#ifndef LINA_UI_HPP
#define LINA_UI_HPP

/**
 * lina_ui.hpp — the built-in chat window (D-036)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The UI lives inside lina_core (blueprint §8.1). It talks to LinaCore only —
 * never to the symbiote driver (Invariant 4). Qt stays out of the core
 * headers (blueprint §7.1); this header is included only when the UI is
 * enabled. Compiled into lina_core_lib via src/lina_ui.cpp.
 */

#include <QString>

class QTextEdit;

namespace lina {

class LinaCore;

namespace ui {

/// The chat window: conversation view + input line, bound to a LinaCore.
class ChatWindow {
public:
    explicit ChatWindow(LinaCore& core);
    ~ChatWindow();

    ChatWindow(const ChatWindow&) = delete;
    ChatWindow& operator=(const ChatWindow&) = delete;

    /// Send a message through the core (public for tests).
    void sendMessage(const QString& text);

    /// Current conversation text (public for tests).
    QString conversationText() const;

    /// Show the window and run the Qt event loop. Blocks until closed.
    int run();

private:
    LinaCore& core_;
    void* window_; // QMainWindow* — Qt types stay out of this header
};

/// Create the Qt application, show the window, and run the event loop.
int start_chat_window(LinaCore& core);

} // namespace ui
} // namespace lina

#endif // LINA_UI_HPP
