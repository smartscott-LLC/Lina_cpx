/**
 * tool_engine.cpp — her hands (Implementation, D-040)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The tools themselves carry ZERO restriction logic (no path allowlists, no
 * command blocklists). The only gate is the approval engine; the only record
 * of what she did is the action ledger (telemetry) — never her memory.
 */

#include "tool_engine.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>

#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace lina::tools {

namespace {

namespace fs = std::filesystem;

constexpr size_t kSummaryLimit = 400;

std::string truncate(const std::string& text, size_t limit = kSummaryLimit) {
    if (text.size() <= limit) return text;
    return text.substr(0, limit) + "...["
           + std::to_string(text.size() - limit) + " bytes more]";
}

// Paths: absolute paths are used as-is; relative paths resolve into the
// workspace. No restriction logic — she owns the machine (D-040).
std::string resolve_path(const std::string& workspace, const std::string& path) {
    if (path.empty()) return workspace;
    fs::path p(path);
    if (p.is_absolute()) return p.string();
    return (fs::path(workspace) / p).string();
}

// Runs a shell command, capturing combined stdout+stderr. Blocks up to
// timeout_seconds (0 = unlimited); kills the child on timeout.
ToolResult run_command(const std::string& command, long long timeout_seconds) {
    ToolResult result;
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result.error = "pipe failed: " + std::string(std::strerror(errno));
        result.exit_code = 1;
        return result;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        result.error = "fork failed: " + std::string(std::strerror(errno));
        result.exit_code = 1;
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }
    if (pid == 0) {
        // Child: both streams into the pipe, then exec the shell.
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buf[4096];
    int status = 0;
    bool timed_out = false;
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::seconds(timeout_seconds);

    for (;;) {
        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) break;

        struct pollfd pfd;
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 100) > 0) {
            ssize_t n;
            while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
                output.append(buf, static_cast<size_t>(n));
            }
        }
        if (timeout_seconds > 0
            && std::chrono::steady_clock::now() > deadline) {
            timed_out = true;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
    }
    // Final drain after the child exits.
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        output.append(buf, static_cast<size_t>(n));
    }
    close(pipefd[0]);

    result.output = output;
    result.summary = truncate(output);
    if (timed_out) {
        result.ok = false;
        result.exit_code = -1;
        result.error = "command timed out after "
                       + std::to_string(timeout_seconds) + "s";
        return result;
    }
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    result.ok = (result.exit_code == 0);
    if (!result.ok) {
        result.error = "exit code " + std::to_string(result.exit_code);
    }
    return result;
}

// ---------------------------------------------------------------------------
// The hands (v1, D-040): workspace, files, terminal.
// ---------------------------------------------------------------------------

class WorkspaceStatusTool : public Tool {
public:
    explicit WorkspaceStatusTool(std::string workspace)
        : workspace_(std::move(workspace)) {}

    std::string name() const override { return "workspace.status"; }
    std::string description() const override {
        return "Report the private workspace path and its current contents.";
    }
    ToolResult run(const std::string&) override {
        ToolResult result;
        try {
            fs::create_directories(workspace_);
            size_t count = 0;
            for (auto it = fs::directory_iterator(workspace_);
                 it != fs::directory_iterator(); ++it) {
                ++count;
            }
            result.ok = true;
            result.exit_code = 0;
            result.output = "workspace: " + workspace_
                            + "\nentries: " + std::to_string(count);
            result.summary = result.output;
        } catch (const fs::filesystem_error& e) {
            result.error = e.what();
            result.exit_code = 1;
        }
        return result;
    }

private:
    std::string workspace_;
};

class FileReadTool : public Tool {
public:
    explicit FileReadTool(std::string workspace)
        : workspace_(std::move(workspace)) {}

    std::string name() const override { return "file.read"; }
    std::string description() const override {
        return "Read a text file. args: {\"path\": \"<absolute or "
               "workspace-relative>\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string raw_path = json_string(args, "path");
        if (raw_path.empty()) {
            result.error = "path is required";
            result.exit_code = 1;
            return result;
        }
        const std::string path = resolve_path(workspace_, raw_path);
        if (fs::is_directory(path)) {
            result.error = "is a directory: " + path;
            result.exit_code = 1;
            return result;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            result.error = "cannot open: " + path;
            result.exit_code = 1;
            return result;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        result.output = ss.str();
        result.ok = true;
        result.summary = truncate(result.output);
        return result;
    }

private:
    std::string workspace_;
};

class FileWriteTool : public Tool {
public:
    explicit FileWriteTool(std::string workspace)
        : workspace_(std::move(workspace)) {}

    std::string name() const override { return "file.write"; }
    std::string description() const override {
        return "Write text to a file (parent directories are created). "
               "args: {\"path\": \"...\", \"content\": \"...\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string raw_path = json_string(args, "path");
        if (raw_path.empty()) {
            result.error = "path is required";
            result.exit_code = 1;
            return result;
        }
        const std::string path = resolve_path(workspace_, raw_path);
        const std::string content = json_string(args, "content");
        try {
            fs::create_directories(fs::path(path).parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) {
                result.error = "cannot write: " + path;
                result.exit_code = 1;
                return result;
            }
            out << content;
            out.close();
            result.ok = true;
            result.exit_code = 0;
            result.output = "wrote " + std::to_string(content.size())
                            + " bytes to " + path;
            result.summary = result.output;
        } catch (const fs::filesystem_error& e) {
            result.error = e.what();
            result.exit_code = 1;
        }
        return result;
    }

private:
    std::string workspace_;
};

class FileListTool : public Tool {
public:
    explicit FileListTool(std::string workspace)
        : workspace_(std::move(workspace)) {}

    std::string name() const override { return "file.list"; }
    std::string description() const override {
        return "List a directory. args: {\"path\": \"<optional, defaults to "
               "the workspace>\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string dir =
            resolve_path(workspace_, json_string(args, "path"));
        try {
            std::ostringstream ss;
            size_t count = 0;
            for (auto it = fs::directory_iterator(dir);
                 it != fs::directory_iterator(); ++it) {
                ss << (it->is_directory() ? "[dir]  " : "[file] ")
                   << it->path().filename().string() << "\n";
                ++count;
            }
            result.ok = true;
            result.exit_code = 0;
            result.output = ss.str() + "(" + std::to_string(count)
                            + " entries in " + dir + ")";
            result.summary = truncate(result.output);
        } catch (const fs::filesystem_error& e) {
            result.error = e.what();
            result.exit_code = 1;
        }
        return result;
    }

private:
    std::string workspace_;
};

class TerminalRunTool : public Tool {
public:
    std::string name() const override { return "terminal.run"; }
    std::string description() const override {
        return "Run a shell command and return its combined output. "
               "args: {\"command\": \"...\", \"timeout_seconds\": <optional, "
               "0 = unlimited>}.";
    }
    ToolResult run(const std::string& args) override {
        const std::string command = json_string(args, "command");
        if (command.empty()) {
            ToolResult result;
            result.error = "command is required";
            result.exit_code = 1;
            return result;
        }
        const long long timeout = json_int(args, "timeout_seconds", 120);
        return run_command(command, timeout);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Tolerant flat-JSON extraction (no external JSON dependency)
// ---------------------------------------------------------------------------

namespace {

// Position just past the first `"key":` occurrence, or npos.
std::string::size_type find_key(const std::string& json,
                                const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    std::string::size_type pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        const auto colon = json.find(':', pos + needle.size());
        if (colon == std::string::npos) return std::string::npos;
        auto value = colon + 1;
        while (value < json.size()
               && std::isspace(static_cast<unsigned char>(json[value]))) {
            ++value;
        }
        return value;
    }
    return std::string::npos;
}

} // namespace

std::string json_string(const std::string& json, const std::string& key) {
    const auto value = find_key(json, key);
    if (value == std::string::npos || value >= json.size()) return "";
    if (json[value] == '"') {
        std::string out;
        bool escaped = false;
        for (auto i = value + 1; i < json.size(); ++i) {
            const char c = json[i];
            if (c == '"' && !escaped) break;
            if (c == '\\' && !escaped) {
                escaped = true;
                continue;
            }
            if (escaped) {
                if (c == 'n') out += '\n';
                else if (c == 't') out += '\t';
                else out += c;
                escaped = false;
            } else {
                out += c;
            }
        }
        return out;
    }
    auto end = value;
    while (end < json.size()
           && !std::isspace(static_cast<unsigned char>(json[end]))
           && json[end] != ',' && json[end] != '}' && json[end] != ']') {
        ++end;
    }
    return json.substr(value, end - value);
}

long long json_int(const std::string& json, const std::string& key,
                   long long fallback) {
    const std::string raw = json_string(json, key);
    if (raw.empty()) return fallback;
    try {
        return std::stoll(raw);
    } catch (...) {
        return fallback;
    }
}

// ---------------------------------------------------------------------------
// Hand factories
// ---------------------------------------------------------------------------

std::shared_ptr<Tool> make_workspace_status_tool(const std::string& workspace) {
    return std::make_shared<WorkspaceStatusTool>(workspace);
}

std::shared_ptr<Tool> make_file_read_tool(const std::string& workspace) {
    return std::make_shared<FileReadTool>(workspace);
}

std::shared_ptr<Tool> make_file_write_tool(const std::string& workspace) {
    return std::make_shared<FileWriteTool>(workspace);
}

std::shared_ptr<Tool> make_file_list_tool(const std::string& workspace) {
    return std::make_shared<FileListTool>(workspace);
}

std::shared_ptr<Tool> make_terminal_run_tool() {
    return std::make_shared<TerminalRunTool>();
}

// ---------------------------------------------------------------------------
// ToolEngine
// ---------------------------------------------------------------------------

ToolEngine::ToolEngine(std::string workspace_dir)
    : workspace_dir_(std::move(workspace_dir)) {}

void ToolEngine::register_tool(std::shared_ptr<Tool> tool) {
    tools_.push_back(std::move(tool));
}

std::vector<std::string> ToolEngine::tool_names() const {
    std::vector<std::string> names;
    for (const auto& tool : tools_) names.push_back(tool->name());
    return names;
}

std::string ToolEngine::registry_block() const {
    std::ostringstream oss;
    oss << "[TOOL REGISTRY]\n";
    for (const auto& tool : tools_) {
        oss << "- " << tool->name() << ": " << tool->description() << "\n";
    }
    oss << "To call a tool, emit: <tool_call>"
           "{\"name\":\"<tool>\",\"arguments\":{...}}"
           "</tool_call>";
    return oss.str();
}

std::string ToolEngine::ensure_workspace() const {
    fs::create_directories(workspace_dir_);
    return workspace_dir_;
}

ToolResult ToolEngine::execute(const ToolRequest& request) {
    Tool* matched = nullptr;
    for (const auto& tool : tools_) {
        if (tool->name() == request.name) {
            matched = tool.get();
            break;
        }
    }
    if (!matched) {
        ToolResult unknown;
        unknown.error = "unknown tool: " + request.name;
        unknown.exit_code = 1;
        return unknown;
    }

    // The approval engine is the ONLY gate (D-040). No handler → denied.
    if (!approver_) {
        ToolResult denied;
        denied.error = "denied: no approval handler registered";
        denied.exit_code = 1;
        return denied;
    }

    ApprovalRequest approval;
    approval.action_id = request.action_id.empty()
        ? "act_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())
        : request.action_id;
    approval.tool_name = request.name;
    approval.description = request.description.empty()
        ? matched->description() + " args=" + request.arguments_json
        : request.description;

    const auto decision = approver_(approval);
    if (decision != ApprovalDecision::Approved) {
        ToolResult denied;
        denied.ok = false;
        denied.error = decision == ApprovalDecision::Denied
            ? "denied by human"
            : "approval timed out";
        denied.exit_code = 1;
        denied.action_id = approval.action_id;
        return denied;
    }

    ToolResult result = matched->run(request.arguments_json);
    result.action_id = approval.action_id;
    return result;
}

} // namespace lina::tools
