/**
 * lina_ui.cpp — the built-in command center (D-036 rebuilt per D-038)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The three-column command center (blueprint §8.1, principal layout spec):
 *   - Left:  telemetry (RAM / CPU / session time) + test harness
 *   - Middle: chat workspace (selectable bubbles, attachments, expanding
 *     input, thinking indicator, inline approval cards)
 *   - Right: live log reel (pause/resume autoscroll)
 * plus a top-level settings modal (auto-approve, timeouts, thresholds).
 *
 * The window talks to LinaCore only — never the symbiote driver (Invariant 4).
 * Every reply passes through the polytope first (Invariant 5). Technical
 * events flow on the telemetry bus (Invariant 6) into the log reel — never
 * the cognitive bus.
 *
 * Deliberately moc-free: plain QObject/QWidget subclassing + lambda
 * connections, so no Qt meta-object compiler step is needed.
 */

#include "lina_ui.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTextDocument>
#include <QTextEdit>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "lina_core.hpp"

namespace lina::ui {

namespace {

// =============================================================================
// THEME — obsidian marble / midnight blue, metallic gold + silver accents
// =============================================================================

static const char* kCommandCenterQss = R"(
* { font-family: "DejaVu Sans", sans-serif; font-size: 13px; }
QMainWindow, QDialog { background: #0b0e14; }
#headerBar {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                stop:0 #151c2b, stop:1 #0e131c);
    border-bottom: 1px solid #2b3550;
}
#titleLabel { color: #d4af37; font-size: 15px; font-weight: bold;
              letter-spacing: 2px; }
#panelTitle { color: #c9a227; font-size: 11px; font-weight: bold;
              letter-spacing: 1px; }
QLabel { color: #dfe6f2; }
QFrame#panel { background: #12161f; border: 1px solid #232c44;
               border-radius: 6px; }
QTextEdit, QLineEdit {
    background: #0f141e; color: #dfe6f2;
    border: 1px solid #2b3550; border-radius: 4px; padding: 4px;
    selection-background-color: #3d4f73; selection-color: #ffffff;
}
QTextEdit:focus, QLineEdit:focus { border: 1px solid #c9a227; }
QPushButton {
    background: #1c2436; color: #c7cddc;
    border: 1px solid #2b3550; border-radius: 4px; padding: 5px 12px;
}
QPushButton:hover { background: #242e46; border: 1px solid #c9a227;
                    color: #e8c04a; }
QPushButton:pressed { background: #161d2c; }
QPushButton:disabled { color: #5a637a; border-color: #232a3c;
                       background: #141a26; }
#goldButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #d4af37, stop:1 #a8842a);
    color: #12161f; font-weight: bold; border: 1px solid #d4af37;
}
#goldButton:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #e8c95a, stop:1 #c9a227);
}
#silverButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #c7cddc, stop:1 #98a1b5);
    color: #12161f; font-weight: bold; border: 1px solid #c7cddc;
}
QProgressBar {
    background: #0f141e; border: 1px solid #2b3550; border-radius: 3px;
    height: 10px; text-align: center;
}
QProgressBar::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                stop:0 #c9a227, stop:1 #6b8cff);
    border-radius: 2px;
}
QScrollBar:vertical { background: #0e131c; width: 10px; }
QScrollBar::handle:vertical { background: #2b3550; border-radius: 5px;
                              min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #c9a227; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QToolButton { background: transparent; border: 1px solid #2b3550;
              border-radius: 4px; color: #c7cddc; padding: 4px 8px; }
QToolButton:hover { border-color: #c9a227; color: #e8c04a; }
QCheckBox { color: #dfe6f2; spacing: 6px; }
QSpinBox, QComboBox { background: #0f141e; color: #dfe6f2;
                      border: 1px solid #2b3550; border-radius: 4px;
                      padding: 3px; }
QComboBox::drop-down { border: none; }
QComboBox QAbstractItemView { background: #12161f; color: #dfe6f2;
                              selection-background-color: #2b3a55; }
#logReel, #testResults {
    font-family: "DejaVu Sans Mono", monospace; font-size: 12px;
    background: #0a0d12;
}
#thinkingLabel { color: #98a1b5; font-style: italic; }
#attachmentLabel { color: #98a1b5; font-size: 11px; }
)";

static const char* kYouBubbleQss =
    "QTextEdit { background: #16203a; border: 1px solid #2b3550;"
    " border-radius: 8px; padding: 6px; color: #dfe6f2; }";
static const char* kLinaBubbleQss =
    "QTextEdit { background: #1a2a3f; border: 1px solid #3d4f73;"
    " border-radius: 8px; padding: 6px; color: #e8ecf5; }";
static const char* kSystemBubbleQss =
    "QTextEdit { background: #141a26; border: 1px dashed #2b3550;"
    " border-radius: 6px; padding: 4px; color: #98a1b5; }";

static QString esc(const QString& text) {
    return text.toHtmlEscaped();
}

// =============================================================================
// LOG REEL — thread-safe technical log sink (Invariant 6: telemetry bus).
// Moc-free: observers instead of Qt signals.
// =============================================================================

class LogReel {
public:
    using LineObserver = std::function<void(const QString&)>;
    using ClearObserver = std::function<void()>;
    using EntryObserver =
        std::function<void(const QString&, const QString&, const QString&)>;

    static LogReel& instance() {
        static LogReel reel;
        return reel;
    }

    void append(const QString& category, const QString& level,
                const QString& message) {
        int lvl = level == "debug" ? 0 : level == "info"   ? 1
                   : level == "warn" ? 2 : 3;
        if (lvl < level_filter_) return;

        QString line = QTime::currentTime().toString("HH:mm:ss")
                       + " [" + category + "/" + level + "] " + message;
        std::vector<LineObserver> observers;
        std::vector<EntryObserver> entry_observers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.append(line);
            while (lines_.size() > max_lines_) lines_.removeFirst();
            observers = line_observers_;
            entry_observers = entry_observers_;
        }
        for (const auto& observer : observers) observer(line);
        for (const auto& observer : entry_observers) {
            observer(category, level, message);
        }
    }

    void setLevelFilter(int lvl) { level_filter_ = lvl; }
    void setMaxLines(int n) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_lines_ = n;
        while (lines_.size() > max_lines_) lines_.removeFirst();
    }
    void clear() {
        std::vector<ClearObserver> observers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.clear();
            observers = clear_observers_;
        }
        for (const auto& observer : observers) observer();
    }

    // UI-thread-only: seed a fresh view with the retained lines.
    QVector<QString> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_;
    }

    void addLineObserver(LineObserver observer) {
        std::lock_guard<std::mutex> lock(mutex_);
        line_observers_.push_back(std::move(observer));
    }

    void addClearObserver(ClearObserver observer) {
        std::lock_guard<std::mutex> lock(mutex_);
        clear_observers_.push_back(std::move(observer));
    }

    void addEntryObserver(EntryObserver observer) {
        std::lock_guard<std::mutex> lock(mutex_);
        entry_observers_.push_back(std::move(observer));
    }

private:
    LogReel() = default;
    mutable std::mutex mutex_;
    QVector<QString> lines_;
    std::vector<LineObserver> line_observers_;
    std::vector<ClearObserver> clear_observers_;
    std::vector<EntryObserver> entry_observers_;
    int level_filter_{1};   // 0=debug 1=info 2=warn 3=error
    int max_lines_{2000};
};

// =============================================================================
// TELEMETRY MONITOR — RAM / CPU from /proc (Linux); graceful n/a otherwise
// =============================================================================

struct TelemetrySnapshot {
    double ram_percent{-1.0};
    double cpu_percent{-1.0};
};

class TelemetryMonitor {
public:
    TelemetrySnapshot sample() {
        TelemetrySnapshot s;
        s.ram_percent = sampleRam();
        s.cpu_percent = sampleCpu();
        return s;
    }

private:
    double sampleRam() {
        std::ifstream f("/proc/meminfo");
        if (!f.is_open()) return -1.0;
        double total = -1.0;
        double available = -1.0;
        std::string key;
        unsigned long long val;
        std::string unit;
        while (f >> key >> val >> unit) {
            if (key == "MemTotal:") {
                total = static_cast<double>(val);
            } else if (key == "MemAvailable:") {
                available = static_cast<double>(val);
                break;
            }
        }
        if (total <= 0.0 || available < 0.0) return -1.0;
        return (1.0 - available / total) * 100.0;
    }

    double sampleCpu() {
        std::ifstream f("/proc/stat");
        if (!f.is_open()) return -1.0;
        std::string cpu;
        unsigned long long user, nice, system, idle, iowait, irq, softirq,
            steal;
        f >> cpu >> user >> nice >> system >> idle >> iowait >> irq
          >> softirq >> steal;
        if (cpu != "cpu") return -1.0;
        unsigned long long total =
            user + nice + system + idle + iowait + irq + softirq + steal;
        unsigned long long idle_total = idle + iowait;
        double percent = 0.0;
        if (prev_total_ > 0 && total >= prev_total_) {
            double d_total = static_cast<double>(total - prev_total_);
            double d_idle = static_cast<double>(idle_total - prev_idle_);
            if (d_total > 0.0) {
                percent = (1.0 - d_idle / d_total) * 100.0;
            }
        }
        prev_total_ = total;
        prev_idle_ = idle_total;
        return percent;
    }

    unsigned long long prev_total_{0};
    unsigned long long prev_idle_{0};
};

// =============================================================================
// SETTINGS — persisted per-session window preferences
// =============================================================================

struct UiSettings {
    bool auto_approve{false};
    int approval_timeout_ms{30000};
    int telemetry_interval_ms{1000};
    QString test_binary_dir{"build"};
    int log_level_filter{1}; // info
    int max_log_lines{2000};
};

class SettingsDialog : public QDialog {
public:
    explicit SettingsDialog(const UiSettings& current, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("LINA — Settings");
        setModal(true);

        auto* form = new QFormLayout(this);

        auto_approve_ = new QCheckBox(
            "Auto-approve tool actions without asking", this);
        auto_approve_->setChecked(current.auto_approve);

        approval_timeout_ = new QSpinBox(this);
        approval_timeout_->setRange(100, 600000);
        approval_timeout_->setSuffix(" ms");
        approval_timeout_->setValue(current.approval_timeout_ms);

        telemetry_interval_ = new QSpinBox(this);
        telemetry_interval_->setRange(100, 60000);
        telemetry_interval_->setSuffix(" ms");
        telemetry_interval_->setValue(current.telemetry_interval_ms);

        binary_dir_ = new QLineEdit(current.test_binary_dir, this);

        log_level_ = new QComboBox(this);
        log_level_->addItems({"debug", "info", "warn", "error"});
        log_level_->setCurrentIndex(
            qBound(0, current.log_level_filter, 3));

        max_lines_ = new QSpinBox(this);
        max_lines_->setRange(100, 10000);
        max_lines_->setValue(current.max_log_lines);

        form->addRow("Auto-approve", auto_approve_);
        form->addRow("Approval timeout", approval_timeout_);
        form->addRow("Telemetry interval", telemetry_interval_);
        form->addRow("Test binary directory", binary_dir_);
        form->addRow("Log level filter", log_level_);
        form->addRow("Log reel capacity", max_lines_);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);

        setStyleSheet(QString::fromLatin1(kCommandCenterQss));
        resize(440, 320);
    }

    UiSettings values() const {
        UiSettings s;
        s.auto_approve = auto_approve_->isChecked();
        s.approval_timeout_ms = approval_timeout_->value();
        s.telemetry_interval_ms = telemetry_interval_->value();
        s.test_binary_dir = binary_dir_->text().trimmed();
        s.log_level_filter = log_level_->currentIndex();
        s.max_log_lines = max_lines_->value();
        return s;
    }

private:
    QCheckBox* auto_approve_ = nullptr;
    QSpinBox* approval_timeout_ = nullptr;
    QSpinBox* telemetry_interval_ = nullptr;
    QLineEdit* binary_dir_ = nullptr;
    QComboBox* log_level_ = nullptr;
    QSpinBox* max_lines_ = nullptr;
};

// =============================================================================
// APPROVAL CARD — inline human-in-the-loop decision (blueprint §6 actions)
// =============================================================================

class ApprovalCard : public QFrame {
public:
    ApprovalCard(const ApprovalRequest& request, QWidget* parent)
        : QFrame(parent)
    {
        setObjectName("approvalCard");
        auto* lay = new QVBoxLayout(this);

        auto* title = new QLabel(
            "<b style='color:#c9a227'>⏸ ACTION REQUIRES APPROVAL</b>", this);
        auto* desc = new QLabel(
            "Tool: <b>" + esc(QString::fromStdString(request.tool_name))
            + "</b><br>" + esc(QString::fromStdString(request.description)),
            this);
        desc->setWordWrap(true);
        desc->setTextInteractionFlags(Qt::TextSelectableByMouse
                                      | Qt::TextSelectableByKeyboard);

        auto* row = new QHBoxLayout();
        auto* approve = new QPushButton("✔ Approve", this);
        approve->setObjectName("goldButton");
        auto* deny = new QPushButton("✖ Deny", this);
        deny->setObjectName("silverButton");
        auto* timeout = new QLabel(
            "Auto-declines after "
            + QString::number(request.timeout_ms / 1000) + "s", this);
        timeout->setStyleSheet("color: #98a1b5; font-size: 10px;");
        row->addWidget(approve);
        row->addWidget(deny);
        row->addStretch(1);
        row->addWidget(timeout);

        lay->addWidget(title);
        lay->addWidget(desc);
        lay->addLayout(row);

        connect(approve, &QPushButton::clicked, this,
                [this] { if (on_approved_) on_approved_(); });
        connect(deny, &QPushButton::clicked, this,
                [this] { if (on_denied_) on_denied_(); });
    }

    void setDecisionHandlers(std::function<void()> on_approved,
                             std::function<void()> on_denied)
    {
        on_approved_ = std::move(on_approved);
        on_denied_ = std::move(on_denied);
    }

private:
    std::function<void()> on_approved_;
    std::function<void()> on_denied_;
};

// =============================================================================
// THE COMMAND CENTER WINDOW
// =============================================================================

class ChatWindowImpl : public QMainWindow {
public:
    explicit ChatWindowImpl(LinaCore& core)
        : QMainWindow(), core_(core)
    {
        setWindowTitle("LINA — Language Intuitive Neural Architecture");
        resize(1280, 800);
        setStyleSheet(QString::fromLatin1(kCommandCenterQss));

        buildHeader();
        buildSplitter();

        // Telemetry bus: core events → log reel (technical bus only).
        core_.set_telemetry_sink([this](const std::string& message) {
            QMetaObject::invokeMethod(this, [this, message] {
                LogReel::instance().append(
                    "core", "info", QString::fromStdString(message));
            }, Qt::QueuedConnection);
        });

        // Approval gate: her tools ask the human through this window.
        core_.set_approval_handler([this](const ApprovalRequest& request) {
            return handleApproval(request);
        });

        // Seed the reel with what already happened.
        const auto retained = LogReel::instance().snapshot();
        for (const auto& line : retained) log_view_->append(line);
        log_view_->verticalScrollBar()->setValue(
            log_view_->verticalScrollBar()->maximum());

        core_.begin_session();
        session_timer_.start();
        LogReel::instance().append("ui", "info", "command center opened");

        telemetry_timer_ = new QTimer(this);
        connect(telemetry_timer_, &QTimer::timeout, this, [this] {
            tickTelemetry();
        });
        telemetry_timer_->start(settings_.telemetry_interval_ms);

        thinking_timer_ = new QTimer(this);
        connect(thinking_timer_, &QTimer::timeout, this, [this] {
            if (thinking_) {
                thinking_dots_ = (thinking_dots_ + 1) % 4;
                thinking_->setText(
                    "LINA is thinking" + QString(thinking_dots_, '.'));
            }
        });

        // Initial status line in the conversation.
        appendBubble("system",
                     esc(QString::fromStdString(core_.get_status())));
    }

    ~ChatWindowImpl() override {
        destroyed_ = true;
        core_.set_approval_handler(nullptr);
        core_.set_telemetry_sink(nullptr);
        if (running_) {
            running_->kill();
            running_->deleteLater();
            running_ = nullptr;
        }
        LogReel::instance().append("ui", "info", "command center closed");
        core_.end_session();
    }

    // ---------------------------------------------------------------- public

    void sendMessage(const QString& text) {
        if (text.trimmed().isEmpty() || busy_) return;

        // Capture the attachment set before clearing it.
        const QStringList attachments = attachments_;

        QString full = text;
        if (!attachments.isEmpty()) {
            full = "[Attached: " + attachments.join(", ") + "]\n" + full;
        }
        appendBubble("You", esc(full));
        input_->clear();
        attachments_.clear();
        updateAttachmentLabel();

        // D-046: the first image attachment rides this turn as her eyes — the
        // multimodal prompt decodes it at the frame boundary. Everything else
        // stays in the text prefix (workspace files, etc.).
        QString image_path;
        for (const QString& attachment : attachments) {
            const QString ext = QFileInfo(attachment).suffix().toLower();
            if (ext == "png" || ext == "jpg" || ext == "jpeg"
                || ext == "bmp" || ext == "gif" || ext == "webp") {
                image_path = attachment;
                break;
            }
        }

        setBusy(true);
        // D-041: the open-window turn driver — she processes on her own thread.
        core_.begin_turn(full.toStdString(), makeTurnCallbacks(),
                         image_path.toStdString());
    }

    // D-041: the streaming event channel — every callback marshals to the UI
    // thread (the turn worker is not a QObject).
    LinaCore::TurnCallbacks makeTurnCallbacks() {
        LinaCore::TurnCallbacks cb;
        cb.on_thought = [this](const std::string& text) {
            QMetaObject::invokeMethod(this, [this, text] {
                appendThought(QString::fromStdString(text));
            }, Qt::QueuedConnection);
        };
        cb.on_rolling_score = [this](double score) {
            QMetaObject::invokeMethod(this, [this, score] {
                score_label_->setText(
                    "alignment " + QString::number(score, 'f', 2));
            }, Qt::QueuedConnection);
        };
        cb.on_tool_call = [this](const std::string& json) {
            QMetaObject::invokeMethod(this, [this, json] {
                appendBubble("system",
                             "🔧 tool call — "
                                 + esc(QString::fromStdString(json)));
            }, Qt::QueuedConnection);
        };
        cb.on_tool_result = [this](const std::string& name, bool ok,
                                   const std::string& summary) {
            QMetaObject::invokeMethod(this, [this, name, ok, summary] {
                appendBubble("system",
                             (ok ? "✔ " : "✖ ")
                                 + esc(QString::fromStdString(name)) + " — "
                                 + esc(QString::fromStdString(summary)));
                LogReel::instance().append(
                    "tool", ok ? "info" : "warn",
                    QString::fromStdString(name)
                        + (ok ? " ok: " : " failed: ")
                        + QString::fromStdString(summary));
            }, Qt::QueuedConnection);
        };
        cb.on_complete = [this](const std::string& reply) {
            QMetaObject::invokeMethod(this, [this, reply] {
                setBusy(false);
                // D-047: a withheld turn (empty payload) is silence — clear
                // the thinking state, show no bubble.
                if (!reply.empty()) {
                    appendBubble("LINA", esc(QString::fromStdString(reply)));
                }
            }, Qt::QueuedConnection);
        };
        cb.on_window = [this](const std::string& event) {
            QMetaObject::invokeMethod(this, [this, event] {
                appendBubble("system", esc(QString::fromStdString(event)));
            }, Qt::QueuedConnection);
        };
        cb.on_error = [this](const std::string& error) {
            QMetaObject::invokeMethod(this, [this, error] {
                LogReel::instance().append(
                    "ui", "error", QString::fromStdString(error));
                setBusy(false);
            }, Qt::QueuedConnection);
        };
        return cb;
    }

    QString conversationText() const {
        QString out;
        for (const auto* bubble : bubbles_) {
            if (!out.isEmpty()) out += "\n";
            out += bubble->toPlainText();
        }
        return out;
    }

    bool isBusy() const { return busy_; }

    bool waitForIdle(int timeout_ms) const {
        auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeout_ms);
        while (busy_) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(5);
        }
        return true;
    }

    bool hasPendingApproval() const { return approval_pending_; }

    void resolvePendingApproval(bool approve) {
        std::lock_guard<std::mutex> lock(approval_mutex_);
        if (!approval_pending_) return;
        approval_pending_ = false;
        removeApprovalCard();
        if (approve) {
            appendBubble("system", "✔ Approved — the action may proceed.");
            LogReel::instance().append(
                "ui", "info", "approval resolved=approved");
        } else {
            appendBubble("system", "✖ Denied — the action is declined.");
            LogReel::instance().append(
                "ui", "info", "approval resolved=denied");
        }
        try {
            approval_promise_.set_value(
                approve ? ApprovalDecision::Approved
                        : ApprovalDecision::Denied);
        } catch (...) {
            // promise already satisfied (timeout raced the click) — ignore
        }
    }

    bool autoApproveEnabled() const { return settings_.auto_approve; }
    void setAutoApprove(bool enabled) { settings_.auto_approve = enabled; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QMainWindow::resizeEvent(event);
        updateInputHeight();
    }

private:
    // ------------------------------------------------------------------ layout

    void buildHeader() {
        auto* header = new QWidget(this);
        header->setObjectName("headerBar");
        auto* h = new QHBoxLayout(header);
        h->setContentsMargins(14, 8, 14, 8);

        auto* title = new QLabel("LINA — COMMAND CENTER", header);
        title->setObjectName("titleLabel");

        auto* settings_button = new QToolButton(header);
        settings_button->setText("⚙ Settings");
        connect(settings_button, &QToolButton::clicked, this, [this] {
            openSettings();
        });

        h->addWidget(title);
        h->addStretch(1);
        h->addWidget(settings_button);

        setMenuWidget(header);
    }

    void buildSplitter() {
        splitter_ = new QSplitter(Qt::Horizontal, this);
        splitter_->addWidget(buildLeftPanel());
        splitter_->addWidget(buildMiddlePanel());
        splitter_->addWidget(buildRightPanel());
        splitter_->setStretchFactor(0, 1);
        splitter_->setStretchFactor(1, 1);
        splitter_->setStretchFactor(2, 1);
        splitter_->setSizes({420, 420, 420});
        setCentralWidget(splitter_);
    }

    QWidget* buildPanel(const QString& title, QWidget* parent) {
        auto* panel = new QFrame(parent);
        panel->setObjectName("panel");
        auto* lay = new QVBoxLayout(panel);
        lay->setContentsMargins(10, 8, 10, 10);

        auto* label = new QLabel(title, panel);
        label->setObjectName("panelTitle");
        lay->addWidget(label);
        return panel;
    }

    QWidget* buildLeftPanel() {
        auto* panel = buildPanel("TELEMETRY & TEST HARNESS", this);

        // --- Telemetry gauges ---
        auto* tele_group = new QFrame(panel);
        tele_group->setStyleSheet(
            "QFrame { background: #0f141e; border: 1px solid #232c44;"
            " border-radius: 4px; }");
        auto* tele = new QVBoxLayout(tele_group);
        tele->setContentsMargins(8, 6, 8, 6);

        ram_label_ = new QLabel("RAM —", tele_group);
        ram_bar_ = new QProgressBar(tele_group);
        ram_bar_->setRange(0, 100);
        ram_bar_->setTextVisible(false);
        cpu_label_ = new QLabel("CPU —", tele_group);
        cpu_bar_ = new QProgressBar(tele_group);
        cpu_bar_->setRange(0, 100);
        cpu_bar_->setTextVisible(false);
        session_label_ = new QLabel("Session 00:00:00", tele_group);

        tele->addWidget(ram_label_);
        tele->addWidget(ram_bar_);
        tele->addSpacing(4);
        tele->addWidget(cpu_label_);
        tele->addWidget(cpu_bar_);
        tele->addSpacing(4);
        tele->addWidget(session_label_);

        // --- Test harness ---
        auto* harness = new QFrame(panel);
        auto* harness_lay = new QVBoxLayout(harness);
        harness_lay->setContentsMargins(0, 8, 0, 0);

        const struct { const char* label; const char* binary; } suites[] = {
            {"Value Engine", "value_engine_tests"},
            {"Memory", "memory_module_tests"},
            {"Storage", "storage_tests"},
            {"Orchestrator", "orchestrator_tests"},
            {"UI", "ui_tests"},
        };
        for (const auto& suite : suites) {
            auto* btn = new QPushButton(suite.label, harness);
            btn->setToolTip(
                QString("Run %1 from the test binary directory").arg(suite.binary));
            connect(btn, &QPushButton::clicked, this, [this, suite] {
                runSuite(suite.binary);
            });
            harness_lay->addWidget(btn);
            harness_buttons_.append(btn);
        }
        auto* all_btn = new QPushButton("All (ctest)", harness);
        connect(all_btn, &QPushButton::clicked, this, [this] {
            runSuite("ctest", {"--test-dir", settings_.test_binary_dir,
                               "--output-on-failure"});
        });
        harness_lay->addWidget(all_btn);
        harness_buttons_.append(all_btn);

        results_view_ = new QTextEdit(harness);
        results_view_->setObjectName("testResults");
        results_view_->setReadOnly(true);
        results_view_->setPlaceholderText("Test results appear here.");
        harness_lay->addWidget(results_view_, /*stretch=*/1);

        auto* panel_lay = qobject_cast<QVBoxLayout*>(panel->layout());
        panel_lay->addWidget(tele_group);
        panel_lay->addWidget(harness, /*stretch=*/1);
        return panel;
    }

    QWidget* buildMiddlePanel() {
        auto* panel = buildPanel("CHAT WORKSPACE", this);

        // Message stream (widget bubbles → full selection/copy + inline cards).
        messages_area_ = new QScrollArea(panel);
        messages_area_->setWidgetResizable(true);
        messages_area_->setFrameShape(QFrame::NoFrame);
        messages_container_ = new QWidget(messages_area_);
        messages_layout_ = new QVBoxLayout(messages_container_);
        messages_layout_->setContentsMargins(4, 4, 4, 4);
        messages_layout_->addStretch(1); // keeps bubbles packed at the top
        messages_area_->setWidget(messages_container_);

        // Attachment row.
        auto* attach_row = new QHBoxLayout();
        auto* attach_btn = new QPushButton("📎 Attach", panel);
        connect(attach_btn, &QPushButton::clicked, this, [this] {
            const auto files = QFileDialog::getOpenFileNames(
                this, "Attach files to the workspace");
            if (!files.isEmpty()) {
                attachments_ += files;
                updateAttachmentLabel();
            }
        });
        auto* attach_dir_btn = new QPushButton("📁 Attach Folder", panel);
        connect(attach_dir_btn, &QPushButton::clicked, this, [this] {
            const auto dir = QFileDialog::getExistingDirectory(
                this, "Attach a folder to the workspace");
            if (!dir.isEmpty()) {
                attachments_ << dir;
                updateAttachmentLabel();
            }
        });
        auto* clear_btn = new QPushButton("✕ Clear", panel);
        connect(clear_btn, &QPushButton::clicked, this, [this] {
            attachments_.clear();
            updateAttachmentLabel();
        });
        attachment_label_ = new QLabel("No attachments", panel);
        attachment_label_->setObjectName("attachmentLabel");
        attach_row->addWidget(attach_btn);
        attach_row->addWidget(attach_dir_btn);
        attach_row->addWidget(clear_btn);
        attach_row->addWidget(attachment_label_, /*stretch=*/1);

        // Expanding input + send.
        auto* input_row = new QHBoxLayout();
        input_ = new QTextEdit(panel);
        input_->setPlaceholderText(
            "Message LINA…  (Ctrl+Enter to send)");
        input_->setMinimumHeight(36);
        input_->setMaximumHeight(160);
        input_->setAcceptRichText(false);
        send_button_ = new QPushButton("Send", panel);
        send_button_->setObjectName("goldButton");
        stop_button_ = new QPushButton("■ Stop", panel);
        stop_button_->setEnabled(false);
        score_label_ = new QLabel("", panel);
        score_label_->setObjectName("thinkingLabel");
        input_row->addWidget(input_, /*stretch=*/1);
        input_row->addWidget(score_label_);
        input_row->addWidget(send_button_);
        input_row->addWidget(stop_button_);

        connect(send_button_, &QPushButton::clicked, this, [this] {
            sendMessage(input_->toPlainText());
        });
        connect(stop_button_, &QPushButton::clicked, this, [this] {
            core_.stop_turn(); // D-041: stream cancellation
            LogReel::instance().append("ui", "info", "stop requested");
        });
        auto* send_shortcut = new QShortcut(
            QKeySequence("Ctrl+Return"), input_);
        connect(send_shortcut, &QShortcut::activated, this, [this] {
            sendMessage(input_->toPlainText());
        });
        connect(input_->document(), &QTextDocument::contentsChanged,
                this, [this] { updateInputHeight(); });

        auto* panel_lay = qobject_cast<QVBoxLayout*>(panel->layout());
        panel_lay->addWidget(messages_area_, /*stretch=*/1);
        panel_lay->addLayout(attach_row);
        panel_lay->addLayout(input_row);
        return panel;
    }

    QWidget* buildRightPanel() {
        auto* panel = buildPanel("LIVE LOG REEL", this);

        auto* controls = new QHBoxLayout();
        pause_button_ = new QPushButton("⏸ Pause reel", panel);
        connect(pause_button_, &QPushButton::clicked, this, [this] {
            reel_paused_ = !reel_paused_;
            pause_button_->setText(
                reel_paused_ ? "▶ Resume reel" : "⏸ Pause reel");
        });
        auto* clear_button = new QPushButton("✕ Clear", panel);
        connect(clear_button, &QPushButton::clicked, this, [this] {
            LogReel::instance().clear();
        });
        controls->addWidget(pause_button_);
        controls->addWidget(clear_button);
        controls->addStretch(1);

        log_view_ = new QTextEdit(panel);
        log_view_->setObjectName("logReel");
        log_view_->setReadOnly(true);
        log_view_->setPlaceholderText("System log stream…");

        LogReel::instance().addLineObserver([this](const QString& line) {
            // Observers may fire from worker threads — marshal to the UI thread.
            QMetaObject::invokeMethod(this, [this, line] {
                appendLogLine(line);
            }, Qt::QueuedConnection);
        });
        LogReel::instance().addClearObserver([this]() {
            QMetaObject::invokeMethod(this, [this] {
                log_view_->clear();
            }, Qt::QueuedConnection);
        });
        // D-043: UI-owned technical logs persist on the telemetry bus; core
        // events are already persisted by the core itself (no duplicates).
        LogReel::instance().addEntryObserver(
            [this](const QString& category, const QString& level,
                   const QString& message) {
                if (category == "ui" || category == "harness") {
                    core_.append_telemetry_log(
                        category.toStdString(), level.toStdString(),
                        message.toStdString());
                }
            });

        auto* panel_lay = qobject_cast<QVBoxLayout*>(panel->layout());
        panel_lay->addLayout(controls);
        panel_lay->addWidget(log_view_, /*stretch=*/1);
        return panel;
    }

    // ---------------------------------------------------------------- helpers

    void openSettings() {
        SettingsDialog dlg(settings_, this);
        if (dlg.exec() == QDialog::Accepted) {
            settings_ = dlg.values();
            telemetry_timer_->setInterval(settings_.telemetry_interval_ms);
            LogReel::instance().setLevelFilter(settings_.log_level_filter);
            LogReel::instance().setMaxLines(settings_.max_log_lines);
            LogReel::instance().append("ui", "info", "settings updated");
        }
    }

    void tickTelemetry() {
        const auto snap = telemetry_.sample();
        updateGauge(ram_label_, ram_bar_, "RAM", snap.ram_percent);
        updateGauge(cpu_label_, cpu_bar_, "CPU", snap.cpu_percent);
        session_label_->setText(
            "Session " + formatElapsed(session_timer_.elapsed()));
        if (++telemetry_tick_ % 30 == 0) {
            LogReel::instance().append(
                "telemetry", "debug",
                "ram=" + QString::number(snap.ram_percent, 'f', 1)
                + "% cpu=" + QString::number(snap.cpu_percent, 'f', 1) + "%");
        }
    }

    static void updateGauge(QLabel* label, QProgressBar* bar,
                            const QString& name, double value) {
        if (value < 0.0) {
            label->setText(name + " n/a");
            bar->setValue(0);
            return;
        }
        label->setText(name + " " + QString::number(value, 'f', 1) + "%");
        bar->setValue(qBound(0, static_cast<int>(value + 0.5), 100));
    }

    static QString formatElapsed(qint64 ms) {
        const qint64 total_secs = ms / 1000;
        const qint64 h = total_secs / 3600;
        const qint64 m = (total_secs % 3600) / 60;
        const qint64 s = total_secs % 60;
        return QString("%1:%2:%3")
            .arg(h, 2, 10, QLatin1Char('0'))
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    }

    void updateInputHeight() {
        if (!input_) return;
        const int doc_h = static_cast<int>(
                              input_->document()->size().height())
                          + 20;
        const int max_h = qMax(36,
            static_cast<int>(messages_area_->height() * 0.20));
        input_->setFixedHeight(qBound(36, doc_h, max_h));
    }

    QTextEdit* appendBubble(const QString& who, const QString& html) {
        auto* te = new QTextEdit(messages_container_);
        te->setReadOnly(true);
        te->setFrameShape(QFrame::NoFrame);
        te->setStyleSheet(QString::fromLatin1(
            who == "You"     ? kYouBubbleQss
            : who == "LINA"  ? kLinaBubbleQss
                             : kSystemBubbleQss));
        te->setTextInteractionFlags(Qt::TextSelectableByMouse
                                    | Qt::TextSelectableByKeyboard);
        const QString content = who.isEmpty()
            ? html
            : "<b style='color:#c9a227'>" + esc(who) + ":</b> " + html;
        te->setHtml(content);
        te->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(te->document(), &QTextDocument::contentsChanged, te, [te] {
            te->setFixedHeight(
                static_cast<int>(te->document()->size().height()) + 16);
        });
        messages_layout_->insertWidget(messages_layout_->count() - 1, te);
        bubbles_.append(te);
        scrollMessagesToBottom();
        return te;
    }

    void showThinking() {
        thinking_ = new QLabel(messages_container_);
        thinking_->setObjectName("thinkingLabel");
        thinking_->setText("LINA is thinking");
        thinking_dots_ = 0;
        messages_layout_->insertWidget(messages_layout_->count() - 1,
                                       thinking_);
        scrollMessagesToBottom();
    }

    void hideThinking() {
        thinking_timer_->stop();
        if (thinking_) {
            delete thinking_;
            thinking_ = nullptr;
        }
        if (thinking_pane_) {
            delete thinking_pane_;
            thinking_pane_ = nullptr;
        }
    }

    // Live stream of her deliberation (D-041) — a growing translucent pane.
    void appendThought(const QString& text) {
        if (!thinking_pane_) {
            thinking_pane_ = new QTextEdit(messages_container_);
            thinking_pane_->setReadOnly(true);
            thinking_pane_->setFrameShape(QFrame::NoFrame);
            thinking_pane_->setStyleSheet(QString::fromLatin1(kSystemBubbleQss));
            thinking_pane_->setTextInteractionFlags(
                Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
            thinking_pane_->setHtml(
                "<i>⟦ thinking ⟧</i> ");
            connect(thinking_pane_->document(), &QTextDocument::contentsChanged,
                    thinking_pane_, [this] {
                thinking_pane_->setFixedHeight(static_cast<int>(
                    thinking_pane_->document()->size().height()) + 16);
            });
            thinking_pane_->setSizePolicy(QSizePolicy::Expanding,
                                          QSizePolicy::Fixed);
            messages_layout_->insertWidget(messages_layout_->count() - 1,
                                           thinking_pane_);
        }
        QTextCursor cursor = thinking_pane_->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertHtml(esc(text) + " ");
        thinking_pane_->setTextCursor(cursor);
        scrollMessagesToBottom();
    }

    void scrollMessagesToBottom() {
        QTimer::singleShot(0, this, [this] {
            auto* sb = messages_area_->verticalScrollBar();
            sb->setValue(sb->maximum());
        });
    }

    void appendLogLine(const QString& line) {
        log_view_->append(line);
        if (!reel_paused_) {
            auto* sb = log_view_->verticalScrollBar();
            sb->setValue(sb->maximum());
        }
    }

    void setBusy(bool busy) {
        busy_ = busy;
        send_button_->setEnabled(!busy);
        stop_button_->setEnabled(busy);
        input_->setEnabled(!busy);
        if (busy) {
            showThinking();
            LogReel::instance().append("ui", "info", "turn started");
        } else {
            hideThinking();
            score_label_->clear();
            LogReel::instance().append("ui", "info", "turn finished");
        }
    }

    void updateAttachmentLabel() {
        if (attachments_.isEmpty()) {
            attachment_label_->setText("No attachments");
            return;
        }
        QString shown = attachments_.join(", ");
        if (shown.size() > 96) shown = shown.left(93) + "…";
        attachment_label_->setText(
            QString::number(attachments_.size()) + " attached: " + shown);
    }

    // ------------------------------------------------------ test harness (QProcess)

    void runSuite(const QString& binary) {
        runSuite(binary, QStringList());
    }

    void runSuite(const QString& binary, const QStringList& args) {
        if (running_) return;
        const QString program = binary == "ctest"
            ? binary
            : settings_.test_binary_dir + "/" + binary;
        appendResults("<span style='color:#c9a227'>▶ Running: " + esc(program)
                      + (args.isEmpty() ? QString()
                                        : " " + esc(args.join(" ")))
                      + "</span>");
        LogReel::instance().append(
            "harness", "info", "start " + program + " " + args.join(" "));

        running_ = new QProcess(this);
        setHarnessEnabled(false);
        connect(running_, &QProcess::readyReadStandardOutput, this, [this] {
            appendResults(esc(QString::fromUtf8(
                running_->readAllStandardOutput())));
        });
        connect(running_, &QProcess::readyReadStandardError, this, [this] {
            appendResults("<span style='color:#e06c75'>"
                          + esc(QString::fromUtf8(
                                running_->readAllStandardError()))
                          + "</span>");
        });
        connect(running_, &QProcess::finished, this,
                [this, program](int code, QProcess::ExitStatus) {
            appendResults("<span style='color:#98c379'>■ Finished: "
                          + esc(program) + " exit=" + QString::number(code)
                          + "</span>");
            LogReel::instance().append(
                "harness", "info",
                "finish " + program + " exit=" + QString::number(code));
            running_->deleteLater();
            running_ = nullptr;
            setHarnessEnabled(true);
        });
        running_->setWorkingDirectory(settings_.test_binary_dir);
        running_->start(program, args);
    }

    void setHarnessEnabled(bool enabled) {
        for (auto* button : harness_buttons_) button->setEnabled(enabled);
    }

    void appendResults(const QString& html) {
        results_view_->append(html);
        auto* sb = results_view_->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

    // -------------------------------------------------------- approval handler

    ApprovalDecision handleApproval(const ApprovalRequest& request) {
        if (settings_.auto_approve) {
            LogReel::instance().append(
                "ui", "info",
                "approval auto-approved id="
                + QString::fromStdString(request.action_id)
                + " tool=" + QString::fromStdString(request.tool_name));
            return ApprovalDecision::Approved;
        }
        {
            std::lock_guard<std::mutex> lock(approval_mutex_);
            if (approval_pending_) return ApprovalDecision::Denied;
            approval_pending_ = true;
            approval_promise_ = std::promise<ApprovalDecision>{};
        }
        auto future = approval_promise_.get_future();

        QMetaObject::invokeMethod(this, [this, request] {
            showApprovalCard(request);
        }, Qt::QueuedConnection);

        const auto status = future.wait_for(
            std::chrono::milliseconds(request.timeout_ms));
        if (status == std::future_status::timeout) {
            approval_pending_ = false;
            QMetaObject::invokeMethod(this, [this] {
                removeApprovalCard();
                appendBubble("system",
                             "⏱ Approval timed out — action declined.");
            }, Qt::QueuedConnection);
            return ApprovalDecision::TimedOut;
        }
        return future.get();
    }

    void showApprovalCard(const ApprovalRequest& request) {
        appendBubble("system",
                     "⏸ Action requested — tool "
                     + esc(QString::fromStdString(request.tool_name)) + ": "
                     + esc(QString::fromStdString(request.description)));
        auto* card = new ApprovalCard(request, messages_container_);
        cards_.append(card);
        messages_layout_->insertWidget(messages_layout_->count() - 1, card);
        card->setDecisionHandlers(
            [this] { resolvePendingApproval(true); },
            [this] { resolvePendingApproval(false); });
        scrollMessagesToBottom();
    }

    void removeApprovalCard() {
        while (!cards_.isEmpty()) {
            QFrame* card = cards_.takeLast();
            messages_layout_->removeWidget(card);
            delete card;
        }
    }

    // ------------------------------------------------------------------ state

    LinaCore& core_;
    UiSettings settings_;
    QElapsedTimer session_timer_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> destroyed_{false};
    int telemetry_tick_{0};

    QSplitter* splitter_ = nullptr;

    // left panel
    TelemetryMonitor telemetry_;
    QLabel* ram_label_ = nullptr;
    QProgressBar* ram_bar_ = nullptr;
    QLabel* cpu_label_ = nullptr;
    QProgressBar* cpu_bar_ = nullptr;
    QLabel* session_label_ = nullptr;
    QTextEdit* results_view_ = nullptr;
    QVector<QPushButton*> harness_buttons_;
    QProcess* running_ = nullptr;

    // middle panel
    QScrollArea* messages_area_ = nullptr;
    QWidget* messages_container_ = nullptr;
    QVBoxLayout* messages_layout_ = nullptr;
    QVector<QTextEdit*> bubbles_;
    QVector<QFrame*> cards_;
    QLabel* thinking_ = nullptr;
    QTimer* thinking_timer_ = nullptr;
    int thinking_dots_{0};
    QTextEdit* input_ = nullptr;
    QPushButton* send_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QLabel* score_label_ = nullptr;
    QTextEdit* thinking_pane_ = nullptr;
    QStringList attachments_;
    QLabel* attachment_label_ = nullptr;

    // right panel
    QTextEdit* log_view_ = nullptr;
    bool reel_paused_{false};
    QPushButton* pause_button_ = nullptr;

    // timers
    QTimer* telemetry_timer_ = nullptr;

    // approval gate
    std::mutex approval_mutex_;
    std::atomic<bool> approval_pending_{false};
    std::promise<ApprovalDecision> approval_promise_;
};

} // namespace

// =============================================================================
// PUBLIC WRAPPER — Qt types stay out of the header (blueprint §7.1)
// =============================================================================

ChatWindow::ChatWindow(LinaCore& core) : core_(core) {
    window_ = new ChatWindowImpl(core);
}

ChatWindow::~ChatWindow() {
    delete static_cast<ChatWindowImpl*>(window_);
}

void ChatWindow::sendMessage(const QString& text) {
    static_cast<ChatWindowImpl*>(window_)->sendMessage(text);
}

QString ChatWindow::conversationText() const {
    return static_cast<const ChatWindowImpl*>(window_)->conversationText();
}

bool ChatWindow::isBusy() const {
    return static_cast<const ChatWindowImpl*>(window_)->isBusy();
}

bool ChatWindow::waitForIdle(int timeout_ms) const {
    return static_cast<const ChatWindowImpl*>(window_)
        ->waitForIdle(timeout_ms);
}

bool ChatWindow::hasPendingApproval() const {
    return static_cast<const ChatWindowImpl*>(window_)
        ->hasPendingApproval();
}

void ChatWindow::resolvePendingApproval(bool approve) {
    static_cast<ChatWindowImpl*>(window_)->resolvePendingApproval(approve);
}

bool ChatWindow::autoApproveEnabled() const {
    return static_cast<const ChatWindowImpl*>(window_)
        ->autoApproveEnabled();
}

void ChatWindow::setAutoApprove(bool enabled) {
    static_cast<ChatWindowImpl*>(window_)->setAutoApprove(enabled);
}

int ChatWindow::run() {
    auto* impl = static_cast<ChatWindowImpl*>(window_);
    impl->show();
    return qApp->exec();
}

int start_chat_window(LinaCore& core) {
    int argc = 1;
    char arg0[] = "lina_core";
    char* argv[] = {arg0, nullptr};
    QApplication app(argc, argv);
    ChatWindow window(core);
    return window.run();
}

} // namespace lina::ui
