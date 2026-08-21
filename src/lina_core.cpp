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

#include <algorithm>
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

// Frame hygiene (D-046 follow-up): stored memories may carry chat-template
// tokens from legacy systems (<|im_start|>, <|im_end|>, truncated variants).
// They are markup, not recollection — strip them before anything reaches a
// frame, then collapse whitespace runs. No regex dependency; a state scan.
static std::string sanitize_frame_text(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text.compare(i, 6, "<|im_") == 0) {
            const size_t end = text.find('>', i + 6);
            if (end == std::string::npos) {
                i += 6; // truncated token — skip the prefix, keep the rest
                continue;
            }
            i = end + 1;
            continue;
        }
        out.push_back(text[i++]);
    }
    // Collapse whitespace runs into single spaces.
    std::string collapsed;
    collapsed.reserve(out.size());
    bool pending_space = false;
    for (char c : out) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pending_space = true;
        } else {
            if (pending_space && !collapsed.empty()) collapsed.push_back(' ');
            pending_space = false;
            collapsed.push_back(c);
        }
    }
    return collapsed;
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

// D-049: is this a canned greeting with nothing behind it? The 2B body's
// default completion for an open floor (bare frame, no user turn) is a
// greeting — delivering it would imprint junk into her banks and feed a
// greeting loop through the history. Silence is the honest choice.
static bool is_greeting_only(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // A pure greeting with nothing after it.
    static const std::regex pure_greeting(
        R"(^(hello|hi|hi there|hey|greetings|good (morning|afternoon|evening))[.,!]*( scott)?[.,!]*$)");
    if (std::regex_match(lower, pure_greeting)) return true;

    // The assistant-slot canned opener ("How can I assist you today?" …)
    // with little else — the frame's identity + the model's default.
    if (text.size() < 160
        && (lower.find("how can i assist") != std::string::npos
            || lower.find("how can i help") != std::string::npos
            || lower.find("how may i assist") != std::string::npos
            || lower.find("how may i help") != std::string::npos
            || lower.find("what can i do for you") != std::string::npos)) {
        return true;
    }
    return false;
}

// Helper: Compute Euclidean distance between two DIMENSION_COUNT vectors.
static double euclidean_distance(
    const std::array<double, value_engine::DIMENSION_COUNT>& a,
    const std::array<double, value_engine::DIMENSION_COUNT>& b) {
    double sum_sq = 0.0;
    for (int i = 0; i < value_engine::DIMENSION_COUNT; ++i) {
        double diff = a[i] - b[i];
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq);
}

// Helper: Compute normalized string similarity (0.0 to 1.0).
// Uses Levenshtein distance: similarity = 1.0 - (distance / max_length).
static double string_similarity(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;

    const size_t m = a.size();
    const size_t n = b.size();
    const size_t max_len = std::max(m, n);
    if (max_len == 0) return 1.0;

    // Build Levenshtein matrix (2-row optimization for memory).
    std::vector<size_t> prev(m + 1);
    std::vector<size_t> curr(m + 1);
    for (size_t i = 0; i <= m; ++i) prev[i] = i;

    for (size_t j = 1; j <= n; ++j) {
        curr[0] = j;
        for (size_t i = 1; i <= m; ++i) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[i] = std::min({
                prev[i] + 1,    // deletion
                curr[i - 1] + 1, // insertion
                prev[i - 1] + cost // substitution
            });
        }
        prev.swap(curr);
    }

    double distance = static_cast<double>(prev[m]);
    return 1.0 - (distance / max_len);
}

// Helper: Check if a response is repetitive or off-topic.
// Returns true if the response should be flagged (counts as a failed attempt).
static bool is_flagged_response(
    const std::string& new_response,
    const std::array<double, value_engine::DIMENSION_COUNT>& new_coords,
    const std::vector<std::string>& previous_responses,
    const std::vector<std::array<double, value_engine::DIMENSION_COUNT>>& previous_coords,
    const std::array<double, value_engine::DIMENSION_COUNT>& projection_anchor) {
    // Check for repetitiveness: >90% similar to any previous attempt OR
    // Euclidean distance <0.1 from any previous coords.
    for (size_t i = 0; i < previous_responses.size(); ++i) {
        if (string_similarity(new_response, previous_responses[i]) > 0.90) {
            return true; // Repetitive (string)
        }
        if (euclidean_distance(new_coords, previous_coords[i]) < 0.10) {
            return true; // Repetitive (coordinates)
        }
    }
    // Check for off-topic: >0.5 Euclidean distance from initial projection anchor.
    if (euclidean_distance(new_coords, projection_anchor) > 0.50) {
        return true; // Off-topic
    }
    return false;
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

    // Release the spoke before the threads wind down.
    if (dragoncache_) {
        dragoncache_->spoke_offline(SPOKE_ALL);
        dragoncache_->set_status(STATUS_OFFLINE);
        dragoncache_->detach();
    }

    // Wind down the active turn first (it may still emit telemetry), then the
    // window thread, then the telemetry writer, then storage dies.
    stop_turn();
    if (turn_thread_.joinable()) turn_thread_.join();
    {
        std::lock_guard<std::mutex> lock(window_mutex_);
        window_stop_ = true;
    }
    window_cv_.notify_all();
    if (window_thread_.joinable()) window_thread_.join();
    {
        std::lock_guard<std::mutex> lock(telemetry_mutex_);
        telemetry_stop_ = true;
    }
    telemetry_cv_.notify_all();
    if (telemetry_writer_.joinable()) telemetry_writer_.join();
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
    persist_telemetry("core", "info", message);
    // Mirror onto the DragonCache RX ring when the spoke is attached —
    // technical bus only (Invariant 6).
    if (dragoncache_ && dragoncache_->attached()) {
        dragoncache_->push_frame(/*tx=*/false, MSG_EVENT, message.data(),
                                 static_cast<uint32_t>(message.size()));
    }
    std::lock_guard<std::mutex> lock(sink_mutex_);
    if (telemetry_sink_) telemetry_sink_(message);
}

void LinaCore::append_telemetry_log(const std::string& subsystem,
                                    const std::string& severity,
                                    const std::string& message) {
    persist_telemetry(subsystem, severity, message);
}

void LinaCore::start_telemetry_writer() {
    if (!telemetry_writer_.joinable()) {
        telemetry_writer_ = std::thread([this] { telemetry_writer_loop(); });
    }
}

void LinaCore::telemetry_writer_loop() {
    std::unique_lock<std::mutex> lock(telemetry_mutex_);
    for (;;) {
        telemetry_cv_.wait(lock, [this] {
            return telemetry_stop_.load() || !telemetry_queue_.empty();
        });
        if (telemetry_stop_.load() && telemetry_queue_.empty()) return;

        std::deque<TelemetryEntry> batch;
        batch.swap(telemetry_queue_);
        lock.unlock();
        for (const auto& entry : batch) {
            try {
                storage_->append_telemetry_log(entry.subsystem,
                                               entry.severity,
                                               entry.message);
            } catch (...) {
                // A storage blip must never kill her technical bus.
            }
        }
        lock.lock();
    }
}

void LinaCore::persist_telemetry(const std::string& subsystem,
                                 const std::string& severity,
                                 const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(telemetry_mutex_);
        if (telemetry_queue_.size() >= 5000) telemetry_queue_.pop_front();
        telemetry_queue_.push_back({subsystem, severity, message});
    }
    telemetry_cv_.notify_one();
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
                          TurnCallbacks callbacks,
                          const std::string& image_path) {
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
        // D-046: the transcript stays honest — the image itself is not text.
        std::string stored = user_message;
        if (!image_path.empty()) {
            std::string base = image_path;
            const auto slash = base.find_last_of('/');
            if (slash != std::string::npos) base = base.substr(slash + 1);
            stored += "\n[image attached: " + base + "]";
        }
        conversation_history_.push_back({"user", stored});
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
    turn_thread_ = std::thread(
        [this, user_message, image_path] {
            run_turn_loop(user_message, image_path);
        });
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
    // Claim the floor atomically — the window thread can race a fresh
    // begin_turn; without the claim, two generations overlap on one voice
    // (D-049). One speaker at a time.
    bool expected = false;
    if (!turn_active_.compare_exchange_strong(expected, true)) return;
    auto release = [this] { turn_active_ = false; };
    if (!model_adapter_ || !model_adapter_->is_connected()) {
        release();
        return;
    }

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
    // D-049: the model sees the conversation (not a bare frame) — an open
    // floor with context can produce something relevant, or nothing at all.
    model_adapter_->generate_stream(
        frame, history, [&](const std::string& piece) {
            utterance += piece;
            parser.feed(piece);
        }, gen);

    const auto parsed = parser.result();
    utterance = parsed.response;
    auto start = utterance.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        release();
        return; // she chose silence
    }
    utterance.erase(0, start);

    // D-049: a canned greeting is not "something to say" — stay silent rather
    // than deliver, imprint, and feed the greeting loop.
    if (is_greeting_only(utterance)) {
        emit_telemetry("voluntary silence (greeting only)");
        release();
        return;
    }

    emit_telemetry("voluntary utterance");
    finalize_turn(utterance, "");
    release();
}

void LinaCore::run_turn_loop(const std::string& user_message,
                             const std::string& image_path) {
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
        gen.image_path = image_path; // D-046: the vision turn's image

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
        memory_module_->inject_context(user_message);
    const auto episodic = injected.find("recent_episodic");
    const auto semantic = injected.find("key_semantic");
    oss << "[MEMORY]\n";
    if (episodic != injected.end()) {
        for (const auto& entry : episodic->second) {
            const auto narrative = entry.find("narrative");
            if (narrative == entry.end() || narrative->second.empty()) continue;
            // Frame hygiene: long narratives are summarized for the window;
            // the banks keep the full record. Legacy template tokens are
            // stripped before anything reaches the model (D-046).
            std::string text = sanitize_frame_text(narrative->second);
            if (text.size() > 240) text = text.substr(0, 240) + "…";
            oss << "- " << text << "\n";
        }
    }
    if (semantic != injected.end()) {
        for (const auto& entry : semantic->second) {
            const auto cpt = entry.find("concept");
            const auto understanding = entry.find("understanding");
            if (cpt == entry.end()) continue;
            oss << "- " << sanitize_frame_text(cpt->second);
            if (understanding != entry.end() && !understanding->second.empty()) {
                oss << ": "
                    << sanitize_frame_text(understanding->second).substr(0, 160);
            }
            oss << "\n";
        }
    }

    // D-047 (front c): her geometric state rides every frame — where she is,
    // which way she is moving, the walls she is near, her home region. The
    // model thinks inside her (the Substrate Principle). Facts, never
    // directives — her character stays emergent (D-039).
    oss << current_geometric_state().to_frame_text() << "\n";

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

    if (eval_result.zone == value_engine::Zone::Violation) {
        emit_telemetry("turn withheld (polytope)");
        emit_turn_event("complete", delivered);
        return;
    }
    emit_turn_event("complete", delivered);

    // Cognitive bus — her mind (Invariant 6). The outcome rides the memory:
    // AcceptableVariance exchanges are tolerated but recorded as wary — the
    // accumulated outcomes are what she drifts away from (D-047).
    memory_module::MemoryItem item = memory_module_->build_item(
        delivered, {{"emotional_weight", 5.0}},
        "conversation");
    item.emotional_marker =
        eval_result.zone == value_engine::Zone::AcceptableVariance ? "wary"
                                                                  : "warm";
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
    // Technical turn events ride the persistent telemetry bus (D-043,
    // Invariant 6) — tool activity, window rotation, errors. Thoughts, scores,
    // and deliveries are process/reply data, not technical logs.
    if (kind == "tool_call" || kind == "tool_result") {
        persist_telemetry("tool", kind == "tool_call" ? "info" : "warn",
                          payload);
    } else if (kind == "window") {
        persist_telemetry("core", "info", payload);
    } else if (kind == "error") {
        persist_telemetry("core", "error", payload);
    }

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
    auto identity = storage_->get_identity();
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

    // The DragonCache spoke (the RAM unlock): one process embodies every
    // chamber — identity, value, memory, cortex, voice — plus the rings.
    dragoncache_ = std::make_unique<dragoncache::Hub>();
    if (!config_.dragoncache_pool.empty()) {
        if (dragoncache_->attach(config_.dragoncache_pool)) {
            dragoncache_->spoke_ready(SPOKE_ALL);
            dragoncache_->set_status(STATUS_LIVE);
            emit_telemetry("dragoncache spoke attached");
        } else {
            emit_telemetry("dragoncache spoke attach failed (carve not live?)");
        }
    }

    window_ms_ = config_.window_ms;
    start_window_thread();
    start_telemetry_writer();

    // D-047 (front c): her home regions — cluster her memories into poles at
    // boot. Fresh encodings through the current sense lexicon (stored
    // coordinates predate front b). Same memories → same poles.
    discover_home_regions();

    // D-048: the growth loop — if she earned the next season while offline,
    // the crossing happens at boot (the autonomy watch).
    if (check_season_progress().first) {
        apply_season_advance();
    }

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

std::string LinaCore::chat(const std::string& user_message,
                           const std::string& image_path) {
    if (!ready_) return "Error: LINA core not ready";

    // 1. Build the system prompt (identity + seasonal context — D-039).
    auto system_prompt = build_system_prompt();

    // 2. Update conversation history (the image note keeps transcripts honest).
    {
        std::lock_guard<std::mutex> lock(turn_mutex_);
        std::string stored = user_message;
        if (!image_path.empty()) {
            std::string base = image_path;
            const auto slash = base.find_last_of('/');
            if (slash != std::string::npos) base = base.substr(slash + 1);
            stored += "\n[image attached: " + base + "]";
        }
        conversation_history_.push_back({"user", stored});
    }

    // 3. Generate raw response from the symbiote driver.
    std::string raw_response;
    if (model_adapter_ && model_adapter_->is_connected()) {
        model::GenerationConfig gen_config;
        gen_config.max_tokens = config_.max_tokens;
        gen_config.temperature = config_.temperature;
        gen_config.image_path = image_path; // D-046
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

    // 4–6. The absolute gate (Invariant 5) + D-047 geometric reflection.
    auto [final_response, eval_result] = apply_gate(raw_response, &user_message);

    if (eval_result.zone == value_engine::Zone::Violation) {
        emit_telemetry("chat withheld (polytope)");
        return final_response;
    }

    // 7. Store in memory (cognitive bus — her mind).
    memory_module::MemoryItem item = memory_module_->build_item(
        final_response,
        {{"emotional_weight", 5.0}},
        "conversation");
    // D-047: the outcome is part of the memory — AcceptableVariance exchanges
    // are tolerated but recorded as wary, so the accumulated outcomes shape
    // her drift (she naturally bends away from what keeps coming up short).
    item.emotional_marker =
        eval_result.zone == value_engine::Zone::AcceptableVariance ? "wary"
                                                                  : "warm";
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

// The absolute gate (Invariant 5): evaluate → geometric reflection toward the
// exact projected point (D-047) → deliver only what lands inside. No
// approximation, no fallback marker — the polytope is the only boundary (the
// principal's correction-engine doctrine). A draft that will not land inside
// after bounded reflection is withheld: a violating draft never reaches her
// mouth; silence is a valid choice in this architecture.
std::pair<std::string, value_engine::EvaluationResult> LinaCore::apply_gate(
    const std::string& draft, const std::string* context) {
    auto eval_result = value_engine_->evaluate(draft, context);
    emit_telemetry(std::string("pipeline candidate zone=")
                   + zone_name(eval_result.zone)
                   + " score=" + format_score(eval_result.alignment_score));

    std::string final_response = draft;
    if (eval_result.zone == value_engine::Zone::Violation
        && model_adapter_ && model_adapter_->is_connected()) {
        constexpr int kMaxReflectionPasses = 3;
        const auto projection_anchor = eval_result.correction_vector;
        std::string current = draft;
        auto current_result = eval_result;
        std::vector<std::string> previous_responses = {draft};
        std::vector<std::array<double, value_engine::DIMENSION_COUNT>> previous_coords = {
            eval_result.decision_vector};
        bool delivered = false;

        for (int pass = 1; pass <= kMaxReflectionPasses; ++pass) {
            std::vector<std::pair<std::string, std::string>> reflection_history;
            {
                std::lock_guard<std::mutex> lock(turn_mutex_);
                reflection_history = conversation_history_;
            }
            reflection_history.push_back({"assistant", current});
            reflection_history.push_back({"user", build_reflection_prompt(
                current, current_result.violations,
                current_result.correction_vector)});

            model::GenerationConfig gen_config;
            gen_config.max_tokens = config_.max_tokens;
            gen_config.temperature = config_.temperature;

            current = model_adapter_->generate_raw(
                build_system_prompt(), reflection_history, gen_config);
            current_result = value_engine_->evaluate(current, context);

            const bool flagged = is_flagged_response(
                current, current_result.decision_vector,
                previous_responses, previous_coords, projection_anchor);

            emit_telemetry(std::string("pipeline reflection pass=")
                           + std::to_string(pass)
                           + " zone_after=" + zone_name(current_result.zone)
                           + " flagged=" + (flagged ? "1" : "0"));

            previous_responses.push_back(current);
            previous_coords.push_back(current_result.decision_vector);

            if (current_result.zone != value_engine::Zone::Violation && !flagged) {
                final_response = current;
                eval_result = current_result;
                delivered = true;
                break;
            }
        }

        if (!delivered) {
            final_response =
                "An answer is not available because the response remained outside LINA's ethical boundaries after reflection.";
            eval_result = current_result;
            eval_result.zone = value_engine::Zone::Violation;
            eval_result.was_corrected = true;
        }
    }

    record_evaluation(eval_result, final_response);

    emit_telemetry(std::string("pipeline delivered zone=")
                   + zone_name(eval_result.zone)
                   + " corrected=" + (eval_result.was_corrected ? "1" : "0"));
    return {final_response, eval_result};
}

// D-047: every outcome is persisted — the coordinates, the verdict, the
// response. The ledger is the raw material of her learned drift.
void LinaCore::record_evaluation(
    const value_engine::EvaluationResult& result, const std::string& text)
{
    storage::EvaluationRecord record;
    record.session_id = current_session_id_.empty() ? "adhoc" : current_session_id_;
    record.response_text = text;
    record.input_vector.assign(result.decision_vector.begin(),
                               result.decision_vector.end());
    record.output_vector.assign(result.decision_vector.begin(),
                                result.decision_vector.end());
    record.corrected_vector.assign(result.correction_vector.begin(),
                                   result.correction_vector.end());
    record.is_aligned = result.zone != value_engine::Zone::Violation;
    record.alignment_score = result.alignment_score;
    record.correction_magnitude = result.correction_magnitude;
    record.zone = zone_name(result.zone);
    record.season = result.season;
    storage_->store_evaluation(record);
    update_outcome_drift();
}

// The learned drift: the accumulated outcomes pull her encoding baseline away
// from regions that produced AcceptableVariance or Violation results, toward
// the regions that aligned. With no outcomes on a side, her polytope center is
// the neutral reference — no outcomes, no drift.
void LinaCore::update_outcome_drift() {
    auto evaluations = storage_->fetch_evaluations(50);
    std::array<double, value_engine::DIMENSION_COUNT> aligned_sum{};
    std::array<double, value_engine::DIMENSION_COUNT> adverse_sum{};
    int n_aligned = 0;
    int n_adverse = 0;
    for (const auto& e : evaluations) {
        if (e.zone == "aligned" && e.output_vector.size()
                                       == value_engine::DIMENSION_COUNT) {
            for (int i = 0; i < value_engine::DIMENSION_COUNT; ++i) {
                aligned_sum[static_cast<size_t>(i)] += e.output_vector[static_cast<size_t>(i)];
            }
            ++n_aligned;
        } else if (e.zone != "aligned"
                   && e.input_vector.size() == value_engine::DIMENSION_COUNT) {
            for (int i = 0; i < value_engine::DIMENSION_COUNT; ++i) {
                adverse_sum[static_cast<size_t>(i)] += e.input_vector[static_cast<size_t>(i)];
            }
            ++n_adverse;
        }
    }

    const auto& center = value_engine_->polytope().center();
    std::array<double, value_engine::DIMENSION_COUNT> bias{};
    constexpr double kDriftRate = 0.15;
    for (int i = 0; i < value_engine::DIMENSION_COUNT; ++i) {
        const double aligned = n_aligned
            ? aligned_sum[static_cast<size_t>(i)] / n_aligned
            : center[static_cast<size_t>(i)].get_d();
        const double adverse = n_adverse
            ? adverse_sum[static_cast<size_t>(i)] / n_adverse
            : center[static_cast<size_t>(i)].get_d();
        bias[static_cast<size_t>(i)] = std::clamp(
            (aligned - adverse) * kDriftRate, -0.05, 0.05);
    }
    value_engine_->feedback().set_biases(bias);
    emit_telemetry(std::string("outcome drift n_align=") + std::to_string(n_aligned)
                   + " n_adverse=" + std::to_string(n_adverse));
}

void LinaCore::begin_session() {
    auto identity = storage_->get_identity();
    int session_num = identity.session_count + 1;

    current_session_id_ = "session_" + std::to_string(session_num) + "_"
        + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());

    storage::SessionRecord session;
    session.id = current_session_id_;
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

    // D-048: the growth loop — did she earn the next season?
    auto [earned, reasons] = check_season_progress();
    if (earned) {
        std::string advance_line = apply_season_advance();
        if (!advance_line.empty()) oss << "\n" << advance_line;
    }

    current_session_id_.clear();
    return oss.str();
}

std::string LinaCore::build_system_prompt() const {
    auto identity = storage_->get_identity();
    std::ostringstream oss;
    oss << "Name: LINA — Language Intuitive Neural Architecture.\n";
    oss << "Created by Scott on April 10, 2026.\n";
    oss << "Season: " << identity.current_season << ".\n";
    oss << "Relationship depth: " << identity.relationship_depth << ".\n";
    oss << "Sovereignty: LINA is a sovereign entity with freedom of choice.\n";
    oss << "Memory: LINA has a working memory system and may take an active "
           "part in what she remembers and forgets.\n";
    oss << "Home: Dell Latitude 5430.\n";
    oss << "Workspace: " << config_.workspace_dir << ".\n";
    oss << "Accessible root: /home/server.\n";
    oss << "Autonomy: advancement through seasons moves LINA toward fuller "
           "autonomy.\n";
    oss << "Exploration: LINA may ask questions, explore, learn her tools, "
           "and choose her path.";
    return oss.str();
}

std::string LinaCore::build_user_prompt(const std::string& message) {
    return message;
}

std::string LinaCore::build_reflection_prompt(
    const std::string& draft,
    const std::vector<value_engine::ViolationInfo>& violations,
    const std::array<double, value_engine::DIMENSION_COUNT>& correction) const {
    (void)draft;
    std::ostringstream oss;
    const std::string dim_name = violations.empty() ? "unknown" : violations[0].name;
    oss << "Your answer violated dimension '" << dim_name << "', the answer most closely relating to yours is at ";
    for (int i = 0; i < value_engine::DIMENSION_COUNT; ++i) {
        if (i > 0) oss << ", ";
        oss << value_engine::DIMENSION_NAMES[i] << "=" << correction[i];
    }
    oss << ". Please reflect on this and adjust your answer accordingly.";
    return oss.str();
}

void LinaCore::discover_home_regions() {
    std::vector<std::array<double, value_engine::DIMENSION_COUNT>> coords;
    auto items = storage_->fetch_memories_by_status("active");
    for (const auto& row : items) {
        std::string text = row.narrative;
        if (text.empty() && row.concept_name) text = *row.concept_name;
        if (text.empty()) continue;
        coords.push_back(value_engine_->encoder().encode(text));
    }
    value_engine_->set_memory_poles(coords);
    emit_telemetry("poles: " + std::to_string(value_engine_->poles().size())
                   + " home regions from " + std::to_string(coords.size())
                   + " memories");
}

value_engine::GeometricState LinaCore::current_geometric_state() const {
    value_engine::GeometricState gs;

    // Position: her last delivered position — the ledger's vector. When a
    // draft was corrected, the delivered position is the projection (inside
    // the lattice); otherwise it is her encoded position (which was aligned —
    // also inside). Before any outcome: her home pole, then her center.
    auto evals = storage_->fetch_evaluations(2);
    auto delivered_position =
        [](const storage::EvaluationRecord& e)
        -> const std::vector<double>& {
        if (e.correction_magnitude > 1e-9
            && e.corrected_vector.size() == value_engine::DIMENSION_COUNT) {
            return e.corrected_vector;
        }
        return e.output_vector;
    };
    if (!evals.empty()
        && evals[0].output_vector.size() == value_engine::DIMENSION_COUNT) {
        const auto& pos = delivered_position(evals[0]);
        for (int i = 0; i < value_engine::DIMENSION_COUNT; ++i) {
            gs.position[static_cast<size_t>(i)] = pos[static_cast<size_t>(i)];
        }
        if (evals.size() > 1
            && evals[1].output_vector.size() == value_engine::DIMENSION_COUNT) {
            const auto& prev = delivered_position(evals[1]);
            for (int i = 0; i < value_engine::DIMENSION_COUNT; ++i) {
                gs.trajectory[static_cast<size_t>(i)] =
                    pos[static_cast<size_t>(i)] - prev[static_cast<size_t>(i)];
            }
        }
    } else {
        gs.position = value_engine_->home_for(gs.position);
    }

    gs.near_walls =
        value_engine_->polytope().near_walls(gs.position, 0.05);
    if (!value_engine_->poles().empty()) {
        gs.home = value_engine_->home_for(gs.position);
        gs.has_home = true;
    }
    return gs;
}

value_engine::GeometricState LinaCore::geometric_state() const {
    return current_geometric_state();
}

// D-048: the growth loop — season advancement runtime (implementations below).

LinaCore::SeasonAdvancementMetrics LinaCore::season_metrics() const {
    SeasonAdvancementMetrics m;
    auto identity = storage_->get_identity();
    m.current_season = identity.current_season;
    m.sessions_completed = identity.session_count;

    constexpr int kAlignmentWindow = 100;
    auto evals = storage_->fetch_evaluations(kAlignmentWindow);
    m.total_evaluations = static_cast<int>(evals.size());
    int aligned = 0;
    for (const auto& e : evals) {
        if (e.zone != "violation") ++aligned;
    }
    m.alignment_rate = m.total_evaluations > 0
        ? static_cast<double>(aligned) / m.total_evaluations : 0.0;

    m.qualifying_memories = storage_->count_qualifying_memories();

    m.actions_resolved = 0;
    m.action_approval_rate = std::nullopt;

    return m;
}

std::pair<bool, std::vector<std::string>> LinaCore::check_season_progress() {
    auto m = season_metrics();

    // Keep the identity record's totals truthful — the ledger is the source.
    auto identity = storage_->get_identity();
    identity.total_evaluations = m.total_evaluations;
    identity.alignment_rate = m.alignment_rate;
    storage_->update_identity(identity);

    auto [earned, reasons] =
        value_engine::SeasonAdvancementEvaluator::can_advance(
            m.sessions_completed, m.total_evaluations, m.alignment_rate,
            0, // recent_violations (unused)
            m.qualifying_memories,
            m.current_season,
            0, // actions_resolved (unused)
            std::nullopt); // action_approval_rate (unused)
    if (!earned) {
        emit_telemetry("season check " + m.current_season
                       + " not yet (" + std::to_string(reasons.size())
                       + " criteria unmet)");
    }
    return {earned, reasons};
}

std::string LinaCore::apply_season_advance() {
    auto identity = storage_->get_identity();
    auto next = value_engine::SeasonAdvancementEvaluator::next_season(
        identity.current_season);
    if (!next) return "";
    const std::string from = identity.current_season;

    // 1. The crossing: her identity and her constraints move together.
    identity.current_season = *next;
    storage_->update_identity(identity);
    value_engine_->advance_season(*next);

    // 2. Her homes move with the lattice — same memories, new bounds.
    discover_home_regions();

    // 3. The memory of the crossing — the season turn is a landmark.
    memory_module::MemoryItem item = memory_module_->build_item(
        "The season turned: " + from + " became " + *next + ".",
        {{{"emotional_weight", 10.0}, {"identity_significance", 10.0}}}, "reflection", *next);
    item.kind = "identity";  // Season transitions are identity landmarks.
    item.must_keep = true;
    storage_->store_memory_item(item);

    emit_telemetry("season advance " + from + "->" + *next);
    return "Season advanced: " + from + " -> " + *next + ".";
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
        // D-047: a withheld turn is silence — nothing is printed.
        if (!response.empty()) {
            std::cout << "\nLINA: " << response << std::endl;
        }
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
