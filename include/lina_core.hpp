#ifndef LINA_CORE_HPP
#define LINA_CORE_HPP

/**
 * lina_core.hpp — the orchestrator
 *
 * "Safe by design. Not safe by limitation."
 *
 * LinaCore binds the chambers: storage (PostgreSQL + pgvector), the value
 * engine (her polytope), the memory module (her MPS), and the symbiote
 * driver (attached from outside — D-023/D-033). Every candidate response
 * passes through her polytope before it reaches the user (Invariant 5).
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "approval_gate.hpp"
#include "host_model_adapter.hpp"
#include "memory_module.hpp"
#include "storage_backend.hpp"
#include "stream_parser.hpp"
#include "tool_engine.hpp"
#include "value_engine.hpp"

namespace lina {

// D-038: technical-event sink — the telemetry bus (Invariant 6: process
// events never touch the cognitive bus). The UI routes these to the log reel.
using TelemetrySink = std::function<void(const std::string&)>;

struct LinaConfig {
    std::string db_connection{"postgresql://localhost/lina"};
    std::string model_type{"llama"};
    std::string model_path{"./models/llama.gguf"};
    std::string api_endpoint{""};
    std::string api_key{""};
    std::string user_id{"default_user"};
    bool headless{false};
    bool enable_ui{true};
    int max_tokens{2048};
    float temperature{0.7f};
    std::string season{"spring"};
    std::string log_level{"info"};
    std::string workspace_dir{"workspace"}; // her private workspace (D-040)
    int64_t window_ms{180000}; // [cycle_reset] window (D-041)
    int context_budget{8192};  // token budget — the rate limiter (D-041)
};

class LinaCore {
public:
    explicit LinaCore(const LinaConfig& config);
    ~LinaCore();

    // Main chat interface
    std::string chat(const std::string& user_message);

    // Session management
    void begin_session(const std::string& user_id = "");
    std::string end_session();

    // Direct access
    value_engine::ValueEngine& value_engine() { return *value_engine_; }
    memory_module::MemoryModule& memory_module() { return *memory_module_; }
    storage::StorageBackend& storage() { return *storage_; }
    model::HostModelAdapter& model() { return *model_adapter_; }
    tools::ToolEngine& tool_engine() { return *tool_engine_; }

    // D-040: execute a tool through the approval gate, recording the action
    // in the ledger (lina_actions — telemetry, never memory).
    tools::ToolResult execute_tool(const tools::ToolRequest& request);

    // Run modes
    void run_headless();
    void run_ui(); // Qt6 UI (if enabled)

    // Status
    bool is_ready() const { return ready_; }
    std::string get_status() const;

    // D-033: attach the symbiote driver — providers plug in from outside.
    void attach_model(std::unique_ptr<model::HostModelAdapter> adapter);

    // D-038: approval gate + telemetry bus.
    void set_approval_handler(ApprovalHandler handler);
    void set_telemetry_sink(TelemetrySink sink);
    // Blocks (up to request.timeout_ms) for a human decision. Denied when no
    // handler is registered. Future tool executor calls this before acting.
    ApprovalDecision request_approval(const ApprovalRequest& request);

    // D-041: the open-window turn driver. begin_turn runs the loop on a worker
    // thread: stream parser → flagged thoughts / tool calls / EOT → polytope
    // gate at the door → memory imprint. The window timer fires [cycle_reset]
    // and opens her floor. stop_turn cancels the current generation.
    struct TurnCallbacks {
        std::function<void(const std::string&)> on_thought;      // live thoughts
        std::function<void(double)> on_rolling_score;            // advisory 0..1
        std::function<void(const std::string&)> on_tool_call;    // raw JSON
        std::function<void(const std::string&, bool, const std::string&)>
            on_tool_result;                                      // name, ok, summary
        std::function<void(const std::string&)> on_complete;     // delivered response
        std::function<void(const std::string&)> on_window;       // [cycle_reset] etc.
        std::function<void(const std::string&)> on_error;
    };
    void begin_turn(const std::string& user_message,
                    TurnCallbacks callbacks);
    void stop_turn();
    bool turn_active() const { return turn_active_; }
    void set_window_ms(int64_t ms);

private:
    LinaConfig config_;
    bool ready_{false};

    std::shared_ptr<storage::StorageBackend> storage_; // owns the backend (D-005)
    // Shared with the memory module (D-005) — one engine, one heart.
    std::shared_ptr<value_engine::ValueEngine> value_engine_;
    std::unique_ptr<memory_module::MemoryModule> memory_module_;
    std::unique_ptr<model::HostModelAdapter> model_adapter_;
    std::unique_ptr<tools::ToolEngine> tool_engine_; // her hands (D-040)

    std::string current_session_id_;
    std::vector<std::pair<std::string, std::string>> conversation_history_;

    ApprovalHandler approval_handler_;
    TelemetrySink telemetry_sink_;
    std::mutex sink_mutex_;

    // D-041: turn state.
    std::atomic<bool> turn_active_{false};
    std::atomic<bool> turn_stop_{false};
    std::mutex turn_mutex_; // conversation_history_ + turn callbacks
    TurnCallbacks turn_callbacks_;
    std::thread turn_thread_;
    int64_t window_ms_{180000};
    std::chrono::steady_clock::time_point next_window_at_;
    std::mutex window_mutex_;
    std::condition_variable window_cv_;
    std::atomic<bool> window_stop_{false};
    std::thread window_thread_;

    void start_window_thread();
    void window_loop();
    void run_turn_loop(const std::string& user_message);
    void run_voluntary_turn();
    std::string build_turn_frame(
        const std::string& user_message,
        const std::vector<std::pair<std::string, std::string>>& history) const;
    void finalize_turn(std::string response_text,
                       const std::string& user_message);
    // The absolute gate (Invariant 5): evaluate → D-037 reflection → marker.
    std::pair<std::string, value_engine::EvaluationResult> apply_gate(
        const std::string& draft, const std::string* context);
    void emit_turn_event(const std::string& kind, const std::string& payload);
    void emit_telemetry(const std::string& message);

    void initialize();
    std::string build_system_prompt() const;
    std::string build_user_prompt(const std::string& message);
    // D-037: builds the violation report fed back to the body for revision.
    std::string build_reflection_prompt(
        const std::string& draft,
        const std::vector<value_engine::ViolationInfo>& violations) const;
};

} // namespace lina

#endif // LINA_CORE_HPP
