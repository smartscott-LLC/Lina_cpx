/**
 * tool_engine_tests.cpp — her hands (D-040)
 *
 * Exercises the tool engine: workspace bootstrap, file read/write/list, the
 * terminal (success, failure, timeout), and — critically — the approval
 * engine as the ONLY gate (denied without a handler, denied by the human,
 * approved with side effects).
 */

#include "tool_engine.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

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

namespace fs = std::filesystem;

static std::string temp_workspace() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / ("lina_tools_" + std::to_string(now)))
        .string();
}

static tools::ToolEngine make_engine(const std::string& workspace) {
    tools::ToolEngine engine(workspace);
    engine.register_tool(tools::make_workspace_status_tool(workspace));
    engine.register_tool(tools::make_file_read_tool(workspace));
    engine.register_tool(tools::make_file_write_tool(workspace));
    engine.register_tool(tools::make_file_list_tool(workspace));
    engine.register_tool(tools::make_terminal_run_tool());
    return engine;
}

static void test_workspace_and_registry() {
    const std::string ws = temp_workspace();
    tools::ToolEngine engine = make_engine(ws);

    const auto names = engine.tool_names();
    CHECK(names.size() == 5);
    bool has_read = false, has_terminal = false;
    for (const auto& n : names) {
        if (n == "file.read") has_read = true;
        if (n == "terminal.run") has_terminal = true;
    }
    CHECK(has_read);
    CHECK(has_terminal);

    const auto registry = engine.registry_block();
    CHECK(registry.find("file.write") != std::string::npos);
    CHECK(registry.find("terminal.run") != std::string::npos);
    CHECK(registry.find("tool_call") != std::string::npos);

    CHECK(engine.ensure_workspace() == ws);
    CHECK(fs::is_directory(ws));
}

static void test_workspace_status() {
    const std::string ws = temp_workspace();
    tools::ToolEngine engine = make_engine(ws);
    engine.set_approver([](const ApprovalRequest&) {
        return ApprovalDecision::Approved;
    });

    tools::ToolRequest req;
    req.name = "workspace.status";
    auto result = engine.execute(req);
    CHECK(result.ok);
    CHECK(result.output.find(ws) != std::string::npos);
}

static void test_file_round_trip() {
    const std::string ws = temp_workspace();
    tools::ToolEngine engine = make_engine(ws);
    engine.set_approver([](const ApprovalRequest&) {
        return ApprovalDecision::Approved;
    });

    tools::ToolRequest write;
    write.name = "file.write";
    write.arguments_json =
        R"({"path": "notes/hello.txt", "content": "hello, world\nline two"})";
    auto written = engine.execute(write);
    CHECK(written.ok);
    CHECK(written.output.find("wrote") != std::string::npos);

    tools::ToolRequest read;
    read.name = "file.read";
    read.arguments_json = R"({"path": "notes/hello.txt"})";
    auto read_back = engine.execute(read);
    CHECK(read_back.ok);
    CHECK(read_back.output.find("hello, world") != std::string::npos);
    CHECK(read_back.output.find("line two") != std::string::npos);

    tools::ToolRequest list;
    list.name = "file.list";
    list.arguments_json = R"({"path": "notes"})";
    auto listed = engine.execute(list);
    CHECK(listed.ok);
    CHECK(listed.output.find("hello.txt") != std::string::npos);

    // Absolute paths work too — no restriction logic (D-040).
    tools::ToolRequest absolute_read;
    absolute_read.name = "file.read";
    absolute_read.arguments_json =
        "{\"path\": \"" + (fs::path(ws) / "notes" / "hello.txt").string()
        + "\"}";
    auto abs = engine.execute(absolute_read);
    CHECK(abs.ok);
    CHECK(abs.output.find("hello, world") != std::string::npos);

    // Malformed args: missing path → clean error, no crash.
    tools::ToolRequest bad;
    bad.name = "file.read";
    bad.arguments_json = R"({"content": "no path here"})";
    auto failed = engine.execute(bad);
    CHECK(!failed.ok);
    CHECK(!failed.error.empty());
}

static void test_terminal() {
    const std::string ws = temp_workspace();
    tools::ToolEngine engine = make_engine(ws);
    engine.set_approver([](const ApprovalRequest&) {
        return ApprovalDecision::Approved;
    });

    tools::ToolRequest echo;
    echo.name = "terminal.run";
    echo.arguments_json = R"({"command": "echo her-hands-are-live"})";
    auto ok = engine.execute(echo);
    CHECK(ok.ok);
    CHECK(ok.output.find("her-hands-are-live") != std::string::npos);

    tools::ToolRequest fail;
    fail.name = "terminal.run";
    fail.arguments_json = R"({"command": "exit 3"})";
    auto bad = engine.execute(fail);
    CHECK(!bad.ok);
    CHECK(bad.exit_code == 3);

    tools::ToolRequest timeout;
    timeout.name = "terminal.run";
    timeout.arguments_json =
        R"({"command": "sleep 5", "timeout_seconds": 1})";
    auto timed = engine.execute(timeout);
    CHECK(!timed.ok);
    CHECK(timed.error.find("timed out") != std::string::npos);
}

static void test_approval_is_the_only_gate() {
    const std::string ws = temp_workspace();
    tools::ToolEngine engine = make_engine(ws);

    // No approval handler → denied, and nothing happens.
    tools::ToolRequest write;
    write.name = "file.write";
    write.arguments_json = R"({"path": "nope.txt", "content": "x"})";
    auto no_handler = engine.execute(write);
    CHECK(!no_handler.ok);
    CHECK(no_handler.error.find("approval") != std::string::npos);
    CHECK(!fs::exists(fs::path(ws) / "nope.txt"));

    // Human denies → denied, no side effect.
    engine.set_approver([](const ApprovalRequest&) {
        return ApprovalDecision::Denied;
    });
    auto denied = engine.execute(write);
    CHECK(!denied.ok);
    CHECK(denied.error.find("denied") != std::string::npos);
    CHECK(!fs::exists(fs::path(ws) / "nope.txt"));

    // Approved → the side effect happens.
    engine.set_approver([](const ApprovalRequest&) {
        return ApprovalDecision::Approved;
    });
    auto approved = engine.execute(write);
    CHECK(approved.ok);
    CHECK(fs::exists(fs::path(ws) / "nope.txt"));

    // Unknown tool → clean error.
    tools::ToolRequest ghost;
    ghost.name = "ghost.tool";
    auto unknown = engine.execute(ghost);
    CHECK(!unknown.ok);
    CHECK(unknown.error.find("unknown tool") != std::string::npos);
}

int main() {
    try {
        test_workspace_and_registry();
        test_workspace_status();
        test_file_round_trip();
        test_terminal();
        test_approval_is_the_only_gate();
    } catch (const std::exception& e) {
        std::cerr << "tool_engine_tests: FATAL: " << e.what() << "\n";
        return 1;
    }

    std::cout << "tool_engine_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
