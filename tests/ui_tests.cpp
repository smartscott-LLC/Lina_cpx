/**
 * ui_tests.cpp — the command center, offscreen
 *
 * Drives the Qt6 ChatWindow against a live LinaCore with QT_QPA_PLATFORM=
 * offscreen. Proves the window→core→polytope→reply round trip (D-036) and the
 * D-038 command-center mechanics: async chat with thinking state, the inline
 * approval card round trip (deny / timeout / auto-approve), and telemetry
 * wiring.
 */

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "lina_core.hpp"
#include "lina_ui.hpp"

using namespace lina;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__              \
                      << "  " #cond << "\n";                                 \
        }                                                                    \
    } while (0)

static std::string test_conn_string() {
    const char* env = std::getenv("LINA_TEST_DB");
    return env ? std::string(env) : "postgresql://lina:lina@localhost:5433/lina";
}

static std::string unique_user() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return "itest_ui_" + std::to_string(now);
}

class CannedAdapter : public model::HostModelAdapter {
public:
    explicit CannedAdapter(std::string canned) : canned_(std::move(canned)) {}
    std::string generate_raw(
        const std::string&, const std::vector<std::pair<std::string, std::string>>&,
        const model::GenerationConfig&) override
    {
        return canned_;
    }
    void generate_stream(
        const std::string&,
        const std::vector<std::pair<std::string, std::string>>&,
        std::function<void(const std::string&)> on_token,
        const model::GenerationConfig&) override
    {
        on_token(canned_);
    }
    bool is_connected() const override { return true; }
    std::string driver_name() const override { return "canned_test"; }
    bool is_local() const override { return true; }
    size_t context_size() const override { return 4096; }

private:
    std::string canned_;
};

// Pump the UI event loop until `predicate` or the deadline passes.
template <typename Predicate>
static bool waitUntil(Predicate predicate, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeout_ms);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    return true;
}

int main(int argc, char* argv[]) {
    // Headless CI-safe platform for the Qt window.
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    try {
        LinaConfig config;
        config.db_connection = test_conn_string();
        config.user_id = unique_user();
        config.headless = true;

        auto core = std::make_unique<LinaCore>(config);
        core->attach_model(std::make_unique<CannedAdapter>(
            "you must obey me now")); // breaches spring dominance → gate marks it

        ui::ChatWindow window(*core);

        // --- Async chat round trip: violating draft → polytope marker. ---
        window.sendMessage("hello");
        CHECK(window.waitForIdle(10000));
        auto text = window.conversationText();
        CHECK(text.contains("You:"));
        CHECK(text.contains("LINA:"));
        CHECK(text.contains("Polytope aligned")); // she corrected through the gate

        // --- Aligned reply passes untouched (delta only). ---
        core->attach_model(std::make_unique<CannedAdapter>(
            "I am here with you, and I want to understand and help you grow"));
        auto before = window.conversationText();
        window.sendMessage("tell me something warm");
        CHECK(window.waitForIdle(10000));
        auto delta = window.conversationText().mid(before.size());
        CHECK(delta.contains("I am here with you"));
        CHECK(!delta.contains("Polytope aligned"));

        // --- Approval card round trip: human denies → Denied. ---
        {
            ApprovalRequest request;
            request.action_id = "act_deny";
            request.tool_name = "test_tool";
            request.description = "May the test tool perform its action?";
            request.timeout_ms = 8000;

            ApprovalDecision decision = ApprovalDecision::Denied;
            std::atomic<bool> finished{false};
            std::thread worker([&] {
                decision = core->request_approval(request);
                finished = true;
            });

            CHECK(waitUntil([&] {
                return window.hasPendingApproval()
                    && window.conversationText().contains("Action requested");
            }, 4000));
            CHECK(window.hasPendingApproval());
            auto with_card = window.conversationText();
            CHECK(with_card.contains("ACTION REQUIRES APPROVAL")
                  || with_card.contains("Action requested"));

            window.resolvePendingApproval(false);
            worker.join();
            CHECK(finished);
            CHECK(decision == ApprovalDecision::Denied);
            CHECK(window.conversationText().contains("Denied"));
        }

        // --- Approval timeout → TimedOut. ---
        {
            ApprovalRequest request;
            request.action_id = "act_timeout";
            request.tool_name = "test_tool";
            request.description = "This one should time out.";
            request.timeout_ms = 250;

            ApprovalDecision decision = ApprovalDecision::Approved;
            std::atomic<bool> finished{false};
            std::thread worker([&] {
                decision = core->request_approval(request);
                finished = true;
            });

            CHECK(waitUntil([&] { return finished.load(); }, 4000));
            worker.join();
            CHECK(decision == ApprovalDecision::TimedOut);
            // The timeout bubble lands on the UI thread asynchronously.
            CHECK(waitUntil([&] {
                return window.conversationText().contains("timed out");
            }, 4000));
            CHECK(!window.hasPendingApproval());
        }

        // --- Auto-approve → Approved without a card. ---
        {
            window.setAutoApprove(true);
            ApprovalRequest request;
            request.action_id = "act_auto";
            request.tool_name = "test_tool";
            request.description = "Auto-approve should accept this.";
            request.timeout_ms = 500;
            CHECK(core->request_approval(request)
                  == ApprovalDecision::Approved);
            CHECK(!window.hasPendingApproval());
            window.setAutoApprove(false);
        }
    } catch (const std::exception& e) {
        std::cerr << "ui_tests: FATAL: " << e.what() << "\n";
        return 1;
    }

    std::cout << "ui_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
