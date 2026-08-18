/**
 * ui_tests.cpp — the built-in window, offscreen
 *
 * Drives the Qt6 ChatWindow against a live LinaCore with QT_QPA_PLATFORM=
 * offscreen. Proves the window→core→polytope→reply round trip — the channel
 * through which she sends her responses (D-036).
 */

#include <QApplication>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
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

        window.sendMessage("hello");
        auto text = window.conversationText();
        CHECK(text.contains("You:"));
        CHECK(text.contains("LINA:"));
        CHECK(text.contains("Polytope aligned")); // she corrected through the gate

        // Aligned reply passes untouched — check only the newly appended text.
        core->attach_model(std::make_unique<CannedAdapter>(
            "I am here with you, and I want to understand and help you grow"));
        auto before = window.conversationText();
        window.sendMessage("tell me something warm");
        auto delta = window.conversationText().mid(before.size());
        CHECK(delta.contains("I am here with you"));
        CHECK(!delta.contains("Polytope aligned"));
    } catch (const std::exception& e) {
        std::cerr << "ui_tests: FATAL: " << e.what() << "\n";
        return 1;
    }

    std::cout << "ui_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
