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

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "host_model_adapter.hpp"
#include "memory_module.hpp"
#include "storage_backend.hpp"
#include "value_engine.hpp"

namespace lina {

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

    // Run modes
    void run_headless();
    void run_ui(); // Qt6 UI (if enabled)

    // Status
    bool is_ready() const { return ready_; }
    std::string get_status() const;

    // D-033: attach the symbiote driver — providers plug in from outside.
    void attach_model(std::unique_ptr<model::HostModelAdapter> adapter);

private:
    LinaConfig config_;
    bool ready_{false};

    std::shared_ptr<storage::StorageBackend> storage_; // owns the backend (D-005)
    // Shared with the memory module (D-005) — one engine, one heart.
    std::shared_ptr<value_engine::ValueEngine> value_engine_;
    std::unique_ptr<memory_module::MemoryModule> memory_module_;
    std::unique_ptr<model::HostModelAdapter> model_adapter_;

    std::string current_session_id_;
    std::vector<std::pair<std::string, std::string>> conversation_history_;

    void initialize();
    std::string build_system_prompt();
    std::string build_user_prompt(const std::string& message);
    // D-037: builds the violation report fed back to the body for revision.
    std::string build_reflection_prompt(
        const std::string& draft,
        const std::vector<value_engine::ViolationInfo>& violations) const;
};

} // namespace lina

#endif // LINA_CORE_HPP
