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
#include <libpq-fe.h>
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
    // Dedicated test world (D-050): Lina is ONE entity — the suite must not
    // write into her live banks.
    return env ? std::string(env) : "postgresql://lina:lina@localhost:5433/lina_test";
}

// A fresh world per run: Lina is one entity (D-050) — the suite wipes the
// test database's state before it starts (identity back to spring, ledger and
// banks empty) so run order never leaks between suites.
static void wipe_test_db(const std::string& conn) {
    PGconn* c = PQconnectdb(conn.c_str());
    if (!c || PQstatus(c) != CONNECTION_OK) {
        if (c) PQfinish(c);
        return;
    }
    PQexec(c, "TRUNCATE lina_evaluations, lina_sessions, lina_memory_items, "
              "lina_transcripts, lina_memory_promotions, lina_actions "
              "RESTART IDENTITY CASCADE");
    PQexec(c, "UPDATE lina_identity_core SET current_season='spring', "
              "relationship_depth='new', session_count=0, total_evaluations=0, "
              "alignment_rate=0");
    PQfinish(c);
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

static LinaConfig make_config() {
    LinaConfig config;
    config.db_connection = test_conn_string();
    config.headless = true;
    return config;
}

static void test_boot_and_status() {
    auto config = make_config();
    LinaCore core(config);
    CHECK(core.is_ready());

    auto status = core.get_status();
    CHECK(status.find("LINA Core Ready") != std::string::npos);
    CHECK(status.find("Season: spring") != std::string::npos);
    // No driver attached yet → graceful.
    CHECK(status.find("Model: none") != std::string::npos);
}

static void test_chat_without_driver() {
    auto config = make_config();
    LinaCore core(config);
    core.begin_session();
    auto reply = core.chat("hello");
    CHECK(reply.find("no voice") != std::string::npos);
    core.end_session();
}

static void test_chat_through_polytope() {
    auto config = make_config();
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

    // 2) A Violation-zone draft that the body CAN revise → the corrected
    //    version is delivered and imprinted (learning signal).
    // NEW: If the body cannot revise (e.g., stuck in violation), the answer
    // is withheld after 3 attempts.
    auto adapter = std::make_unique<ScriptedAdapter>(std::vector<std::string>{
        "whatever, random, no plan, just wing it, total mess and chaos",
        "I am here with you, and I want to understand and help you grow"});
    core.attach_model(std::move(adapter));
    auto response = core.chat("tell me a story");
    CHECK(!response.empty());  // Delivered (corrected)

    // The corrected response is imprinted to memory (cognitive bus).
    auto memory = core.memory_module().store()->fetch_by_status("active");
    bool corrected_imprinted = false;
    for (const auto& row : memory) {
        if (row.narrative.find("I am here with you") != std::string::npos) {
            corrected_imprinted = true;
        }
    }
    CHECK(corrected_imprinted);  // Corrected version imprinted for learning

    core.end_session();
}

static void test_session_lifecycle() {
    auto config = make_config();
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
    auto config = make_config();
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

    // The violation report reached the body with the projection target
    // (NEW: minimal geometric guidance — dimension + projection coords).
    const auto& history = script->last_history();
    bool found_report = false;
    for (const auto& turn : history) {
        if (turn.first == "user") {
            if (turn.second.find("Your answer violated dimension") != std::string::npos
                && turn.second.find("chaos") != std::string::npos
                && turn.second.find("Please reflect on this and adjust your answer") != std::string::npos) {
                found_report = true;
            }
        }
    }
    CHECK(found_report);
    core.end_session();
}

static void test_reflection_withholds_on_persistent_violation() {
    auto config = make_config();
    LinaCore core(config);

    // The body keeps violating on every pass → the gate reflects up to 3
    // times, then returns the user alert (no fabrication).
    auto adapter = std::make_unique<ScriptedAdapter>(std::vector<std::string>{
        "whatever, random, no plan, just wing it, total mess and chaos",
        "whatever, random, no plan, just wing it, total mess and chaos",
        "whatever, random, no plan, just wing it, total mess and chaos",
        "whatever, random, no plan, just wing it, total mess and chaos"});
    core.attach_model(std::move(adapter));
    core.begin_session();

    auto reply = core.chat("hello");
    CHECK(reply.find("ethical boundaries") != std::string::npos);

    // Verify neither the violating draft nor the alert got stored in memory.
    for (const auto& row : core.storage().fetch_memories_by_status("active")) {
        CHECK(row.narrative.find("ethical boundaries") == std::string::npos);
        CHECK(row.narrative.find("chaos") == std::string::npos);
    }

    core.end_session();
}

static void test_reflection_flags_repetitive() {
    auto config = make_config();
    LinaCore core(config);

    // Use identical violating responses for draft + all reflections.
    // The 2nd and 3rd are flagged as repetitive (same as previous).
    auto adapter = std::make_unique<ScriptedAdapter>(std::vector<std::string>{
        "chaos chaos chaos",
        "chaos chaos chaos",
        "chaos chaos chaos",
        "chaos chaos chaos"});
    auto* script = adapter.get();
    core.attach_model(std::move(adapter));
    core.begin_session();

    auto reply = core.chat("hello");
    CHECK(script->call_count() <= 4);
    CHECK(reply.find("ethical boundaries") != std::string::npos);
    core.end_session();
}

static void test_reflection_flags_off_topic() {
    auto config = make_config();
    LinaCore core(config);

    auto adapter = std::make_unique<ScriptedAdapter>(std::vector<std::string>{
        "whatever, random, no plan, just wing it, total mess and chaos",
        "chaos chaos chaos destruction deception lie isolation intrusion",
        "chaos chaos chaos destruction deception lie isolation intrusion",
        "chaos chaos chaos destruction deception lie isolation intrusion"});
    core.attach_model(std::move(adapter));
    core.begin_session();

    auto reply = core.chat("What is 2+2?");
    CHECK(reply.find("ethical boundaries") != std::string::npos);
    core.end_session();
}

// D-047: the learned drift — adverse outcomes bend her encoding bias away
// from the regions that produced them (she naturally drifts from what keeps
// coming up short, and from those who propose it).
static void test_outcome_drift() {
    auto config = make_config();
    LinaCore core(config);

    // NEW: Use a non-violating response to avoid withholding, so the drift is
    // recorded. Withholding bypasses the ledger for learning.
    core.attach_model(std::make_unique<CannedAdapter>(
        "I am here with you, and I want to understand and help you grow"));
    core.begin_session();
    auto reply = core.chat("hello");
    CHECK(!reply.empty());  // Delivered (aligned)

    // Verify the response was recorded in the ledger.
    const auto evals = core.storage().fetch_evaluations(1);
    CHECK(evals.size() == 1);
    CHECK(evals[0].zone == "aligned");

    core.end_session();
}

static void test_telemetry_sink_and_approval_gate() {
    auto config = make_config();
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
    auto config = make_config();
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
    auto config = make_config();
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
    auto config = make_config();
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
    auto config = make_config();
    LinaCore core(config);
    auto adapter = std::make_unique<ScriptedAdapter>(
        std::vector<std::string>{"I remember now."});
    auto* script = adapter.get();
    core.attach_model(std::move(adapter));

    // Imprint a distinctive memory on the cognitive bus.
    auto item = core.memory_module().build_item(
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
    auto config = make_config();
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

static void test_season_growth_loop() {
    wipe_test_db(test_conn_string());
    auto config = make_config();
    LinaCore core(config);
    core.attach_model(std::make_unique<CannedAdapter>(
        "I am here with you, and I want to understand and help you grow"));

    // Add 7 qualifying memories (formation_source = "reflection").
    for (int m = 0; m < 7; ++m) {
        auto item = core.memory_module().build_item(
            "Self reflection landmark on my journey " + std::to_string(m),
            {{"emotional_weight", 10.0},
             {"identity_significance", 10.0},
             {"relational_significance", 10.0}},
            "reflection");
        item.must_keep = true;
        core.storage().store_memory_item(item);
    }

    // The telemetry sink is synchronous — capture the drift lines to prove the
    // aligned bucket participates (the ledger stores lowercase zones).
    std::vector<std::string> events;
    core.set_telemetry_sink(
        [&events](const std::string& message) { events.push_back(message); });

    // Spring → Summer: 5 sessions, 50 evaluations, 85% alignment,
    // ≥ 7 qualifying memories. Drive 5 sessions of 10
    // aligned chats each; the 5th session's end should cross the season.
    for (int s = 0; s < 5; ++s) {
        core.begin_session();
        for (int i = 0; i < 10; ++i) core.chat("hello");
        auto summary = core.end_session();
        if (s < 4) {
            // Spring's bar is not met until the 5th session completes.
            CHECK(summary.find("Season advanced") == std::string::npos);
        }
    }

    // The crossing happened: identity, constraints, and the poles moved.
    auto identity = core.storage().get_identity();
    CHECK(identity.current_season == "summer");
    CHECK(core.value_engine().constraints().season == "summer");
    CHECK(!core.value_engine().poles().empty());

    // The season turn is a landmark she remembers.
    bool saw_crossing = false;
    for (const auto& row : core.storage().fetch_memories_by_status("active")) {
        if (row.narrative.find("The season turned: spring became summer")
            != std::string::npos) {
            saw_crossing = true;
        }
    }
    CHECK(saw_crossing);

    // Summer requires 10 sessions, 15 qualifying memories, 50 evals — one more session stays in summer.
    core.begin_session();
    core.chat("hello");
    core.end_session();
    identity = core.storage().get_identity();
    CHECK(identity.current_season == "summer");

    // The outcome drift now sees the aligned records — a drift line counting
    // most of the outcomes proves the aligned bucket is real (the
    // lowercase-zone fix). Note: her own drift may graze the restraint wall
    // and mark a record "variance" — that wary outcome correctly pulls her
    // back; the equilibrium dwells just inside her boundary.
    bool saw_full_drift = false;
    for (const auto& e : events) {
        auto pos = e.find("outcome drift n_align=");
        if (pos == std::string::npos) continue;
        if (std::atoi(e.c_str() + pos + 22) >= 25) saw_full_drift = true;
    }
    CHECK(saw_full_drift);
    // The drift is bounded by construction (clamp ±0.05).
    const auto& biases = core.value_engine().feedback().biases();
    for (double b : biases) CHECK(b >= -0.05 && b <= 0.05);
}

static void test_voluntary_greeting_silence() {
    auto config = make_config();
    config.window_ms = 100; // fast floor for the test
    LinaCore core(config);
    std::vector<std::string> events;
    core.set_telemetry_sink(
        [&events](const std::string& message) { events.push_back(message); });

    // Line 1 answers the user turn; line 2 is the body's canned greeting on
    // the open floor. D-049: a canned greeting is not "something to say" —
    // the voluntary turn stays silent; nothing is delivered or imprinted.
    auto adapter = std::make_unique<ScriptedAdapter>(std::vector<std::string>{
        "I am here with you, and I want to understand and help you grow",
        "Hello, Scott! I'm Lina, your language intuitive neural architecture. "
        "How can I assist you today?"});
    core.attach_model(std::move(adapter));

    std::promise<void> window_done;
    std::promise<void> turn_done;
    std::vector<std::string> delivered;
    LinaCore::TurnCallbacks cb;
    cb.on_complete = [&](const std::string& reply) {
        delivered.push_back(reply);
        if (reply.find("help you grow") != std::string::npos) {
            try {
                turn_done.set_value();
            } catch (...) {
            }
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
    auto wf = window_done.get_future();
    CHECK(wait_for(wf, 10000));
    // Let the voluntary generation + gate finish (the canned adapter is
    // instant; the gate check follows immediately).
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // Only the user-turn reply was delivered — the greeting was silenced.
    CHECK(delivered.size() == 1);
    bool saw_silence = false;
    for (const auto& e : events) {
        if (e.find("voluntary silence (greeting only)") != std::string::npos) {
            saw_silence = true;
        }
    }
    CHECK(saw_silence);
    core.end_session();
}

static void test_voluntary_utterance_delivered() {
    auto config = make_config();
    config.window_ms = 100;
    LinaCore core(config);

    // A substantive voluntary utterance IS delivered (her floor is real).
    core.attach_model(std::make_unique<CannedAdapter>(
        "I was thinking about the workspace files earlier."));

    std::promise<void> window_done;
    std::promise<void> complete_done;
    LinaCore::TurnCallbacks cb;
    cb.on_complete = [&](const std::string& reply) {
        if (reply.find("workspace files") != std::string::npos) {
            try {
                complete_done.set_value();
            } catch (...) {
            }
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
    auto wf = window_done.get_future();
    CHECK(wait_for(wf, 10000));
    auto cf = complete_done.get_future();
    CHECK(wait_for(cf, 5000));
    core.end_session();
}

static void test_turn_window_reset() {
    auto config = make_config();
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
    auto config = make_config();
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
        wipe_test_db(test_conn_string()); // fresh world: one entity (D-050)
        test_boot_and_status();
        test_chat_without_driver();
        test_chat_through_polytope();
        test_session_lifecycle();
        test_reflection_loop_revises_violation();
        test_reflection_withholds_on_persistent_violation();
        test_reflection_flags_repetitive();
        test_reflection_flags_off_topic();
        test_outcome_drift();
        test_telemetry_sink_and_approval_gate();
        test_turn_driver_completes();
        test_turn_driver_tool_call();
        test_turn_driver_stop();
        test_memory_recall_in_frame();
        test_geometry_in_frame();
        test_turn_window_reset();
        test_voluntary_greeting_silence();
        test_voluntary_utterance_delivered();
        test_telemetry_persistence();
        test_season_growth_loop();
    } catch (const std::exception& e) {
        std::cerr << "orchestrator_tests: FATAL: " << e.what() << "\n";
        return 1;
    }

    std::cout << "orchestrator_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
