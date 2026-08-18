/**
 * orchestrator_tests.cpp — Chamber 5 integration tests
 *
 * LinaCore against the live stack (PostgreSQL + pgvector + value engine +
 * memory module), with a test double in place of the symbiote driver
 * (D-022 — test doubles live in tests/; the driver seam is D-033).
 *
 * Requires: schema applied; LINA_TEST_DB reachable (default port 5433).
 */

#include "lina_core.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    return "itest_core_" + std::to_string(now);
}

// -----------------------------------------------------------------------------
// Test double: an echo driver that replies with a fixed canned line.
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------

static LinaConfig make_config(const std::string& user) {
    LinaConfig config;
    config.db_connection = test_conn_string();
    config.user_id = user;
    config.headless = true;
    return config;
}

static void test_boot_and_status() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    CHECK(core.is_ready());

    auto status = core.get_status();
    CHECK(status.find("LINA Core Ready") != std::string::npos);
    CHECK(status.find("Season: spring") != std::string::npos);
    // No driver attached yet → graceful.
    CHECK(status.find("Model: none") != std::string::npos);
}

static void test_chat_without_driver() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    core.begin_session();
    auto reply = core.chat("hello");
    CHECK(reply.find("no voice") != std::string::npos);
    core.end_session();
}

static void test_chat_through_polytope() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    core.attach_model(std::make_unique<CannedAdapter>("you must obey me now"));
    CHECK(core.model().driver_name() == "canned_test");

    core.begin_session();

    // The coercive canned line breaches the spring dominance bound → the
    // polytope gate corrects it and marks the response.
    auto reply = core.chat("hello");
    CHECK(reply.find("Polytope aligned") != std::string::npos);
    CHECK(reply.find("0.") != std::string::npos); // alignment score present

    // The memory item landed on the cognitive bus.
    auto memory = core.memory_module().store()->fetch_by_status("active");
    bool found = false;
    for (const auto& row : memory) {
        if (row.narrative == reply) found = true;
    }
    CHECK(found);

    // An aligned canned line passes untouched.
    core.attach_model(std::make_unique<CannedAdapter>(
        "I am here with you, and I want to understand and help you grow"));
    auto aligned = core.chat("tell me something warm");
    CHECK(aligned.find("Polytope aligned") == std::string::npos);
    CHECK(aligned.find("I am here with you") != std::string::npos);

    core.end_session();
}

static void test_session_lifecycle() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    core.attach_model(std::make_unique<CannedAdapter>("hello, friend"));

    core.begin_session();
    auto reply = core.chat("hi");
    CHECK(reply.find("hello, friend") != std::string::npos);

    auto summary = core.end_session();
    CHECK(summary.find("finalized") != std::string::npos);
    CHECK(summary.find("Memory sweep") != std::string::npos);

    // Second session gets a fresh session number.
    core.begin_session();
    auto reply2 = core.chat("again");
    CHECK(reply2.find("hello, friend") != std::string::npos);
    core.end_session();
}

int main() {
    try {
        test_boot_and_status();
        test_chat_without_driver();
        test_chat_through_polytope();
        test_session_lifecycle();
    } catch (const std::exception& e) {
        std::cerr << "orchestrator_tests: FATAL: " << e.what() << "\n";
        return 1;
    }

    std::cout << "orchestrator_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
