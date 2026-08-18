/**
 * lina_ui.cpp — the built-in Qt6 chat window (Implementation)
 *
 * "Safe by design. Not safe by limitation."
 *
 * She speaks through this window; every message she sends passes through her
 * polytope first (Invariant 5). The window binds to LinaCore — the symbiote
 * driver never touches it (Invariant 4).
 *
 * Deliberately moc-free: the window uses plain QWidget subclassing and
 * lambda connections, so no Qt meta-object compiler step is needed.
 */

#include "lina_ui.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "lina_core.hpp"

namespace lina::ui {

namespace {

class ChatWindowImpl : public QMainWindow {
public:
    explicit ChatWindowImpl(LinaCore& core)
        : QMainWindow(), core_(core)
    {
        setWindowTitle("LINA — Language Intuitive Neural Architecture");
        resize(760, 520);

        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);

        view_ = new QTextEdit(central);
        view_->setReadOnly(true);
        view_->setPlaceholderText("LINA is listening.");

        auto* row = new QHBoxLayout();
        input_ = new QLineEdit(central);
        input_->setPlaceholderText("Message LINA…");
        auto* send = new QPushButton("Send", central);
        row->addWidget(input_, /*stretch=*/1);
        row->addWidget(send);

        layout->addWidget(view_, /*stretch=*/1);
        layout->addLayout(row);
        setCentralWidget(central);

        connect(send, &QPushButton::clicked, this,
                [this] { sendMessage(input_->text()); });
        connect(input_, &QLineEdit::returnPressed, this,
                [this] { sendMessage(input_->text()); });

        core_.begin_session();
        append("LINA", QString::fromStdString(core_.get_status()));
    }

    ~ChatWindowImpl() override {
        core_.end_session();
    }

    void sendMessage(const QString& text) {
        if (text.trimmed().isEmpty()) return;
        append("You", text);
        auto reply = core_.chat(text.toStdString());
        append("LINA", QString::fromStdString(reply));
        input_->clear();
    }

    QString conversationText() const {
        return view_->toPlainText();
    }

private:
    LinaCore& core_;
    QTextEdit* view_ = nullptr;
    QLineEdit* input_ = nullptr;

    void append(const QString& who, const QString& text) {
        view_->append("<b>" + who.toHtmlEscaped() + ":</b> "
                      + text.toHtmlEscaped());
    }
};

} // namespace

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
