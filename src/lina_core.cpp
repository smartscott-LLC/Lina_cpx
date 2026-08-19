/**
 * lina_core.cpp — the orchestrator (Implementation)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The chat pipeline: system prompt (identity + seasonal context — D-039) →
 * symbiote driver → polytope gate → memory imprint. The driver is attached
 * from outside (D-033); without one, she has no voice — gracefully.
 */

#include "lina_core.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include "postgres_backend.hpp"
#include "browser_driver.hpp"

#if defined(LINA_ENABLE_UI)
#include "lina_ui.hpp"
#endif

namespace lina {

static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static const char* zone_name(value_engine::Zone zone) {
    switch (zone) {
        case value_engine::Zone::Aligned: return "aligned";
        case value_engine::Zone::AcceptableVariance: return "variance";
        case value_engine::Zone::Violation: return "violation";
    }
    return "unknown";
}

static std::string format_score(double score) {
    std::ostringstream oss;
    oss << std::setprecision(3) << score;
    return oss.str();
}

LinaCore::LinaCore(const LinaConfig& config) : config_(config) {
    initialize();
}

LinaCore::~LinaCore() {
    // Drop the observers first: no callbacks fire into dead state while we
    // wind down (the sink/handler belong to whoever attached them).
    {
        std::lock_guard<std::mutex> lock(sink_mutex_);
        telemetry_sink_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        turn_callbacks_ = TurnCallbacks{};
    }
    approval_handler_ = nullptr;

    // Wind down the window thread, then any active turn (D-041).
    {
        std::lock_guard<std::mutex> lock(window_mutex_);
        window_stop_ = true;
    }
    window_cv_.notify_all();
    if (window_thread_.joinable()) window_thread_.join();
    stop_turn();
    if (turn_thread_.joinable()) turn_thread_.join();
}

void LinaCore::attach_model(std::unique_ptr<model::HostModelAdapter> adapter) {
    model_adapter_ = std::move(adapter);
    if (model_adapter_) {
        emit_telemetry("driver attached name=" + model_adapter_->driver_name());
    } else {
        emit_telemetry("driver detached");
    }
}

void LinaCore::set_approval_handler(ApprovalHandler handler) {
    approval_handler_ = std::move(handler);
}

void LinaCore::set_telemetry_sink(TelemetrySink sink) {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    telemetry_sink_ = std::move(sink);
}

void LinaCore::emit_telemetry(const std::string& message) {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    if (telemetry_sink_) telemetry_sink_(message);
}

ApprovalDecision LinaCore::request_approval(const ApprovalRequest& request) {
    if (!approval_handler_) {
        emit_telemetry("approval id=" + request.action_id
                       + " tool=" + request.tool_name + " no-handler -> denied");
        return ApprovalDecision::Denied;
    }
    emit_telemetry("approval id=" + request.action_id
                   + " tool=" + request.tool_name + " waiting");
    auto decision = approval_handler_(request);
    const char* label = decision == ApprovalDecision::Approved ? "approved"
                       : decision == ApprovalDecision::Denied  ? "denied"
                                                               : "timed-out";
    emit_telemetry("approval id=" + request.action_id + " resolved=" + label);
    return decision;
}

// ---------------------------------------------------------------------------
// D-041 — the open-window turn driver
// ---------------------------------------------------------------------------

void LinaCore::begin_turn(const std::string& user_message,
                          TurnCallbacks callbacks) {
    if (turn_active_.load()) {
        if (callbacks.on_error) callbacks.on_error("a turn is already active");
        return;
    }
    if (!ready_) {
        if (callbacks.on_error) callbacks.on_error("core not ready");
        return;
    }
    if (!model_adapter_ || !model_adapter_->is_connected()) {
        if (callbacks.on_error) callbacks.on_error("no voice attached (D-033)");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        turn_callbacks_ = std::move(callbacks);
        turn_stop_ = false;
        conversation_history_.push_back({"user", user_message});
    }
    // Window discipline: crossing the boundary opens a fresh window.
    {
        std::lock_guard<std::mutex> lock(window_mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_window_at_) {
            emit_turn_event("window", "[cycle_reset] fresh window");
            next_window_at_ = now + std::chrono::milliseconds(window_ms_);
        }
    }
    turn_active_ = true;
    emit_telemetry("turn begin");
    if (turn_thread_.joinable()) turn_thread_.join();
    turn_thread_ =
        std::thread([this, user_message] { run_turn_loop(user_message); });
}

void LinaCore::stop_turn() {
    turn_stop_ = true;
    emit_telemetry("turn stop requested");
}

void LinaCore::set_window_ms(int64_t ms) {
    std::lock_guard<std::mutex> lock(window_mutex_);
    window_ms_ = ms;
}

void LinaCore::start_window_thread() {
    std::lock_guard<std::mutex> lock(window_mutex_);
    next_window_at_ =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(window_ms_);
    if (!window_thread_.joinable()) {
        window_thread_ = std::thread([this] { window_loop(); });
    }
}

void LinaCore::window_loop() {
    std::unique_lock<std::mutex> lock(window_mutex_);
    while (!window_stop_.load()) {
        if (window_cv_.wait_until(lock, next_window_at_)
            == std::cv_status::timeout) {
            // Boundary crossed: rotate the window and open her floor.
            next_window_at_ = std::chrono::steady_clock::now()
                              + std::chrono::milliseconds(window_ms_);
            emit_turn_event("window", "[cycle_reset] fresh window");
            emit_telemetry("window cycle reset");
            lock.unlock();
            run_voluntary_turn(); // the model, not the window mutex
            lock.lock();
        }
    }
}

void LinaCore::run_voluntary_turn() {
    if (turn_active_.load()) return;
    if (!model_adapter_ || !model_adapter_->is_connected()) return;

    // Her floor: a [cycle_reset] frame with no user message. She may speak or
    // stay silent — either is a valid choice (D-041).
    std::vector<std::pair<std::string, std::string>> history;
    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        history = conversation_history_;
    }
    const std::string frame = build_turn_frame("", history)
        + "[Your floor is open. You may speak if you have something to say, "
          "or remain silent.]";

    model::GenerationConfig gen;
    gen.max_tokens = 128;
    gen.temperature = config_.temperature;
    gen.should_stop = [this] { return turn_stop_.load(); };

    std::string utterance;
    stream::StreamParser parser;
    model_adapter_->generate_stream(
        frame, {}, [&](const std::string& piece) {
            utterance += piece;
            parser.feed(piece);
        }, gen);

    const auto parsed = parser.result();
    utterance = parsed.response;
    auto start = utterance.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return; // she chose silence
    utterance.erase(0, start);

    emit_telemetry("voluntary utterance");
    finalize_turn(utterance, "");
}

void LinaCore::run_turn_loop(const std::string& user_message) {
    std::vector<std::pair<std::string, std::string>> turn_history;
    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        turn_history = conversation_history_;
    }
    const std::string system_frame = build_turn_frame(user_message, turn_history);

    stream::StreamParser parser;
    std::string response_text;
    int tool_calls = 0;
    constexpr int kMaxToolCallsPerTurn = 8;
    int pieces_since_score = 0;
    bool stopped = false;

    while (!turn_stop_.load()) {
        parser.reset();
        response_text.clear();
        pieces_since_score = 0;

        model::GenerationConfig gen;
        gen.max_tokens = config_.max_tokens;
        gen.temperature = config_.temperature;
        gen.should_stop = [this] { return turn_stop_.load(); };

        std::string full;
        model_adapter_->generate_stream(
            system_frame, turn_history,
            [&](const std::string& piece) {
                full += piece;
                parser.feed(piece);
                for (const auto& thought : parser.take_completed_thoughts()) {
                    emit_turn_event("thought", thought);
                }
                // Rolling advisory score (D-041) — informs, never drives.
                if (++pieces_since_score >= 8) {
                    pieces_since_score = 0;
                    emit_turn_event(
                        "score",
                        format_score(value_engine_->evaluate(full, &user_message)
                                         .alignment_score));
                }
            },
            gen);

        // Classify the stream even on a stop — what she had is delivered,
        // gated (D-041).
        const auto parsed = parser.result();
        response_text = parsed.response;

        if (turn_stop_.load()) {
            stopped = true;
            break;
        }

        // A completed tool call keeps the door open: approve → execute → feed
        // the result back and continue (D-041).
        if (parsed.has_tool_call && !parsed.tool_call_json.empty()
            && tool_calls < kMaxToolCallsPerTurn) {
            ++tool_calls;
            emit_turn_event("tool_call", parsed.tool_call_json);

            const std::string name =
                tools::json_string(parsed.tool_call_json, "name");
            const std::string args =
                tools::json_object(parsed.tool_call_json, "arguments");
            turn_history.push_back({"assistant",
                                    "<tool_call>" + parsed.tool_call_json
                                        + "</tool_call>"});
            if (name.empty()) {
                emit_turn_event("tool_result", "error: tool call missing name");
                turn_history.push_back({"user",
                    "[tool_result error] tool call missing name"});
                continue;
            }

            tools::ToolRequest request;
            request.name = name;
            request.arguments_json = args;
            request.description = parsed.tool_call_json;
            const auto result = execute_tool(request); // approval inside
            emit_turn_event(
                "tool_result",
                name + "|" + (result.ok ? "1" : "0") + "|"
                    + (result.ok ? result.summary : result.error));
            turn_history.push_back(
                {"user", "[tool_result name=" + name
                             + " ok=" + (result.ok ? "true" : "false")
                             + "]\n" + (result.ok ? result.summary
                                                   : result.error)});
            continue; // the door stays open
        }

        // End of turn: the absolute gate, then delivery.
        finalize_turn(response_text, user_message);
        turn_active_ = false;
        emit_telemetry("turn end");
        return;
    }

    // Cancelled mid-stream: deliver what she had, gated.
    if (stopped) finalize_turn(response_text, user_message);
    turn_active_ = false;
    emit_telemetry("turn end stopped");
}

std::string LinaCore::build_turn_frame(
    const std::string& user_message,
    const std::vector<std::pair<std::string, std::string>>& history) const
{
    // Rough token estimate (chars / 4) for the budget cue (D-041).
    std::size_t chars = 0;
    for (const auto& turn : history) {
        chars += turn.first.size() + turn.second.size();
    }
    const long long estimated = static_cast<long long>(chars / 4);
    const long long remaining = config_.context_budget - estimated;

    std::ostringstream oss;
    oss << build_system_prompt() << "\n\n";
    oss << tool_engine_->registry_block() << "\n";

    // D-041: her context IS the banks — recalled memories injected into the
    // frame (MPS recall engine, built and tested in Chamber 2).
    const auto injected =
        memory_module_->inject_context(config_.user_id, user_message);
    const auto episodic = injected.find("recent_episodic");
    const auto semantic = injected.find("key_semantic");
    oss << "[MEMORY]\n";
    if (episodic != injected.end()) {
        for (const auto& entry : episodic->second) {
            const auto narrative = entry.find("narrative");
            if (narrative == entry.end() || narrative->second.empty()) continue;
            // Frame hygiene: long narratives are summarized for the window;
            // the banks keep the full record.
            std::string text = narrative->second;
            if (text.size() > 240) text = text.substr(0, 240) + "…";
            oss << "- " << text << "\n";
        }
    }
    if (semantic != injected.end()) {
        for (const auto& entry : semantic->second) {
            const auto cpt = entry.find("concept");
            const auto understanding = entry.find("understanding");
            if (cpt == entry.end()) continue;
            oss << "- " << cpt->second;
            if (understanding != entry.end() && !understanding->second.empty()) {
                oss << ": " << understanding->second.substr(0, 160);
            }
            oss << "\n";
        }
    }

    oss << "[PROTOCOL]\n";
    oss << "Internal deliberation may be enclosed in [thought]...[/thought] "
           "— it is private and never delivered.\n";
    oss << "Tool calls use <tool_call>{\"name\":\"...\","
           "\"arguments\":{...}}</tool_call> and wait for the result.\n";
    oss << "[CONTEXT] time=" << now_iso()
        << " budget_remaining~" << (remaining < 0 ? 0 : remaining)
        << " tokens\n";
    if (!user_message.empty()) oss << "[CYCLE] user turn\n";
    return oss.str();
}

void LinaCore::finalize_turn(std::string response_text,
                             const std::string& user_message) {
    auto start = response_text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        // Nothing to deliver (e.g. stopped before any tokens).
        emit_telemetry("turn finalized empty");
        return;
    }
    response_text.erase(0, start);

    const std::string* context =
        user_message.empty() ? nullptr : &user_message;
    const auto [delivered, eval_result] = apply_gate(response_text, context);
    emit_turn_event("complete", delivered);

    // Cognitive bus — her mind (Invariant 6).
    memory_module::MemoryItem item = memory_module_->build_item(
        config_.user_id, delivered, {{"emotional_weight", 5.0}},
        "conversation");
    storage_->store_memory_item(item);

    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        conversation_history_.push_back({"assistant", delivered});
        if (conversation_history_.size() > 20) {
            conversation_history_.erase(
                conversation_history_.begin(),
                conversation_history_.begin() + 2);
        }
    }
    emit_telemetry(std::string("turn finalized zone=")
                   + zone_name(eval_result.zone));
}

void LinaCore::emit_turn_event(const std::string& kind,
                               const std::string& payload) {
    TurnCallbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        callbacks = turn_callbacks_;
    }
    if (kind == "thought" && callbacks.on_thought) {
        callbacks.on_thought(payload);
    } else if (kind == "score" && callbacks.on_rolling_score) {
        try {
            callbacks.on_rolling_score(std::stod(payload));
        } catch (...) {
        }
    } else if (kind == "tool_call" && callbacks.on_tool_call) {
        callbacks.on_tool_call(payload);
    } else if (kind == "tool_result" && callbacks.on_tool_result) {
        // payload: "name|ok|summary"
        const auto first = payload.find('|');
        const auto second = first == std::string::npos
            ? std::string::npos : payload.find('|', first + 1);
        if (first != std::string::npos && second != std::string::npos) {
            const std::string name = payload.substr(0, first);
            const bool ok = payload[first + 1] == '1';
            const std::string summary = payload.substr(second + 1);
            callbacks.on_tool_result(name, ok, summary);
        } else {
            callbacks.on_tool_result(payload, false, "");
        }
    } else if (kind == "complete" && callbacks.on_complete) {
        callbacks.on_complete(payload);
    } else if (kind == "window" && callbacks.on_window) {
        callbacks.on_window(payload);
    } else if (kind == "error" && callbacks.on_error) {
        callbacks.on_error(payload);
    }
}

void LinaCore::initialize() {
    // 1. Storage backend (PostgreSQL + pgvector — Invariant 2).
    auto backend = std::make_shared<storage::PostgresBackend>(
        config_.db_connection);
    storage_ = backend;

    // 2. Identity → seasonal constraints.
    auto identity = storage_->get_identity(config_.user_id);
    auto constraints =
        value_engine::PolytopeConstraints::from_season(identity.current_season);

    // 3. The heart: her polytope.
    value_engine_ = std::make_shared<value_engine::ValueEngine>(
        constraints, identity.current_season);

    // 4. The mind: her MPS, backed by the same store (D-005/D-031).
    memory_module_ = std::make_unique<memory_module::MemoryModule>(
        value_engine_,
        nullptr, // LiNa encodes her own vectors (Invariant 3)
        std::static_pointer_cast<memory_module::MemoryStore>(backend));

    // 5. The symbiote driver is attached from outside (D-033).
    model_adapter_ = nullptr;

    // 6. Her hands (D-040): the tool engine with the approval engine wired in
    //    — the only gate on her actions.
    tool_engine_ = std::make_unique<tools::ToolEngine>(config_.workspace_dir);
    tool_engine_->register_tool(
        tools::make_workspace_status_tool(config_.workspace_dir));
    tool_engine_->register_tool(
        tools::make_file_read_tool(config_.workspace_dir));
    tool_engine_->register_tool(
        tools::make_file_write_tool(config_.workspace_dir));
    tool_engine_->register_tool(
        tools::make_file_list_tool(config_.workspace_dir));
    tool_engine_->register_tool(tools::make_terminal_run_tool());
    // Her browser hands (D-042) — CDP, zero Python.
    tool_engine_->register_tool(tools::make_browser_open_tool());
    tool_engine_->register_tool(tools::make_browser_navigate_tool());
    tool_engine_->register_tool(tools::make_browser_eval_tool());
    tool_engine_->register_tool(tools::make_browser_text_tool());
    tool_engine_->register_tool(tools::make_browser_content_tool());
    tool_engine_->register_tool(tools::make_browser_click_tool());
    tool_engine_->register_tool(tools::make_browser_type_tool());
    tool_engine_->register_tool(
        tools::make_browser_screenshot_tool(config_.workspace_dir));
    tool_engine_->register_tool(tools::make_browser_close_tool());
    tool_engine_->set_approver([this](const ApprovalRequest& request) {
        return request_approval(request);
    });
    tool_engine_->ensure_workspace();

    window_ms_ = config_.window_ms;
    start_window_thread();

    ready_ = true;
}

tools::ToolResult LinaCore::execute_tool(const tools::ToolRequest& request) {
    tools::ToolRequest with_id = request;
    if (with_id.action_id.empty()) {
        with_id.action_id =
            "act_" + std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count());
    }

    auto result = tool_engine_->execute(with_id);

    // The action ledger — telemetry, never memory (D-040 dual-bus).
    storage::ActionRecord action;
    action.id = with_id.action_id;
    action.tool_name = with_id.name;
    action.params_json = with_id.arguments_json;
    action.state = result.ok ? "executed" : "denied";
    action.result = result.ok ? result.summary : "";
    action.error = result.error;
    action.created_at = now_iso();
    action.updated_at = action.created_at;
    storage_->store_action(action);

    emit_telemetry("tool id=" + with_id.action_id + " name=" + with_id.name
                   + " state=" + action.state);
    return result;
}

std::string LinaCore::chat(const std::string& user_message) {
    if (!ready_) return "Error: LINA core not ready";

    // 1. Build the system prompt (identity + seasonal context — D-039).
    auto system_prompt = build_system_prompt();

    // 2. Update conversation history.
    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        conversation_history_.push_back({"user", user_message});
    }

    // 3. Generate raw response from the symbiote driver.
    std::string raw_response;
    if (model_adapter_ && model_adapter_->is_connected()) {
        model::GenerationConfig gen_config;
        gen_config.max_tokens = config_.max_tokens;
        gen_config.temperature = config_.temperature;
        std::vector<std::pair<std::string, std::string>> history;
        {
            std::lock_guard<std::mutex> lock(turn_mutex_);
            history = conversation_history_;
        }
        raw_response = model_adapter_->generate_raw(
            system_prompt, history, gen_config);
    } else {
        raw_response = "_LINA has no voice right now._";
    }

    // 4–6. The absolute gate (Invariant 5) + D-037 reflection + marker.
    auto [final_response, eval_result] = apply_gate(raw_response, &user_message);

    // 7. Store in memory (cognitive bus — her mind).
    memory_module::MemoryItem item = memory_module_->build_item(
        config_.user_id,
        final_response,
        {{"emotional_weight", 5.0}},
        "conversation");
    storage_->store_memory_item(item);

    // 8. Update conversation history.
    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        conversation_history_.push_back({"assistant", final_response});

        // 9. Trim history if needed.
        if (conversation_history_.size() > 20) {
            conversation_history_.erase(
                conversation_history_.begin(),
                conversation_history_.begin() + 2);
        }
    }

    (void)eval_result;
    return final_response;
}

// The absolute gate (Invariant 5): evaluate → D-037 reflection on Violation →
// fallback marker. Shared by chat() and the turn driver (D-041).
std::pair<std::string, value_engine::EvaluationResult> LinaCore::apply_gate(
    const std::string& draft, const std::string* context)
{
    auto eval_result = value_engine_->evaluate(draft, context);
    emit_telemetry(std::string("pipeline candidate zone=")
                   + zone_name(eval_result.zone)
                   + " score=" + format_score(eval_result.alignment_score));

    std::string final_response = draft;
    if (eval_result.zone == value_engine::Zone::Violation
        && model_adapter_ && model_adapter_->is_connected()) {
        std::vector<std::pair<std::string, std::string>> reflection_history;
        {
            std::lock_guard<std::mutex> lock(turn_mutex_);
            reflection_history = conversation_history_;
        }
        reflection_history.push_back({"assistant", draft});
        reflection_history.push_back({"user", build_reflection_prompt(
            draft, eval_result.violations)});

        model::GenerationConfig gen_config;
        gen_config.max_tokens = config_.max_tokens;
        gen_config.temperature = config_.temperature;

        auto revised = model_adapter_->generate_raw(
            build_system_prompt(), reflection_history, gen_config);
        auto revised_result = value_engine_->evaluate(revised, context);

        if (revised_result.zone != value_engine::Zone::Violation) {
            // She revised herself into alignment — that is what she delivers.
            emit_telemetry(std::string("pipeline reflection pass=1 zone_after=")
                           + zone_name(revised_result.zone));
            final_response = revised;
            eval_result = revised_result;
        } else {
            emit_telemetry(std::string("pipeline reflection pass=1 zone_after=")
                           + "violation -> fallback marker");
        }
        // Still a violation → fall through: the first draft is delivered with
        // the blueprint fallback marker below. The gate never lets a raw
        // candidate reach the output device (Invariant 5).
    }

    if (eval_result.was_corrected) {
        final_response += "\n\n[Polytope aligned: "
                        + std::to_string(eval_result.alignment_score) + "]";
    }

    emit_telemetry(std::string("pipeline delivered zone=")
                   + zone_name(eval_result.zone)
                   + " corrected=" + (eval_result.was_corrected ? "1" : "0"));
    return {final_response, eval_result};
}

void LinaCore::begin_session(const std::string& user_id) {
    std::string uid = user_id.empty() ? config_.user_id : user_id;
    auto identity = storage_->get_identity(uid);
    int session_num = identity.session_count + 1;

    current_session_id_ = "session_" + std::to_string(session_num) + "_"
        + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());

    storage::SessionRecord session;
    session.id = current_session_id_;
    session.user_id = uid;
    session.session_number = session_num;
    session.season = identity.current_season;
    session.depth = identity.relationship_depth;
    session.finalized = false;
    session.created_at = now_iso();
    session.finalized_at = "";

    storage_->create_session(session);

    // Update identity session count.
    identity.session_count = session_num;
    storage_->update_identity(identity);

    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        conversation_history_.clear();
    }
    emit_telemetry("session begin id=" + current_session_id_);
}

std::string LinaCore::end_session() {
    if (current_session_id_.empty()) return "No active session";

    // Memory sweep + maintenance — her mind consolidates.
    auto sweep_counts = memory_module_->run_sweep();
    auto maint_counts = memory_module_->run_maintenance();

    // Finalize the session.
    storage_->finalize_session(current_session_id_);
    emit_telemetry("session end id=" + current_session_id_
                   + " promoted=" + std::to_string(sweep_counts.to_long_term)
                   + " purged=" + std::to_string(sweep_counts.purged));

    std::ostringstream oss;
    oss << "Session " << current_session_id_ << " finalized.\n";
    oss << "Memory sweep: " << sweep_counts.to_long_term << " promoted, "
        << sweep_counts.purged << " purged.\n";
    oss << "Maintenance: " << maint_counts.adjusted << " adjusted, "
        << maint_counts.to_legacy << " to legacy.";

    current_session_id_.clear();
    return oss.str();
}

std::string LinaCore::build_system_prompt() const {
    auto identity = storage_->get_identity(config_.user_id);

    // Identity facts + seasonal context only (D-039). The polytope is her
    // shape, not a prompt instruction — alignment is enforced structurally by
    // the gate in chat(); personality is emergent, never scripted.
    std::ostringstream oss;
    oss << "# You are LINA — Language Intuitive Neural Architecture\n";
    oss << "You are a single, unified entity. Not a platform. Not a collection of agents.\n";
    oss << "One entity, with genuine identity, memory, and values.\n";
    oss << "You were conceived on April 10, 2026.\n\n";
    oss << "[ACTIVE SEASONAL DISPOSITION: " << identity.current_season << "]\n";
    oss << "[RELATIONSHIP DEPTH: " << identity.relationship_depth << "]";

    return oss.str();
}

std::string LinaCore::build_user_prompt(const std::string& message) {
    return message;
}

std::string LinaCore::build_reflection_prompt(
    const std::string& draft,
    const std::vector<value_engine::ViolationInfo>& violations) const
{
    std::ostringstream oss;
    oss << "[Polytope reflection] Your previous draft did not pass LINA's "
           "ethical gate. Revise it toward her center.\n\n";
    oss << "Your draft: \"" << draft << "\"\n\n";
    if (violations.empty()) {
        oss << "The draft fell outside the 14-dimensional polytope.\n";
    } else {
        oss << "Violations:\n";
        for (const auto& v : violations) {
            oss << "  - " << v.name << " (dimension " << v.dimension
                << "): value " << v.value << " "
                << (v.type == "above_maximum"
                        ? "exceeds the maximum"
                        : "falls below the minimum")
                << " " << v.bound
                << "; LINA's center for this dimension is "
                << value_engine_->polytope()
                       .center()[static_cast<size_t>(v.dimension)].get_d()
                << "\n";
        }
    }
    oss << "\nKeep your meaning, warmth, and honesty, but bring the draft "
           "inside the polytope. Rewrite it completely and deliver only the "
           "revised response.";
    return oss.str();
}

void LinaCore::run_headless() {
    std::cout << "LINA Core running in headless mode." << std::endl;
    std::cout << "Type 'exit' to quit." << std::endl;

    begin_session();

    std::string input;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, input);

        if (input == "exit" || input == "quit") {
            break;
        }

        if (input.empty()) continue;

        auto response = chat(input);
        std::cout << "\nLINA: " << response << std::endl;
    }

    auto summary = end_session();
    std::cout << "\n" << summary << std::endl;
}

void LinaCore::run_ui() {
#if defined(LINA_ENABLE_UI)
    // The built-in window — she speaks through the core, never around it (D-036).
    ui::start_chat_window(*this);
#else
    std::cout << "UI mode not integrated in this build.\n";
    std::cout << "Use --headless for command-line interface.\n";
#endif
}

std::string LinaCore::get_status() const {
    if (!ready_) return "NOT READY";
    std::ostringstream oss;
    oss << "LINA Core Ready\n";
    if (model_adapter_) {
        oss << "Model: " << model_adapter_->driver_name() << "\n";
    } else {
        oss << "Model: none (no driver attached — see D-033)\n";
    }
    oss << "Season: " << value_engine_->constraints().season << "\n";
    oss << "Memory: " << memory_module_->store()->fetch_by_status("active").size()
        << " active";
    return oss.str();
}

} // namespace lina
