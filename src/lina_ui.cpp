/**
 * lina_ui.cpp — the built-in command center (D-036 rebuilt per D-038)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The high-performance three-column command center:
 *   - Left:  Cognitive & Substrate HUD + Hardware Telemetry + Comprehensive Test Harness (10 suites + CTest)
 *   - Middle: Chat Workspace (Obsidian, Gold & Electric Blue styling, Markdown & Code Highlighting,
 *             Live Deliberation / Reasoning Stream, Inline Approval Cards, Alignment Score HUD)
 *   - Right: Interactive Live Log Reel (Level & Category Filters, Substring Search, Colorized Monospace)
 * plus a full-featured Command Center Settings modal.
 *
 * The window talks to LinaCore only — never to the symbiote driver (Invariant 4).
 * Every reply passes through the polytope first (Invariant 5). Technical
 * events flow on the telemetry bus (Invariant 6) into the log reel — never
 * the cognitive bus.
 *
 * Deliberately moc-free: plain QObject/QWidget subclassing + lambda
 * connections, so no Qt meta-object compiler step is needed.
 */

#include "lina_ui.hpp"

#include <QString>
#include <QStringList>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
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
#include <cmath>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "lina_core.hpp"

namespace lina::ui {

namespace {

// =============================================================================
// THEME — Obsidian Black, Metallic Gold, and Radiant Electric Blue
// =============================================================================

static const char* kCommandCenterQss = R"(
* {
    font-family: "DejaVu Sans", "Segoe UI", sans-serif;
    font-size: 13px;
    color: #e8edf5;
}
QMainWindow, QDialog {
    background: #05070a;
}
#headerBar {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                stop:0 #0b111e, stop:0.5 #080d16, stop:1 #0b111e);
    border-bottom: 2px solid #00d2ff33;
}
#titleLabel {
    color: #ffd700;
    font-size: 16px;
    font-weight: bold;
    letter-spacing: 2px;
}
#subtitleBadge {
    color: #00d2ff;
    font-size: 11px;
    font-weight: bold;
    letter-spacing: 1px;
    background: #00d2ff18;
    border: 1px solid #00d2ff44;
    border-radius: 4px;
    padding: 2px 8px;
}
#panelTitle {
    color: #ffd700;
    font-size: 12px;
    font-weight: bold;
    letter-spacing: 1.5px;
}
QFrame#panel {
    background: #090d14;
    border: 1px solid #1a2333;
    border-radius: 8px;
}
QFrame#hudCard {
    background: #0c111a;
    border: 1px solid #202d42;
    border-radius: 6px;
}
QTextEdit, QLineEdit {
    background: #070a10;
    color: #e8edf5;
    border: 1px solid #1d293d;
    border-radius: 5px;
    padding: 6px;
    selection-background-color: #00d2ff44;
    selection-color: #ffffff;
}
QTextEdit:focus, QLineEdit:focus {
    border: 1px solid #00d2ff;
    background: #090e17;
}
QPushButton {
    background: #111724;
    color: #d1d9e6;
    border: 1px solid #24324a;
    border-radius: 5px;
    padding: 6px 14px;
    font-weight: 500;
}
QPushButton:hover {
    background: #182338;
    border: 1px solid #00d2ff;
    color: #00f0ff;
}
QPushButton:pressed {
    background: #0b0f17;
}
QPushButton:disabled {
    color: #48546a;
    border-color: #161e2b;
    background: #0a0d14;
}
#goldButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ffd700, stop:1 #b8860b);
    color: #05070a;
    font-weight: bold;
    border: 1px solid #ffd700;
}
#goldButton:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ffe44d, stop:1 #d4af37);
    color: #000000;
    border: 1px solid #fff080;
}
#blueButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #00d2ff, stop:1 #0077b6);
    color: #05070a;
    font-weight: bold;
    border: 1px solid #00d2ff;
}
#blueButton:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #48cae4, stop:1 #0096c7);
    color: #000000;
    border: 1px solid #90e0ef;
}
#dangerButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ff4d4f, stop:1 #a81c20);
    color: #ffffff;
    font-weight: bold;
    border: 1px solid #ff4d4f;
}
#dangerButton:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ff7875, stop:1 #cf1322);
}
#silverButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #d1d9e6, stop:1 #8c9ba5);
    color: #05070a;
    font-weight: bold;
    border: 1px solid #d1d9e6;
}
#silverButton:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ffffff, stop:1 #adb9c4);
}
QProgressBar {
    background: #06090e;
    border: 1px solid #1c2638;
    border-radius: 4px;
    height: 12px;
    text-align: center;
}
QProgressBar::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                stop:0 #0077b6, stop:0.6 #00d2ff, stop:1 #ffd700);
    border-radius: 3px;
}
QScrollBar:vertical {
    background: #06090f;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #1c273a;
    border-radius: 5px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover {
    background: #00d2ff;
}
QScrollBar:horizontal {
    background: #06090f;
    height: 10px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #1c273a;
    border-radius: 5px;
    min-width: 24px;
}
QScrollBar::handle:horizontal:hover {
    background: #00d2ff;
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0;
    height: 0;
}
QToolButton {
    background: #0e1420;
    border: 1px solid #202d42;
    border-radius: 5px;
    color: #d1d9e6;
    padding: 5px 10px;
    font-weight: 500;
}
QToolButton:hover {
    border-color: #ffd700;
    color: #ffd700;
}
QCheckBox {
    color: #e8edf5;
    spacing: 8px;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    background: #090e17;
    border: 1px solid #24324a;
    border-radius: 3px;
}
QCheckBox::indicator:checked {
    background: #00d2ff;
    border-color: #00d2ff;
}
QSpinBox, QComboBox {
    background: #070a10;
    color: #e8edf5;
    border: 1px solid #1d293d;
    border-radius: 4px;
    padding: 4px 8px;
}
QSpinBox:focus, QComboBox:focus {
    border: 1px solid #00d2ff;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background: #0c111a;
    color: #e8edf5;
    selection-background-color: #00d2ff33;
    border: 1px solid #202d42;
}
QSplitter::handle {
    background: #141b29;
}
QSplitter::handle:hover {
    background: #00d2ff;
}
#logReel, #testResults {
    font-family: "DejaVu Sans Mono", "Fira Code", monospace;
    font-size: 12px;
    background: #040609;
    border: 1px solid #182233;
}
#thinkingLabel {
    color: #00d2ff;
    font-style: italic;
    font-weight: bold;
}
#attachmentLabel {
    color: #8c9ba5;
    font-size: 11px;
}
#seasonPill {
    color: #ffd700;
    background: #ffd70018;
    border: 1px solid #ffd70055;
    border-radius: 4px;
    font-weight: bold;
    font-size: 11px;
    padding: 2px 6px;
}
#onlinePill {
    color: #00ff9d;
    background: #00ff9d18;
    border: 1px solid #00ff9d55;
    border-radius: 4px;
    font-weight: bold;
    font-size: 11px;
    padding: 2px 6px;
}
#alignmentBadge {
    color: #00f0ff;
    background: #00d2ff18;
    border: 1px solid #00d2ff55;
    border-radius: 4px;
    font-weight: bold;
    font-size: 11px;
    padding: 3px 8px;
}
)";

static const char* kYouBubbleQss =
    "QTextEdit { background: #0c1424; border: 1px solid #00d2ff44;"
    " border-radius: 8px; padding: 8px; color: #e8edf5; }";

static const char* kLinaBubbleQss =
    "QTextEdit { background: #0f1522; border: 1px solid #ffd70055;"
    " border-radius: 8px; padding: 8px; color: #f4f7fc; }";

static const char* kSystemBubbleQss =
    "QTextEdit { background: #080b12; border: 1px dashed #202e47;"
    " border-radius: 6px; padding: 6px; color: #8fa0b5; }";

static QString esc(const QString& text) {
    return text.toHtmlEscaped();
}

// =============================================================================
// MARKDOWN & CODE FORMATTER FOR CHAT BUBBLES
// =============================================================================

static QString formatMarkdown(const QString& raw) {
    if (raw.isEmpty()) return QString();

    QStringList lines = raw.split('\n');
    QString html;
    bool in_code_block = false;
    QString code_block_lang;
    QString code_block_content;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];

        // Code block fences
        if (line.trimmed().startsWith("```")) {
            if (!in_code_block) {
                in_code_block = true;
                code_block_lang = line.trimmed().mid(3).trimmed();
                code_block_content.clear();
            } else {
                in_code_block = false;
                QString lang_badge = code_block_lang.isEmpty()
                    ? QString()
                    : "<div style='color:#00d2ff; font-size:10px; font-weight:bold; "
                      "margin-bottom:4px; text-transform:uppercase;'>"
                      + esc(code_block_lang) + "</div>";
                html += "<div style='background:#04060a; border:1px solid #00d2ff33; "
                        "border-radius:6px; padding:8px 10px; margin:6px 0; "
                        "font-family:\"DejaVu Sans Mono\",monospace; font-size:12px; color:#cbe3fb;'>"
                        + lang_badge + "<pre style='margin:0; white-space:pre-wrap;'>"
                        + esc(code_block_content) + "</pre></div>";
            }
            continue;
        }

        if (in_code_block) {
            if (!code_block_content.isEmpty()) code_block_content += "\n";
            code_block_content += line;
            continue;
        }

        // Headers
        if (line.startsWith("### ")) {
            html += "<h4 style='color:#ffd700; margin:8px 0 4px 0; font-size:14px; font-weight:bold;'>"
                    + esc(line.mid(4)) + "</h4>";
            continue;
        }
        if (line.startsWith("## ")) {
            html += "<h3 style='color:#00d2ff; margin:10px 0 4px 0; font-size:15px; font-weight:bold; border-bottom:1px solid #00d2ff33; padding-bottom:2px;'>"
                    + esc(line.mid(3)) + "</h3>";
            continue;
        }
        if (line.startsWith("# ")) {
            html += "<h2 style='color:#ffd700; margin:12px 0 6px 0; font-size:16px; font-weight:bold; border-bottom:1px solid #ffd70044; padding-bottom:3px;'>"
                    + esc(line.mid(2)) + "</h2>";
            continue;
        }

        // Empty line -> paragraph break
        if (line.trimmed().isEmpty()) {
            html += "<div style='height:8px;'></div>";
            continue;
        }

        // Inline formatting
        QString formatted = esc(line);

        // Bold: **text**
        static const QRegularExpression bold_re(R"(\*\*(.+?)\*\*)");
        formatted.replace(bold_re, R"(<b style="color:#ffffff;">\1</b>)");

        // Italic: *text*
        static const QRegularExpression italic_re(R"(\*(.+?)\*)");
        formatted.replace(italic_re, R"(<i style="color:#b0c4de;">\1</i>)");

        // Inline code: `text`
        static const QRegularExpression code_re(R"(`(.+?)`)");
        formatted.replace(code_re,
            R"(<code style="background:#070d17; color:#00f0ff; padding:2px 5px; border:1px solid #00d2ff33; border-radius:3px; font-family:'DejaVu Sans Mono',monospace; font-size:11.5px;">\1</code>)");

        // Bullet points
        if (line.trimmed().startsWith("- ") || line.trimmed().startsWith("* ")) {
            formatted = "<div style='margin-left:14px; text-indent:-10px;'>"
                        "<span style='color:#00d2ff;'>•</span> "
                        + formatted.mid(formatted.indexOf(line.trimmed().at(0)) + 2) + "</div>";
        } else {
            formatted = "<div style='margin:2px 0;'>" + formatted + "</div>";
        }

        html += formatted;
    }

    if (in_code_block) {
        html += "<div style='background:#04060a; border:1px solid #00d2ff33; "
                "border-radius:6px; padding:8px 10px; margin:6px 0; "
                "font-family:\"DejaVu Sans Mono\",monospace; font-size:12px; color:#cbe3fb;'>"
                "<pre style='margin:0; white-space:pre-wrap;'>"
                + esc(code_block_content) + "</pre></div>";
    }

    return html;
}

// =============================================================================
// LOG REEL — Thread-safe technical log sink with structured filtering and search
// =============================================================================

struct LogEntry {
    QString timestamp;
    QString category;
    QString level;
    QString message;
    int level_num{1}; // 0=debug, 1=info, 2=warn, 3=error

    QString toFormattedHtml() const {
        QString lvl_color = "#7388a9";
        if (level_num == 1) lvl_color = "#00f0ff";
        else if (level_num == 2) lvl_color = "#ffd700";
        else if (level_num == 3) lvl_color = "#ff4d4f";

        QString cat_color = "#00d2ff";
        if (category == "core") cat_color = "#ffd700";
        else if (category == "tool") cat_color = "#00ff9d";
        else if (category == "harness") cat_color = "#b37feb";
        else if (category == "ui") cat_color = "#70a1ff";

        return QString("<span style='color:#5f7595;'>[%1]</span> "
                       "<span style='color:%2; font-weight:bold;'>[%3]</span> "
                       "<span style='color:%4; font-weight:bold;'>[%5]</span> "
                       "<span style='color:#e8edf5;'>%6</span>")
            .arg(esc(timestamp), cat_color, esc(category.toUpper()),
                 lvl_color, esc(level.toUpper()), esc(message));
    }

    QString toPlainText() const {
        return timestamp + " [" + category + "/" + level + "] " + message;
    }
};

class LogReel {
public:
    using RefreshObserver = std::function<void()>;
    using EntryObserver =
        std::function<void(const std::string&, const std::string&, const std::string&)>;

    static LogReel& instance() {
        static LogReel reel;
        return reel;
    }

    void append(const QString& category, const QString& level,
                const QString& message) {
        int lvl = level == "debug" ? 0 : level == "info" ? 1
                : level == "warn"  ? 2 : 3;

        LogEntry entry;
        entry.timestamp = QTime::currentTime().toString("HH:mm:ss");
        entry.category = category.toLower();
        entry.level = level.toLower();
        entry.message = message;
        entry.level_num = lvl;

        std::vector<RefreshObserver> refresh_obs;
        std::vector<EntryObserver> entry_obs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.append(entry);
            while (entries_.size() > max_lines_) entries_.removeFirst();
            refresh_obs = refresh_observers_;
            entry_obs = entry_observers_;
        }

        for (const auto& obs : refresh_obs) obs();
        for (const auto& obs : entry_obs) {
            obs(category.toStdString(), level.toStdString(), message.toStdString());
        }
    }

    void setLevelFilter(int lvl) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            level_filter_ = lvl;
        }
        notifyRefresh();
    }

    void setCategoryFilter(const QString& cat) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            category_filter_ = cat.toLower();
        }
        notifyRefresh();
    }

    void setSearchQuery(const QString& query) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            search_query_ = query.trimmed();
        }
        notifyRefresh();
    }

    void setMaxLines(int n) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_lines_ = n;
        while (entries_.size() > max_lines_) entries_.removeFirst();
    }

    void clear() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.clear();
        }
        notifyRefresh();
    }

    QVector<LogEntry> filteredSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        QVector<LogEntry> result;
        result.reserve(entries_.size());
        for (const auto& e : entries_) {
            if (e.level_num < level_filter_) continue;
            if (category_filter_ != "all" && !category_filter_.isEmpty()
                && e.category != category_filter_) {
                continue;
            }
            if (!search_query_.isEmpty()
                && !e.message.contains(search_query_, Qt::CaseInsensitive)
                && !e.category.contains(search_query_, Qt::CaseInsensitive)) {
                continue;
            }
            result.append(e);
        }
        return result;
    }

    int totalCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    void addRefreshObserver(RefreshObserver observer) {
        std::lock_guard<std::mutex> lock(mutex_);
        refresh_observers_.push_back(std::move(observer));
    }

    void addEntryObserver(EntryObserver observer) {
        std::lock_guard<std::mutex> lock(mutex_);
        entry_observers_.push_back(std::move(observer));
    }

private:
    LogReel() = default;

    void notifyRefresh() {
        std::vector<RefreshObserver> obs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            obs = refresh_observers_;
        }
        for (const auto& o : obs) o();
    }

    mutable std::mutex mutex_;
    QVector<LogEntry> entries_;
    std::vector<RefreshObserver> refresh_observers_;
    std::vector<EntryObserver> entry_observers_;
    int level_filter_{1}; // 0=debug, 1=info, 2=warn, 3=error
    QString category_filter_{"all"};
    QString search_query_{};
    int max_lines_{5000};
};

// =============================================================================
// TELEMETRY MONITOR — Hardware RAM/CPU & Process Memory
// =============================================================================

struct TelemetrySnapshot {
    double ram_percent{-1.0};
    double ram_used_gb{0.0};
    double ram_total_gb{0.0};
    double process_rss_mb{-1.0};
    double cpu_percent{-1.0};
};

class TelemetryMonitor {
public:
    TelemetrySnapshot sample() {
        TelemetrySnapshot s;
        sampleRam(s);
        s.process_rss_mb = sampleProcessRss();
        s.cpu_percent = sampleCpu();
        return s;
    }

private:
    void sampleRam(TelemetrySnapshot& s) {
        std::ifstream f("/proc/meminfo");
        if (!f.is_open()) return;
        double total_kb = -1.0;
        double available_kb = -1.0;
        std::string key;
        unsigned long long val;
        std::string unit;
        while (f >> key >> val >> unit) {
            if (key == "MemTotal:") {
                total_kb = static_cast<double>(val);
            } else if (key == "MemAvailable:") {
                available_kb = static_cast<double>(val);
                break;
            }
        }
        if (total_kb > 0.0 && available_kb >= 0.0) {
            double used_kb = total_kb - available_kb;
            s.ram_percent = (used_kb / total_kb) * 100.0;
            s.ram_used_gb = used_kb / (1024.0 * 1024.0);
            s.ram_total_gb = total_kb / (1024.0 * 1024.0);
        }
    }

    double sampleProcessRss() {
        std::ifstream f("/proc/self/statm");
        if (!f.is_open()) return -1.0;
        unsigned long long size_pages = 0, resident_pages = 0;
        if (f >> size_pages >> resident_pages) {
            long page_size = 4096;
            return (static_cast<double>(resident_pages) * page_size) / (1024.0 * 1024.0);
        }
        return -1.0;
    }

    double sampleCpu() {
        std::ifstream f("/proc/stat");
        if (!f.is_open()) return -1.0;
        std::string cpu;
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        f >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
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
// SETTINGS — Preferences & Configuration Dialog
// =============================================================================

struct UiSettings {
    bool auto_approve{false};
    int approval_timeout_ms{30000};
    int telemetry_interval_ms{1000};
    QString test_binary_dir{"build"};
    int log_level_filter{1}; // 0=debug, 1=info, 2=warn, 3=error
    int max_log_lines{5000};
};

class SettingsDialog : public QDialog {
public:
    explicit SettingsDialog(const UiSettings& current, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("LINA Command Center — Settings");
        setModal(true);
        setStyleSheet(QString::fromLatin1(kCommandCenterQss));
        resize(540, 420);

        auto* main_lay = new QVBoxLayout(this);
        main_lay->setContentsMargins(16, 16, 16, 16);
        main_lay->setSpacing(12);

        auto* tabs = new QTabWidget(this);

        // --- Tab 1: Autonomy & Security ---
        auto* tab1 = new QWidget();
        auto* form1 = new QFormLayout(tab1);
        form1->setContentsMargins(12, 12, 12, 12);
        form1->setSpacing(12);

        auto_approve_ = new QCheckBox("Auto-approve tool actions without human prompting", tab1);
        auto_approve_->setChecked(current.auto_approve);

        approval_timeout_ = new QSpinBox(tab1);
        approval_timeout_->setRange(100, 600000);
        approval_timeout_->setSuffix(" ms");
        approval_timeout_->setValue(current.approval_timeout_ms);

        form1->addRow("Auto-Approve:", auto_approve_);
        form1->addRow("Approval Timeout:", approval_timeout_);
        tabs->addTab(tab1, "🛡 Autonomy");

        // --- Tab 2: Telemetry & Monitoring ---
        auto* tab2 = new QWidget();
        auto* form2 = new QFormLayout(tab2);
        form2->setContentsMargins(12, 12, 12, 12);
        form2->setSpacing(12);

        telemetry_interval_ = new QSpinBox(tab2);
        telemetry_interval_->setRange(100, 60000);
        telemetry_interval_->setSuffix(" ms");
        telemetry_interval_->setValue(current.telemetry_interval_ms);

        max_lines_ = new QSpinBox(tab2);
        max_lines_->setRange(100, 20000);
        max_lines_->setValue(current.max_log_lines);

        log_level_ = new QComboBox(tab2);
        log_level_->addItems({"Debug (Verbose)", "Info (Standard)", "Warn (Warnings Only)", "Error (Errors Only)"});
        log_level_->setCurrentIndex(qBound(0, current.log_level_filter, 3));

        form2->addRow("Telemetry Refresh Rate:", telemetry_interval_);
        form2->addRow("Log Reel Max Capacity:", max_lines_);
        form2->addRow("Default Log Level:", log_level_);
        tabs->addTab(tab2, "📊 Telemetry & Logs");

        // --- Tab 3: Test Harness Paths ---
        auto* tab3 = new QWidget();
        auto* form3 = new QFormLayout(tab3);
        form3->setContentsMargins(12, 12, 12, 12);
        form3->setSpacing(12);

        auto* dir_row = new QHBoxLayout();
        binary_dir_ = new QLineEdit(current.test_binary_dir, tab3);
        auto* browse_btn = new QPushButton("Browse…", tab3);
        auto* auto_btn = new QPushButton("Auto-Detect", tab3);
        auto_btn->setObjectName("blueButton");

        connect(browse_btn, &QPushButton::clicked, this, [this] {
            QString dir = QFileDialog::getExistingDirectory(this, "Select Test Binary Directory");
            if (!dir.isEmpty()) binary_dir_->setText(dir);
        });
        connect(auto_btn, &QPushButton::clicked, this, [this] {
            // Test standard build locations
            const QStringList candidates = {
                "build", "../build", "cmake-build-debug", "cmake-build-release", "."
            };
            for (const auto& c : candidates) {
                if (QFileInfo::exists(c + "/value_engine_tests") ||
                    QFileInfo::exists(c + "/orchestrator_tests")) {
                    binary_dir_->setText(c);
                    break;
                }
            }
        });

        dir_row->addWidget(binary_dir_, 1);
        dir_row->addWidget(browse_btn);
        dir_row->addWidget(auto_btn);

        form3->addRow("Build / Binary Directory:", dir_row);
        tabs->addTab(tab3, "⚙️ Test Harness");

        main_lay->addWidget(tabs, 1);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setObjectName("goldButton");
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        main_lay->addWidget(buttons);
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
// APPROVAL CARD — Inline Human-In-The-Loop Action Gate
// =============================================================================

class ApprovalCard : public QFrame {
public:
    ApprovalCard(const ApprovalRequest& request, QWidget* parent)
        : QFrame(parent)
    {
        setObjectName("approvalCard");
        setStyleSheet(
            "QFrame#approvalCard { "
            "  background: #101624; "
            "  border: 2px solid #ffd700; "
            "  border-radius: 8px; "
            "  padding: 8px; "
            "  margin: 6px 0; "
            "}");

        auto* lay = new QVBoxLayout(this);
        lay->setSpacing(8);

        auto* header_row = new QHBoxLayout();
        auto* title = new QLabel(
            "<span style='color:#ffd700; font-size:13px; font-weight:bold;'>"
            "⏸ ACTION REQUIRES APPROVAL</span>", this);
        auto* tool_badge = new QLabel(
            "<span style='color:#00d2ff; background:#00d2ff18; border:1px solid #00d2ff55; "
            "padding:2px 8px; border-radius:4px; font-weight:bold; font-size:11px;'>"
            + esc(QString::fromStdString(request.tool_name)) + "</span>", this);
        header_row->addWidget(title);
        header_row->addSpacing(8);
        header_row->addWidget(tool_badge);
        header_row->addStretch(1);

        auto* desc = new QLabel(
            "<span style='color:#e8edf5; font-size:12.5px;'>"
            + esc(QString::fromStdString(request.description)) + "</span>", this);
        desc->setWordWrap(true);
        desc->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

        auto* row = new QHBoxLayout();
        auto* approve = new QPushButton("✔ Approve Action", this);
        approve->setObjectName("goldButton");
        auto* deny = new QPushButton("✖ Deny", this);
        deny->setObjectName("silverButton");
        auto* timeout = new QLabel(
            "Auto-declines in " + QString::number(request.timeout_ms / 1000) + "s", this);
        timeout->setStyleSheet("color: #8c9ba5; font-size: 11px;");

        row->addWidget(approve);
        row->addWidget(deny);
        row->addStretch(1);
        row->addWidget(timeout);

        lay->addLayout(header_row);
        lay->addWidget(desc);
        lay->addLayout(row);

        connect(approve, &QPushButton::clicked, this, [this] {
            if (on_approved_) on_approved_();
        });
        connect(deny, &QPushButton::clicked, this, [this] {
            if (on_denied_) on_denied_();
        });
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
// COMMAND CENTER WINDOW IMPLEMENTATION
// =============================================================================

class ChatWindowImpl : public QMainWindow {
public:
    explicit ChatWindowImpl(LinaCore& core)
        : QMainWindow(), core_(core)
    {
        setWindowTitle("LINA — Language Intuitive Neural Architecture · Command Center");
        resize(1380, 860);
        setStyleSheet(QString::fromLatin1(kCommandCenterQss));

        buildHeader();
        buildSplitter();

        // Telemetry bus: core technical events -> log reel
        core_.set_telemetry_sink([this](const std::string& message) {
            QMetaObject::invokeMethod(this, [this, message] {
                LogReel::instance().append("core", "info", QString::fromStdString(message));
            }, Qt::QueuedConnection);
        });

        // Approval gate: her tools ask the human through this window
        core_.set_approval_handler([this](const ApprovalRequest& request) {
            return handleApproval(request);
        });

        core_.begin_session();
        session_timer_.start();
        LogReel::instance().append("ui", "info", "Command Center initialized and online");

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
                    "⚡ LINA is deliberating inside 14D polytope" + QString(thinking_dots_, '.'));
            }
        });

        // Initial substrate status in the conversation
        appendBubble("system",
                     esc(QString::fromStdString(core_.get_status())));

        // Populate initial log view
        refreshLogView();
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
        LogReel::instance().append("ui", "info", "Command Center session terminated");
        core_.end_session();
    }

    // ---------------------------------------------------------------- public

    void sendMessage(const QString& text) {
        if (text.trimmed().isEmpty() || busy_) return;

        const QStringList attachments = attachments_;
        QString full = text;
        if (!attachments.isEmpty()) {
            full = "[Attached: " + attachments.join(", ") + "]\n" + full;
        }
        appendBubble("You", esc(full));
        input_->clear();
        attachments_.clear();
        updateAttachmentLabel();

        // Multimodal image attachment flow (D-046)
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
        core_.begin_turn(full.toStdString(), makeTurnCallbacks(),
                         image_path.toStdString());
    }

    LinaCore::TurnCallbacks makeTurnCallbacks() {
        LinaCore::TurnCallbacks cb;
        cb.on_thought = [this](const std::string& text) {
            QMetaObject::invokeMethod(this, [this, text] {
                appendThought(QString::fromStdString(text));
            }, Qt::QueuedConnection);
        };
        cb.on_rolling_score = [this](double score) {
            QMetaObject::invokeMethod(this, [this, score] {
                updateAlignmentBadge(score);
            }, Qt::QueuedConnection);
        };
        cb.on_tool_call = [this](const std::string& json) {
            QMetaObject::invokeMethod(this, [this, json] {
                appendBubble("system",
                             "🔧 <b>TOOL INVOCATION:</b> " + esc(QString::fromStdString(json)));
            }, Qt::QueuedConnection);
        };
        cb.on_tool_result = [this](const std::string& name, bool ok,
                                   const std::string& summary) {
            QMetaObject::invokeMethod(this, [this, name, ok, summary] {
                appendBubble("system",
                             (ok ? "✔ <b>TOOL RESULT: " : "✖ <b>TOOL FAILED: ")
                                 + esc(QString::fromStdString(name)) + "</b> — "
                                 + esc(QString::fromStdString(summary)));
                LogReel::instance().append(
                    "tool", ok ? "info" : "warn",
                    QString::fromStdString(name) + (ok ? " ok: " : " failed: ")
                        + QString::fromStdString(summary));
            }, Qt::QueuedConnection);
        };
        cb.on_complete = [this](const std::string& reply) {
            QMetaObject::invokeMethod(this, [this, reply] {
                setBusy(false);
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
                LogReel::instance().append("ui", "error", QString::fromStdString(error));
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
            LogReel::instance().append("ui", "info", "approval resolved=approved");
        } else {
            appendBubble("system", "✖ Denied — the action is declined.");
            LogReel::instance().append("ui", "info", "approval resolved=denied");
        }
        try {
            approval_promise_.set_value(
                approve ? ApprovalDecision::Approved : ApprovalDecision::Denied);
        } catch (...) {
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
        h->setContentsMargins(18, 10, 18, 10);
        h->setSpacing(12);

        auto* title = new QLabel("LINA · COMMAND CENTER", header);
        title->setObjectName("titleLabel");

        auto* subtitle = new QLabel("SUBSTRATE KERNEL 14D", header);
        subtitle->setObjectName("subtitleBadge");

        online_pill_ = new QLabel("● KERNEL ONLINE", header);
        online_pill_->setObjectName("onlinePill");

        season_pill_ = new QLabel("SEASON: SUMMER", header);
        season_pill_->setObjectName("seasonPill");

        auto* settings_button = new QToolButton(header);
        settings_button->setText("⚙ Settings");
        connect(settings_button, &QToolButton::clicked, this, [this] {
            openSettings();
        });

        h->addWidget(title);
        h->addWidget(subtitle);
        h->addSpacing(8);
        h->addWidget(online_pill_);
        h->addWidget(season_pill_);
        h->addStretch(1);
        h->addWidget(settings_button);

        setMenuWidget(header);
    }

    void buildSplitter() {
        splitter_ = new QSplitter(Qt::Horizontal, this);
        splitter_->addWidget(buildLeftPanel());
        splitter_->addWidget(buildMiddlePanel());
        splitter_->addWidget(buildRightPanel());
        splitter_->setStretchFactor(0, 3);
        splitter_->setStretchFactor(1, 4);
        splitter_->setStretchFactor(2, 3);
        splitter_->setSizes({430, 520, 430});
        setCentralWidget(splitter_);
    }

    QWidget* buildPanel(const QString& title, QWidget* parent) {
        auto* panel = new QFrame(parent);
        panel->setObjectName("panel");
        auto* lay = new QVBoxLayout(panel);
        lay->setContentsMargins(12, 10, 12, 12);
        lay->setSpacing(8);

        auto* label = new QLabel(title, panel);
        label->setObjectName("panelTitle");
        lay->addWidget(label);
        return panel;
    }

    QWidget* buildLeftPanel() {
        auto* panel = buildPanel("TELEMETRY & TEST HARNESS", this);
        auto* panel_lay = qobject_cast<QVBoxLayout*>(panel->layout());

        // --- 1. Substrate & Cognitive HUD Card ---
        auto* hud_group = new QFrame(panel);
        hud_group->setObjectName("hudCard");
        auto* hud_lay = new QVBoxLayout(hud_group);
        hud_lay->setContentsMargins(10, 8, 10, 8);
        hud_lay->setSpacing(4);

        auto* hud_title = new QLabel(
            "<span style='color:#ffd700; font-weight:bold; font-size:11px;'>◈ SUBSTRATE & CONGNITIVE HUD</span>", hud_group);
        hud_lay->addWidget(hud_title);

        hud_model_label_ = new QLabel("Host Voice: Connecting…", hud_group);
        hud_model_label_->setStyleSheet("color:#00d2ff; font-size:11.5px; font-weight:500;");
        hud_memory_label_ = new QLabel("MPS Memory: Active 0 | Subconscious 0 | Legacy 0", hud_group);
        hud_memory_label_->setStyleSheet("color:#d1d9e6; font-size:11.5px;");
        hud_substrate_label_ = new QLabel("Polytope: 14D Lattice · Exact Rational Math", hud_group);
        hud_substrate_label_->setStyleSheet("color:#8fa0b5; font-size:11px;");

        hud_lay->addWidget(hud_model_label_);
        hud_lay->addWidget(hud_memory_label_);
        hud_lay->addWidget(hud_substrate_label_);
        panel_lay->addWidget(hud_group);

        // --- 2. Hardware Telemetry Card ---
        auto* tele_group = new QFrame(panel);
        tele_group->setObjectName("hudCard");
        auto* tele = new QVBoxLayout(tele_group);
        tele->setContentsMargins(10, 8, 10, 8);
        tele->setSpacing(4);

        auto* tele_title = new QLabel(
            "<span style='color:#00d2ff; font-weight:bold; font-size:11px;'>📊 HARDWARE TELEMETRY</span>", tele_group);
        tele->addWidget(tele_title);

        ram_label_ = new QLabel("System RAM —", tele_group);
        ram_bar_ = new QProgressBar(tele_group);
        ram_bar_->setRange(0, 100);
        ram_bar_->setTextVisible(false);

        process_ram_label_ = new QLabel("Kernel RSS —", tele_group);
        process_ram_label_->setStyleSheet("color:#8fa0b5; font-size:11px;");

        cpu_label_ = new QLabel("Host CPU —", tele_group);
        cpu_bar_ = new QProgressBar(tele_group);
        cpu_bar_->setRange(0, 100);
        cpu_bar_->setTextVisible(false);

        session_label_ = new QLabel("Session Uptime: 00:00:00", tele_group);
        session_label_->setStyleSheet("color:#ffd700; font-weight:bold; font-size:11.5px;");

        tele->addWidget(ram_label_);
        tele->addWidget(ram_bar_);
        tele->addWidget(process_ram_label_);
        tele->addSpacing(2);
        tele->addWidget(cpu_label_);
        tele->addWidget(cpu_bar_);
        tele->addSpacing(2);
        tele->addWidget(session_label_);
        panel_lay->addWidget(tele_group);

        // --- 3. Test Harness Section ---
        auto* harness = new QFrame(panel);
        harness->setObjectName("hudCard");
        auto* harness_lay = new QVBoxLayout(harness);
        harness_lay->setContentsMargins(10, 8, 10, 8);
        harness_lay->setSpacing(6);

        auto* harness_header = new QHBoxLayout();
        auto* harness_title = new QLabel(
            "<span style='color:#ffd700; font-weight:bold; font-size:11px;'>🧪 TEST HARNESS (10 SUITES)</span>", harness);
        harness_status_badge_ = new QLabel("IDLE", harness);
        harness_status_badge_->setStyleSheet(
            "color:#8fa0b5; background:#141d2b; border:1px solid #23324a; "
            "border-radius:3px; padding:1px 6px; font-size:10px; font-weight:bold;");
        harness_header->addWidget(harness_title);
        harness_header->addStretch(1);
        harness_header->addWidget(harness_status_badge_);
        harness_lay->addLayout(harness_header);

        // Grid of test suite buttons
        auto* suite_scroll = new QScrollArea(harness);
        suite_scroll->setWidgetResizable(true);
        suite_scroll->setFrameShape(QFrame::NoFrame);
        suite_scroll->setMaximumHeight(160);

        auto* suite_container = new QWidget(suite_scroll);
        auto* suite_lay = new QVBoxLayout(suite_container);
        suite_lay->setContentsMargins(0, 0, 4, 0);
        suite_lay->setSpacing(4);

        const struct { const char* label; const char* binary; const char* desc; } suites[] = {
            {"1. Value Engine", "value_engine_tests", "14D Polytope & Exact Math"},
            {"2. Memory Module", "memory_module_tests", "3-Tier MPS & Subconscious"},
            {"3. DragonCache", "dragoncache_tests", "Hugepages & SPSC Rings"},
            {"4. Storage Backend", "storage_tests", "PostgreSQL & pgvector"},
            {"5. Orchestrator", "orchestrator_tests", "Core Binds & Seasons"},
            {"6. UI Offscreen", "ui_tests", "Qt6 Command Center Roundtrip"},
            {"7. Llama Voice", "llama_adapter_tests", "Voice Driver & Multimodal"},
            {"8. Tool Engine", "tool_engine_tests", "Approval-Gated Hands"},
            {"9. Stream Parser", "stream_parser_tests", "Thought/Tool/EOT Classifier"},
            {"10. Browser Driver", "browser_driver_tests", "Pure C++ CDP Hands"}
        };

        for (const auto& suite : suites) {
            auto* btn = new QPushButton(QString("%1 — %2").arg(suite.label, suite.desc), suite_container);
            btn->setToolTip(QString("Run %1").arg(suite.binary));
            btn->setStyleSheet("text-align:left; padding:4px 8px; font-size:11.5px;");
            connect(btn, &QPushButton::clicked, this, [this, b = QString(suite.binary)] {
                runSuite(b);
            });
            suite_lay->addWidget(btn);
            harness_buttons_.append(btn);
        }
        suite_container->setLayout(suite_lay);
        suite_scroll->setWidget(suite_container);
        harness_lay->addWidget(suite_scroll);

        // Action row (Run All CTest + Stop + Clear)
        auto* action_row = new QHBoxLayout();
        auto* all_btn = new QPushButton("▶ Run All (CTest)", harness);
        all_btn->setObjectName("goldButton");
        connect(all_btn, &QPushButton::clicked, this, [this] {
            runSuite("ctest", {"--output-on-failure"});
        });
        harness_buttons_.append(all_btn);

        auto* clear_results_btn = new QPushButton("Clear", harness);
        connect(clear_results_btn, &QPushButton::clicked, this, [this] {
            results_view_->clear();
            harness_status_badge_->setText("IDLE");
            harness_status_badge_->setStyleSheet(
                "color:#8fa0b5; background:#141d2b; border:1px solid #23324a; "
                "border-radius:3px; padding:1px 6px; font-size:10px; font-weight:bold;");
        });

        action_row->addWidget(all_btn, 2);
        action_row->addWidget(clear_results_btn, 1);
        harness_lay->addLayout(action_row);

        results_view_ = new QTextEdit(harness);
        results_view_->setObjectName("testResults");
        results_view_->setReadOnly(true);
        results_view_->setPlaceholderText("Test harness execution logs appear here…");
        harness_lay->addWidget(results_view_, 1);

        panel_lay->addWidget(harness, 1);
        return panel;
    }

    QWidget* buildMiddlePanel() {
        auto* panel = buildPanel("CHAT WORKSPACE", this);

        // Message stream
        messages_area_ = new QScrollArea(panel);
        messages_area_->setWidgetResizable(true);
        messages_area_->setFrameShape(QFrame::NoFrame);
        messages_container_ = new QWidget(messages_area_);
        messages_layout_ = new QVBoxLayout(messages_container_);
        messages_layout_->setContentsMargins(6, 6, 6, 6);
        messages_layout_->setSpacing(10);
        messages_layout_->addStretch(1);
        messages_area_->setWidget(messages_container_);

        // Attachment row
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
        attach_row->addWidget(attachment_label_, 1);

        // Input & Controls row
        auto* input_row = new QHBoxLayout();
        input_ = new QTextEdit(panel);
        input_->setPlaceholderText(
            "Message LINA…  (Ctrl+Enter to send, Shift+Enter for newline)");
        input_->setMinimumHeight(40);
        input_->setMaximumHeight(180);
        input_->setAcceptRichText(false);

        score_label_ = new QLabel("", panel);
        score_label_->setObjectName("alignmentBadge");
        score_label_->setVisible(false);

        send_button_ = new QPushButton("Send", panel);
        send_button_->setObjectName("goldButton");
        stop_button_ = new QPushButton("■ Stop", panel);
        stop_button_->setObjectName("dangerButton");
        stop_button_->setEnabled(false);

        input_row->addWidget(input_, 1);
        input_row->addWidget(score_label_);
        input_row->addWidget(send_button_);
        input_row->addWidget(stop_button_);

        connect(send_button_, &QPushButton::clicked, this, [this] {
            sendMessage(input_->toPlainText());
        });
        connect(stop_button_, &QPushButton::clicked, this, [this] {
            core_.stop_turn();
            LogReel::instance().append("ui", "info", "turn cancellation requested");
        });

        auto* send_shortcut = new QShortcut(QKeySequence("Ctrl+Return"), input_);
        connect(send_shortcut, &QShortcut::activated, this, [this] {
            sendMessage(input_->toPlainText());
        });

        connect(input_->document(), &QTextDocument::contentsChanged,
                this, [this] { updateInputHeight(); });

        auto* panel_lay = qobject_cast<QVBoxLayout*>(panel->layout());
        panel_lay->addWidget(messages_area_, 1);
        panel_lay->addLayout(attach_row);
        panel_lay->addLayout(input_row);
        return panel;
    }

    QWidget* buildRightPanel() {
        auto* panel = buildPanel("LIVE LOG REEL", this);

        // Controls row
        auto* controls = new QHBoxLayout();
        pause_button_ = new QPushButton("⏸ Pause", panel);
        connect(pause_button_, &QPushButton::clicked, this, [this] {
            reel_paused_ = !reel_paused_;
            pause_button_->setText(
                reel_paused_ ? "▶ Resume" : "⏸ Pause");
        });

        auto* clear_button = new QPushButton("✕ Clear", panel);
        connect(clear_button, &QPushButton::clicked, this, [this] {
            LogReel::instance().clear();
        });

        category_combo_ = new QComboBox(panel);
        category_combo_->addItems({"All Categories", "core", "tool", "harness", "ui", "telemetry"});
        connect(category_combo_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            QString cat = text.startsWith("All") ? "all" : text.trimmed();
            LogReel::instance().setCategoryFilter(cat);
        });

        level_combo_ = new QComboBox(panel);
        level_combo_->addItems({"Debug+", "Info+", "Warn+", "Error"});
        level_combo_->setCurrentIndex(1); // Default Info+
        connect(level_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            LogReel::instance().setLevelFilter(idx);
        });

        controls->addWidget(pause_button_);
        controls->addWidget(clear_button);
        controls->addWidget(category_combo_);
        controls->addWidget(level_combo_);

        // Search row
        auto* search_row = new QHBoxLayout();
        search_input_ = new QLineEdit(panel);
        search_input_->setPlaceholderText("🔍 Filter logs by text…");
        search_input_->setClearButtonEnabled(true);
        connect(search_input_, &QLineEdit::textChanged, this, [this](const QString& text) {
            LogReel::instance().setSearchQuery(text);
        });

        log_count_label_ = new QLabel("0 logs", panel);
        log_count_label_->setStyleSheet("color:#8c9ba5; font-size:11px;");

        search_row->addWidget(search_input_, 1);
        search_row->addWidget(log_count_label_);

        log_view_ = new QTextEdit(panel);
        log_view_->setObjectName("logReel");
        log_view_->setReadOnly(true);
        log_view_->setPlaceholderText("Live substrate & core telemetry logs…");

        LogReel::instance().addRefreshObserver([this] {
            QMetaObject::invokeMethod(this, [this] {
                refreshLogView();
            }, Qt::QueuedConnection);
        });

        LogReel::instance().addEntryObserver(
            [this](const std::string& category, const std::string& level,
                   const std::string& message) {
                if (category == "ui" || category == "harness") {
                    core_.append_telemetry_log(category, level, message);
                }
            });

        auto* panel_lay = qobject_cast<QVBoxLayout*>(panel->layout());
        panel_lay->addLayout(controls);
        panel_lay->addLayout(search_row);
        panel_lay->addWidget(log_view_, 1);
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
            level_combo_->setCurrentIndex(settings_.log_level_filter);
            LogReel::instance().append("ui", "info", "Command Center settings updated");
        }
    }

    void refreshLogView() {
        if (!log_view_) return;
        const auto entries = LogReel::instance().filteredSnapshot();
        log_count_label_->setText(QString("%1 logs").arg(entries.size()));

        // Fast rebuild using HTML blocks
        QString full_html;
        full_html.reserve(entries.size() * 128);
        for (const auto& e : entries) {
            full_html += "<div style='margin:1px 0;'>" + e.toFormattedHtml() + "</div>";
        }
        log_view_->setHtml(full_html);

        if (!reel_paused_) {
            auto* sb = log_view_->verticalScrollBar();
            sb->setValue(sb->maximum());
        }
    }

    void tickTelemetry() {
        const auto snap = telemetry_.sample();
        updateGauge(ram_label_, ram_bar_, "System RAM", snap.ram_percent,
                    QString("%1 GB / %2 GB").arg(QString::number(snap.ram_used_gb, 'f', 1),
                                                 QString::number(snap.ram_total_gb, 'f', 1)));

        if (snap.process_rss_mb >= 0.0) {
            process_ram_label_->setText(
                QString("Kernel Process RSS: %1 MB").arg(QString::number(snap.process_rss_mb, 'f', 1)));
        }

        updateGauge(cpu_label_, cpu_bar_, "Host CPU", snap.cpu_percent);
        session_label_->setText("Session Uptime: " + formatElapsed(session_timer_.elapsed()));

        // Update Substrate & Cognitive HUD info
        try {
            std::string season = core_.value_engine().constraints().season;
            season_pill_->setText(QString("SEASON: %1").arg(QString::fromStdString(season).toUpper()));

            if (core_.has_model()) {
                hud_model_label_->setText(QString("Host Voice: %1 (Connected)").arg(QString::fromStdString(core_.model().driver_name())));
            } else {
                hud_model_label_->setText("Host Voice: Standby (Subordinate Compute)");
            }

            size_t active_mem = core_.memory_module().store()->fetch_by_status("active").size();
            hud_memory_label_->setText(QString("MPS Memory: Active %1 items (Consolidated)").arg(active_mem));
        } catch (...) {
        }

        if (++telemetry_tick_ % 30 == 0) {
            LogReel::instance().append(
                "telemetry", "debug",
                "ram=" + QString::number(snap.ram_percent, 'f', 1)
                + "% cpu=" + QString::number(snap.cpu_percent, 'f', 1) + "%");
        }
    }

    static void updateGauge(QLabel* label, QProgressBar* bar,
                            const QString& name, double value,
                            const QString& extra = QString())
    {
        if (value < 0.0) {
            label->setText(name + " n/a");
            bar->setValue(0);
            return;
        }
        QString txt = name + " " + QString::number(value, 'f', 1) + "%";
        if (!extra.isEmpty()) txt += " (" + extra + ")";
        label->setText(txt);
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

    void updateAlignmentBadge(double score) {
        score_label_->setVisible(true);
        QString status_txt = score >= 0.80 ? "ALIGNED" : score >= 0.60 ? "DRIFT" : "VIOLATION";
        QString color = score >= 0.80 ? "#00ff9d" : score >= 0.60 ? "#ffd700" : "#ff4d4f";
        score_label_->setText(
            QString("<span style='color:%1;'>◈ %2: %3</span>")
                .arg(color, status_txt, QString::number(score, 'f', 2)));
    }

    void updateInputHeight() {
        if (!input_) return;
        const int doc_h = static_cast<int>(input_->document()->size().height()) + 20;
        const int max_h = qMax(40, static_cast<int>(messages_area_->height() * 0.25));
        input_->setFixedHeight(qBound(40, doc_h, max_h));
    }

    QTextEdit* appendBubble(const QString& who, const QString& content) {
        auto* te = new QTextEdit(messages_container_);
        te->setReadOnly(true);
        te->setFrameShape(QFrame::NoFrame);
        te->setStyleSheet(QString::fromLatin1(
            who == "You"     ? kYouBubbleQss
            : who == "LINA"  ? kLinaBubbleQss
                             : kSystemBubbleQss));
        te->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

        QString header_html;
        if (who == "You") {
            header_html = "<div style='color:#00d2ff; font-weight:bold; font-size:12px; margin-bottom:4px;'>👤 You:</div>";
        } else if (who == "LINA") {
            header_html = "<div style='color:#ffd700; font-weight:bold; font-size:12px; margin-bottom:4px;'>"
                          "✨ LINA: <span style='color:#00ff9d; font-size:10px; font-weight:normal; "
                          "background:#00ff9d18; border:1px solid #00ff9d44; padding:1px 5px; border-radius:3px;'>ALIGNED</span></div>";
        } else if (!who.isEmpty()) {
            header_html = "<div style='color:#70a1ff; font-weight:bold; font-size:11px; margin-bottom:2px;'>"
                          "⚙️ " + esc(who) + ":</div>";
        }

        // Format markdown body for messages
        QString body_html = (who == "LINA" || who == "You") ? formatMarkdown(content) : content;
        te->setHtml(header_html + body_html);

        te->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(te->document(), &QTextDocument::contentsChanged, te, [te] {
            te->setFixedHeight(static_cast<int>(te->document()->size().height()) + 20);
        });
        messages_layout_->insertWidget(messages_layout_->count() - 1, te);
        bubbles_.append(te);
        scrollMessagesToBottom();
        return te;
    }

    void showThinking() {
        thinking_ = new QLabel(messages_container_);
        thinking_->setObjectName("thinkingLabel");
        thinking_->setText("⚡ LINA is deliberating inside 14D polytope");
        thinking_dots_ = 0;
        messages_layout_->insertWidget(messages_layout_->count() - 1, thinking_);
        thinking_timer_->start(250);
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

    void appendThought(const QString& text) {
        if (!thinking_pane_) {
            thinking_pane_ = new QTextEdit(messages_container_);
            thinking_pane_->setReadOnly(true);
            thinking_pane_->setFrameShape(QFrame::NoFrame);
            thinking_pane_->setStyleSheet(
                "QTextEdit { background: #060b14; border: 1px solid #00d2ff55; "
                "border-radius: 6px; padding: 8px; color: #70c4ff; }");
            thinking_pane_->setTextInteractionFlags(
                Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
            thinking_pane_->setHtml(
                "<div style='color:#00d2ff; font-weight:bold; font-size:11px; margin-bottom:4px;'>"
                "⚡ REASONING STREAM (DELIBERATION)</div>");
            connect(thinking_pane_->document(), &QTextDocument::contentsChanged,
                    thinking_pane_, [this] {
                thinking_pane_->setFixedHeight(static_cast<int>(
                    thinking_pane_->document()->size().height()) + 20);
            });
            thinking_pane_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            messages_layout_->insertWidget(messages_layout_->count() - 1, thinking_pane_);
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

    void setBusy(bool busy) {
        busy_ = busy;
        send_button_->setEnabled(!busy);
        stop_button_->setEnabled(busy);
        input_->setEnabled(!busy);
        if (busy) {
            showThinking();
            LogReel::instance().append("ui", "info", "Turn started: processing through 14D substrate");
        } else {
            hideThinking();
            score_label_->setVisible(false);
            LogReel::instance().append("ui", "info", "Turn completed");
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

    QString resolveBinaryPath(const QString& binary, QString& working_dir) const {
        if (binary == "ctest") {
            working_dir = settings_.test_binary_dir;
            return "ctest";
        }

        const QStringList candidate_dirs = {
            settings_.test_binary_dir,
            ".",
            "build",
            "../build",
            "/home/server/CLionProjects/Lina_cpx/build",
            QCoreApplication::applicationDirPath()
        };

        for (const auto& dir : candidate_dirs) {
            QString path = dir + "/" + binary;
            if (QFileInfo::exists(path) && QFileInfo(path).isExecutable()) {
                working_dir = dir;
                return path;
            }
        }

        working_dir = settings_.test_binary_dir;
        return settings_.test_binary_dir + "/" + binary;
    }

    void runSuite(const QString& binary) {
        runSuite(binary, QStringList());
    }

    void runSuite(const QString& binary, const QStringList& args) {
        if (running_) return;

        QString work_dir;
        const QString program = resolveBinaryPath(binary, work_dir);

        harness_status_badge_->setText("RUNNING");
        harness_status_badge_->setStyleSheet(
            "color:#00d2ff; background:#00d2ff18; border:1px solid #00d2ff55; "
            "border-radius:3px; padding:1px 6px; font-size:10px; font-weight:bold;");

        appendResults(QString("<div style='margin-top:8px; padding-bottom:4px; border-bottom:1px solid #1a2333;'>"
                              "<span style='color:#ffd700; font-weight:bold;'>▶ [RUNNING TEST]:</span> "
                              "<span style='color:#00f0ff;'>%1</span> %2</div>")
                      .arg(esc(program), esc(args.join(" "))));

        LogReel::instance().append("harness", "info", "Starting test suite " + program + " " + args.join(" "));

        running_ = new QProcess(this);
        setHarnessEnabled(false);

        connect(running_, &QProcess::readyReadStandardOutput, this, [this] {
            QString out = QString::fromUtf8(running_->readAllStandardOutput());
            // Highlight passing / error markers
            out = esc(out);
            out.replace("Passed", "<span style='color:#00ff9d; font-weight:bold;'>Passed</span>");
            out.replace("Failed", "<span style='color:#ff4d4f; font-weight:bold;'>Failed</span>");
            out.replace("100% tests passed", "<span style='color:#00ff9d; font-weight:bold;'>100% tests passed</span>");
            appendResults("<div style='color:#d1d9e6; margin:1px 0;'>" + out + "</div>");
        });

        connect(running_, &QProcess::readyReadStandardError, this, [this] {
            QString err = esc(QString::fromUtf8(running_->readAllStandardError()));
            appendResults("<div style='color:#ff7875; margin:1px 0;'>" + err + "</div>");
        });

        connect(running_, &QProcess::finished, this,
                [this, program](int code, QProcess::ExitStatus) {
            bool ok = (code == 0);
            QString color = ok ? "#00ff9d" : "#ff4d4f";
            QString status_text = ok ? "PASSED" : "FAILED";

            harness_status_badge_->setText(status_text);
            harness_status_badge_->setStyleSheet(
                QString("color:%1; background:%118; border:1px solid %155; "
                        "border-radius:3px; padding:1px 6px; font-size:10px; font-weight:bold;").arg(color));

            appendResults(QString("<div style='margin-top:6px; margin-bottom:8px; padding:4px; "
                                  "background:#0a101a; border:1px solid %144; border-radius:4px;'>"
                                  "<span style='color:%1; font-weight:bold;'>■ [TEST %2]:</span> "
                                  "<span style='color:#e8edf5;'>%3 (exit code %4)</span></div>")
                          .arg(color, status_text, esc(program), QString::number(code)));

            LogReel::instance().append(
                "harness", ok ? "info" : "error",
                "Test finished: " + program + " exit=" + QString::number(code));

            running_->deleteLater();
            running_ = nullptr;
            setHarnessEnabled(true);
        });

        running_->setWorkingDirectory(work_dir);
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
                "Action auto-approved id="
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
                     "⏸ Action requested — tool <b>"
                     + esc(QString::fromStdString(request.tool_name)) + "</b>: "
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

    // header pills
    QLabel* online_pill_ = nullptr;
    QLabel* season_pill_ = nullptr;

    // left panel
    TelemetryMonitor telemetry_;
    QLabel* hud_model_label_ = nullptr;
    QLabel* hud_memory_label_ = nullptr;
    QLabel* hud_substrate_label_ = nullptr;
    QLabel* ram_label_ = nullptr;
    QProgressBar* ram_bar_ = nullptr;
    QLabel* process_ram_label_ = nullptr;
    QLabel* cpu_label_ = nullptr;
    QProgressBar* cpu_bar_ = nullptr;
    QLabel* session_label_ = nullptr;
    QLabel* harness_status_badge_ = nullptr;
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
    QComboBox* category_combo_ = nullptr;
    QComboBox* level_combo_ = nullptr;
    QLineEdit* search_input_ = nullptr;
    QLabel* log_count_label_ = nullptr;

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
    return static_cast<ChatWindowImpl*>(window_)->conversationText();
}

bool ChatWindow::isBusy() const {
    return static_cast<ChatWindowImpl*>(window_)->isBusy();
}

bool ChatWindow::waitForIdle(int timeout_ms) const {
    return static_cast<ChatWindowImpl*>(window_)->waitForIdle(timeout_ms);
}

bool ChatWindow::hasPendingApproval() const {
    return static_cast<ChatWindowImpl*>(window_)->hasPendingApproval();
}

void ChatWindow::resolvePendingApproval(bool approve) {
    static_cast<ChatWindowImpl*>(window_)->resolvePendingApproval(approve);
}

bool ChatWindow::autoApproveEnabled() const {
    return static_cast<ChatWindowImpl*>(window_)->autoApproveEnabled();
}

void ChatWindow::setAutoApprove(bool enabled) {
    static_cast<ChatWindowImpl*>(window_)->setAutoApprove(enabled);
}

int ChatWindow::run() {
    auto* win = static_cast<ChatWindowImpl*>(window_);
    win->show();
    return QApplication::exec();
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
