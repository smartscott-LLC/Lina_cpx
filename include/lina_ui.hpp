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

/// The command center: telemetry + test harness | chat workspace | log reel,
/// bound to a LinaCore (D-038).
class ChatWindow {
public:
    explicit ChatWindow(LinaCore& core);
    ~ChatWindow();

    ChatWindow(const ChatWindow&) = delete;
    ChatWindow& operator=(const ChatWindow&) = delete;

    /// Send a message through the core (async; public for tests).
    void sendMessage(const QString& text);

    /// Current conversation text (public for tests).
    QString conversationText() const;

    /// True while a chat is being processed in the worker thread.
    bool isBusy() const;

    /// Process events until idle (or timeout). Returns false on timeout.
    bool waitForIdle(int timeout_ms) const;

    /// Approval gate state (D-038) — public for tests.
    bool hasPendingApproval() const;
    void resolvePendingApproval(bool approve);
    bool autoApproveEnabled() const;
    void setAutoApprove(bool enabled);

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
