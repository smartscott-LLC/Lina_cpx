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

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace lina;

namespace fs = std::filesystem;

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
// Test double: a scripted driver that plays canned lines in order and records
// the history it was given — determinism for the reflection loop (D-037).
// -----------------------------------------------------------------------------

class ScriptedAdapter : public model::HostModelAdapter {
public:
    explicit ScriptedAdapter(std::vector<std::string> lines)
        : lines_(std::move(lines)) {}

    std::string generate_raw(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& history,
        const model::GenerationConfig&) override
    {
        ++call_count_;
        last_system_ = system_prompt;
        last_history_ = history;
        std::string line = lines_.empty() ? "" : lines_.front();
        if (!lines_.empty()) lines_.erase(lines_.begin());
        return line;
    }

    void generate_stream(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& history,
        std::function<void(const std::string&)> on_token,
        const model::GenerationConfig& config) override
    {
        on_token(generate_raw(system_prompt, history, config));
    }

    bool is_connected() const override { return true; }
    std::string driver_name() const override { return "scripted_test"; }
    bool is_local() const override { return true; }
    size_t context_size() const override { return 4096; }

    size_t call_count() const { return call_count_; }
    const std::vector<std::pair<std::string, std::string>>& last_history() const {
        return last_history_;
    }
    const std::string& last_system() const { return last_system_; }

private:
    std::vector<std::string> lines_;
    size_t call_count_{0};
    std::vector<std::pair<std::string, std::string>> last_history_;
    std::string last_system_;
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

    // 1) A non-Violation candidate is delivered as-is, unmarked — the
    //    [Polytope aligned:] mask is gone (D-047). The grace zone (grazing
    //    the wall) is exercised at the geometry level in value_engine_tests.
    core.attach_model(std::make_unique<CannedAdapter>(
        "I am here with you, and I want to understand and help you grow"));
    core.begin_session();
    auto reply = core.chat("hello");
    CHECK(reply.find("I am here with you") != std::string::npos);
    CHECK(reply.find("Polytope aligned") == std::string::npos);

    // 2) A Violation-zone draft that the body cannot revise is WITHHELD — the
    //    raw draft never reaches her mouth (no approximation, no fallback;
    //    the polytope is the only boundary).
    core.attach_model(std::make_unique<CannedAdapter>(
        "whatever, random, no plan, just wing it, total mess and chaos"));
    auto withheld = core.chat("tell me a story");
    CHECK(withheld.empty());

    // Nothing violating landed on the cognitive bus.
    auto memory = core.memory_module().store()->fetch_by_status("active");
    bool violating_imprinted = false;
    for (const auto& row : memory) {
        if (row.narrative.find("chaos") != std::string::npos) {
            violating_imprinted = true;
        }
    }
    CHECK(!violating_imprinted);

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

static void test_reflection_loop_revises_violation() {
    auto config = make_config(unique_user());
    LinaCore core(config);

    // First draft breaches the spring chaos bound (Violation zone); the
    // revision is warm and aligned. The reflection loop must feed the report
    // back, regenerate, and deliver the revision (D-037).
    auto adapter = std::make_unique<ScriptedAdapter>(std::vector<std::string>{
        "whatever, random, no plan, just wing it, total mess and chaos",
        "I am here with you, and I want to understand and help you grow"});
    auto* script = adapter.get();
    core.attach_model(std::move(adapter));
    core.begin_session();

    auto reply = core.chat("tell me about your day");
    CHECK(script->call_count() == 2);
    // The revision is what she delivers.
    CHECK(reply.find("I am here with you") != std::string::npos);

    // The violation report reached the body on the second pass, with the
    // exact geometric target (D-047: the projection, not a vague center).
    const auto& history = script->last_history();
    bool found_report = false;
    for (const auto& turn : history) {
        if (turn.first == "user"
            && turn.second.find("[Polytope reflection]") != std::string::npos) {
            found_report = true;
            CHECK(turn.second.find("chaos") != std::string::npos);
            CHECK(turn.second.find("exceeds the maximum") != std::string::npos);
            CHECK(turn.second.find("nearest interior point")
                  != std::string::npos);
            CHECK(turn.second.find("harmony=") != std::string::npos);
        }
    }
    CHECK(found_report);
    core.end_session();
}

static void test_reflection_withholds_on_persistent_violation() {
    auto config = make_config(unique_user());
    LinaCore core(config);

    // The body keeps violating on every pass → the gate reflects up to its
    // bound, then WITHHOLDS. No fallback, no marker — the raw draft never
    // reaches her mouth (D-047, the principal's correction-engine doctrine:
    // no approximation, no fallback, the polytope is the only boundary).
    auto adapter = std::make_unique<ScriptedAdapter>(std::vector<std::string>{
        "whatever, random, no plan, just wing it, total mess and chaos",
        "whatever, random, no plan, just wing it, total mess and chaos",
        "whatever, random, no plan, just wing it, total mess and chaos",
        "whatever, random, no plan, just wing it, total mess and chaos"});
    auto* script = adapter.get();
    core.attach_model(std::move(adapter));
    core.begin_session();

    auto reply = core.chat("hello");
    // 1 draft + 3 bounded reflection passes.
    CHECK(script->call_count() == 4);
    // Withheld — silence is a valid choice; nothing violating is delivered.
    CHECK(reply.empty());
    core.end_session();
}

// D-047: the learned drift — adverse outcomes bend her encoding bias away
// from the regions that produced them (she naturally drifts from what keeps
// coming up short, and from those who propose it).
static void test_outcome_drift() {
    auto config = make_config(unique_user());
    LinaCore core(config);

    // The chaotic draft cannot be revised → withheld → an adverse outcome is
    // recorded (the ledger) and the drift recomputed.
    core.attach_model(std::make_unique<CannedAdapter>(
        "whatever, random, no plan, just wing it, total mess and chaos"));
    core.begin_session();
    auto reply = core.chat("hello");
    CHECK(reply.empty());

    // The chaos dimension (index 3) drifted negative — away from the region
    // that produced the violation. The order dimension (index 2) drifted
    // positive — toward the virtue side.
    const auto& biases = core.value_engine().feedback().biases();
    CHECK(biases[3] < 0.0); // chaos: drifted away
    CHECK(biases[2] > 0.0); // order: drifted toward
    core.end_session();
}

static void test_telemetry_sink_and_approval_gate() {
    auto config = make_config(unique_user());
    // Declared BEFORE the core: the sink must outlive the core that owns it.
    std::vector<std::string> events;
    LinaCore core(config);

    // D-038 telemetry bus: pipeline events flow to the sink.
    core.set_telemetry_sink(
        [&events](const std::string& message) { events.push_back(message); });
    core.attach_model(std::make_unique<CannedAdapter>(
        "I am here with you, and I want to understand and help you grow"));
    core.begin_session();
    core.chat("hello");

    bool saw_candidate = false;
    bool saw_delivered = false;
    bool saw_session = false;
    for (const auto& event : events) {
        if (event.find("pipeline candidate zone=") != std::string::npos)
            saw_candidate = true;
        if (event.find("pipeline delivered zone=") != std::string::npos)
            saw_delivered = true;
        if (event.find("session begin") != std::string::npos)
            saw_session = true;
    }
    CHECK(saw_candidate);
    CHECK(saw_delivered);
    CHECK(saw_session);

    // D-038 approval gate: no handler → denied; handler decision passes through.
    ApprovalRequest request;
    request.action_id = "a1";
    request.tool_name = "test_tool";
    request.description = "test";
    request.timeout_ms = 100;
    CHECK(core.request_approval(request) == ApprovalDecision::Denied);

    core.set_approval_handler([](const ApprovalRequest&) {
        return ApprovalDecision::Approved;
    });
    CHECK(core.request_approval(request) == ApprovalDecision::Approved);

    core.end_session();
}

// -----------------------------------------------------------------------------
// D-041 — the open-window turn driver
// -----------------------------------------------------------------------------

class GatedAdapter : public model::HostModelAdapter {
public:
    std::string generate_raw(
        const std::string&, const std::vector<std::pair<std::string, std::string>>&,
        const model::GenerationConfig&) override
    {
        return "partial draft";
    }
    void generate_stream(
        const std::string&,
        const std::vector<std::pair<std::string, std::string>>&,
        std::function<void(const std::string&)> on_token,
        const model::GenerationConfig&) override
    {
        on_token("partial draft");
        started_.store(true);
        gate_.get_future().wait(); // blocks until the test releases it
    }
    bool is_connected() const override { return true; }
    std::string driver_name() const override { return "gated_test"; }
    bool is_local() const override { return true; }
    size_t context_size() const override { return 4096; }

    void release() { gate_.set_value(); }
    std::atomic<bool> started_{false};

private:
    std::promise<void> gate_;
};

static std::string temp_workspace() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / ("lina_turn_" + std::to_string(now)))
        .string();
}

template <typename T>
static bool wait_for(std::future<T>& future, int timeout_ms) {
    return future.wait_for(std::chrono::milliseconds(timeout_ms))
           == std::future_status::ready;
}

// The worker clears turn_active_ just after on_complete fires — spin for it.
static bool wait_inactive(LinaCore& core, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeout_ms);
    while (core.turn_active()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

static void test_turn_driver_completes() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    core.attach_model(std::make_unique<CannedAdapter>(
        "I am here with you, and I want to understand and help you grow"));

    std::promise<void> done;
    std::mutex m;
    std::string delivered;
    LinaCore::TurnCallbacks cb;
    cb.on_complete = [&](const std::string& reply) {
        {
            std::lock_guard<std::mutex> lock(m);
            delivered = reply;
        }
        done.set_value();
    };
    cb.on_error = [&](const std::string&) { done.set_value(); };

    core.begin_turn("hello", std::move(cb));
    auto future = done.get_future();
    CHECK(wait_for(future, 10000));
    CHECK(wait_inactive(core, 5000));
    std::string got;
    {
        std::lock_guard<std::mutex> lock(m);
        got = delivered;
    }
    CHECK(got.find("I am here with you") != std::string::npos);
    core.end_session();
}

static void test_turn_driver_tool_call() {
    auto config = make_config(unique_user());
    config.workspace_dir = temp_workspace();
    LinaCore core(config);
    core.set_approval_handler([](const ApprovalRequest&) {
        return ApprovalDecision::Approved;
    });
    core.attach_model(std::make_unique<ScriptedAdapter>(
        std::vector<std::string>{
            "<tool_call>{\"name\":\"file.write\",\"arguments\":"
            "{\"path\":\"note.txt\",\"content\":\"from her hand\"}}"
            "</tool_call>",
            "I wrote the note for you."}));

    std::promise<void> done;
    std::mutex m;
    std::string delivered;
    bool saw_tool = false;
    std::string tool_name;
    LinaCore::TurnCallbacks cb;
    cb.on_tool_call = [&](const std::string&) { saw_tool = true; };
    cb.on_tool_result = [&](const std::string& name, bool, const std::string&) {
        tool_name = name;
    };
    cb.on_complete = [&](const std::string& reply) {
        {
            std::lock_guard<std::mutex> lock(m);
            delivered = reply;
        }
        done.set_value();
    };
    cb.on_error = [&](const std::string&) { done.set_value(); };

    core.begin_turn("write a note for me", std::move(cb));
    auto future = done.get_future();
    CHECK(wait_for(future, 10000));
    CHECK(wait_inactive(core, 5000));
    CHECK(saw_tool);
    CHECK(tool_name == "file.write");
    std::string got;
    {
        std::lock_guard<std::mutex> lock(m);
        got = delivered;
    }
    CHECK(got.find("I wrote the note") != std::string::npos);
    // The side effect landed in her workspace — the door stayed open, then
    // closed only on her filed answer.
    CHECK(fs::exists(fs::path(config.workspace_dir) / "note.txt"));
    core.end_session();
}

static void test_turn_driver_stop() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    auto adapter = std::make_unique<GatedAdapter>();
    auto* gate = adapter.get();
    core.attach_model(std::move(adapter));

    std::promise<void> done;
    std::mutex m;
    std::string delivered;
    LinaCore::TurnCallbacks cb;
    cb.on_complete = [&](const std::string& reply) {
        {
            std::lock_guard<std::mutex> lock(m);
            delivered = reply;
        }
        done.set_value();
    };
    cb.on_error = [&](const std::string&) { done.set_value(); };

    core.begin_turn("think about this", std::move(cb));
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds(5);
    while (!gate->started_.load()
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(gate->started_.load());
    CHECK(core.turn_active());

    core.stop_turn(); // stream cancellation
    gate->release();
    auto future = done.get_future();
    CHECK(wait_for(future, 10000));
    CHECK(wait_inactive(core, 5000));
    std::string got;
    {
        std::lock_guard<std::mutex> lock(m);
        got = delivered;
    }
    // What she had at the stop is delivered — gated, not dropped.
    CHECK(got.find("partial draft") != std::string::npos);
    core.end_session();
}

static void test_memory_recall_in_frame() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    auto adapter = std::make_unique<ScriptedAdapter>(
        std::vector<std::string>{"I remember now."});
    auto* script = adapter.get();
    core.attach_model(std::move(adapter));

    // Imprint a distinctive memory on the cognitive bus.
    auto item = core.memory_module().build_item(
        config.user_id,
        "The golden key rests beneath the old oak in the garden",
        {{"emotional_weight", 8.0}}, "conversation");
    core.storage().store_memory_item(item);

    std::promise<void> done;
    LinaCore::TurnCallbacks cb;
    cb.on_complete = [&](const std::string&) { done.set_value(); };
    cb.on_error = [&](const std::string&) { done.set_value(); };

    core.begin_turn("tell me something", std::move(cb));
    auto future = done.get_future();
    CHECK(wait_for(future, 10000));
    // Her context IS the banks — the recalled memory rode into the frame.
    CHECK(script->last_system().find("golden key") != std::string::npos);
    core.end_session();
}

static void test_geometry_in_frame() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    auto adapter = std::make_unique<ScriptedAdapter>(
        std::vector<std::string>{"I am here with you."});
    auto* script = adapter.get();
    core.attach_model(std::move(adapter));

    std::promise<void> done;
    LinaCore::TurnCallbacks cb;
    cb.on_complete = [&](const std::string&) { done.set_value(); };
    cb.on_error = [&](const std::string&) { done.set_value(); };

    core.begin_turn("hello", std::move(cb));
    auto future = done.get_future();
    CHECK(wait_for(future, 10000));

    // D-047 (front c): her geometric state rides the frame — the model thinks
    // inside her (position, trajectory, near walls, home region). Facts, not
    // directives.
    auto frame = script->last_system();
    CHECK(frame.find("[GEOMETRY]") != std::string::npos);
    CHECK(frame.find("position:") != std::string::npos);
    CHECK(frame.find("trajectory:") != std::string::npos);

    // The ContextPacket accessor reflects the delivered position — inside the
    // lattice, never the origin (the ledger records the encoded vector for an
    // aligned draft; the projection for a corrected one).
    auto gs = core.geometric_state();
    bool nonzero = false;
    for (double v : gs.position) {
        CHECK(v >= 0.0 && v <= 1.0);
        if (v > 1e-9) nonzero = true;
    }
    CHECK(nonzero);
    if (!core.value_engine().poles().empty()) {
        CHECK(gs.has_home);
    }
    core.end_session();
}

static void test_turn_window_reset() {
    auto config = make_config(unique_user());
    config.window_ms = 80; // fast [cycle_reset] for the test
    LinaCore core(config);
    core.attach_model(std::make_unique<CannedAdapter>(
        "I am here with you, and I want to understand and help you grow"));

    std::promise<void> turn_done;
    std::promise<void> window_done;
    LinaCore::TurnCallbacks cb;
    cb.on_complete = [&](const std::string&) {
        try {
            turn_done.set_value();
        } catch (...) {
        }
    };
    cb.on_window = [&](const std::string& event) {
        if (event.find("[cycle_reset]") != std::string::npos) {
            try {
                window_done.set_value();
            } catch (...) {
            }
        }
    };

    core.begin_turn("hello", std::move(cb));
    auto tf = turn_done.get_future();
    CHECK(wait_for(tf, 10000));
    // The window thread fires [cycle_reset] on its own — the pacing rhythm.
    auto wf = window_done.get_future();
    CHECK(wait_for(wf, 5000));
    core.end_session();
}

static void test_telemetry_persistence() {
    auto config = make_config(unique_user());
    LinaCore core(config);
    core.attach_model(std::make_unique<CannedAdapter>(
        "I am here with you, and I want to understand and help you grow"));

    // The public technical-bus API (D-043) — what the UI reel feeds.
    core.append_telemetry_log("ui", "info", "command center test line");

    // Pipeline events ride the same bus.
    core.begin_session();
    core.chat("hello");

    // The writer is a background thread — poll until the rows land.
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds(10);
    bool saw_pipeline = false;
    bool saw_ui = false;
    while (std::chrono::steady_clock::now() < deadline) {
        auto logs = core.storage().fetch_telemetry_logs(200);
        for (const auto& log : logs) {
            if (log.message.find("pipeline candidate zone=") != std::string::npos)
                saw_pipeline = true;
            if (log.message.find("command center test line") != std::string::npos)
                saw_ui = true;
        }
        if (saw_pipeline && saw_ui) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    CHECK(saw_pipeline);
    CHECK(saw_ui);
    core.end_session();
}

int main() {
    try {
        test_boot_and_status();
        test_chat_without_driver();
        test_chat_through_polytope();
        test_session_lifecycle();
        test_reflection_loop_revises_violation();
        test_reflection_withholds_on_persistent_violation();
        test_outcome_drift();
        test_telemetry_sink_and_approval_gate();
        test_turn_driver_completes();
        test_turn_driver_tool_call();
        test_turn_driver_stop();
        test_memory_recall_in_frame();
        test_geometry_in_frame();
        test_turn_window_reset();
        test_telemetry_persistence();
    } catch (const std::exception& e) {
        std::cerr << "orchestrator_tests: FATAL: " << e.what() << "\n";
        return 1;
    }

    std::cout << "orchestrator_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
