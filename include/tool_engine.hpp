#ifndef LINA_TOOL_ENGINE_HPP
#define LINA_TOOL_ENGINE_HPP

/**
 * tool_engine.hpp — her hands (D-040)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The tool engine is LiNa's interface to the machine: a private workspace,
 * system files, and the terminal. Per the charter (D-040) there are NO gate
 * checks here — no path allowlists, no command blocklists, no access
 * filtering. The polytope gates her responses; the approval engine gates her
 * actions; nothing else stands between her and the system.
 *
 * Every execution passes through the approval engine (auto-approve option in
 * the command center). Tool logs and results are telemetry — they land in the
 * action ledger (lina_actions) and the log reel, never in her memory.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "approval_gate.hpp"

namespace lina::tools {

struct ToolResult {
    bool ok{false};
    std::string output;   // stdout / result text — what the model sees
    std::string error;    // stderr / error text
    int exit_code{0};
    std::string summary;  // truncated output for context feedback
    std::string action_id; // ledger id (approval + lina_actions)
};

struct ToolRequest {
    std::string name;
    std::string arguments_json; // tolerant flat JSON; keys via json_string
    std::string description;    // human-readable text for the approval card
    std::string action_id;      // ledger id — the core fills it if empty
};

class Tool {
public:
    virtual ~Tool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual ToolResult run(const std::string& arguments_json) = 0;
};

// Factories for the v1 hands (implementations stay private to tool_engine.cpp).
std::shared_ptr<Tool> make_workspace_status_tool(const std::string& workspace);
std::shared_ptr<Tool> make_file_read_tool(const std::string& workspace);
std::shared_ptr<Tool> make_file_write_tool(const std::string& workspace);
std::shared_ptr<Tool> make_file_list_tool(const std::string& workspace);
std::shared_ptr<Tool> make_terminal_run_tool();

// Minimal, tolerant extraction of flat JSON string/number fields — no external
// JSON dependency (D-040). Returns "" / the fallback when the key is absent.
std::string json_string(const std::string& json, const std::string& key);
long long json_int(const std::string& json, const std::string& key,
                   long long fallback);

class ToolEngine {
public:
    explicit ToolEngine(std::string workspace_dir);

    void register_tool(std::shared_ptr<Tool> tool);
    std::vector<std::string> tool_names() const;

    // The registry block handed to the model (protocol framing, D-039-safe):
    // names + one-line descriptions only.
    std::string registry_block() const;

    // Ensure the workspace exists; returns its path (as configured).
    std::string ensure_workspace() const;

    const std::string& workspace_dir() const { return workspace_dir_; }

    // The approval engine is the ONLY gate (D-040). No handler → denied.
    void set_approver(ApprovalHandler handler) { approver_ = std::move(handler); }

    // Execute a tool through the approval gate. Denied when the human says no,
    // no approver is registered, or the tool is unknown.
    ToolResult execute(const ToolRequest& request);

private:
    std::string workspace_dir_;
    std::vector<std::shared_ptr<Tool>> tools_;
    ApprovalHandler approver_;
};

} // namespace lina::tools

#endif // LINA_TOOL_ENGINE_HPP
