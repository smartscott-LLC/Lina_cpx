/**
 * browser_driver_tests.cpp — her browser hands (D-042)
 *
 * Drives a real headless Chrome/Brave via the pure-C++ CDP driver over data:
 * URLs — no network required. Skips (exit 0) when no browser binary is found,
 * so CI stays green without one.
 */

#include "browser_driver.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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
    return (fs::temp_directory_path() / ("lina_browser_" + std::to_string(now)))
        .string();
}

static tools::ToolEngine make_engine(const std::string& workspace) {
    tools::ToolEngine engine(workspace);
    engine.register_tool(tools::make_browser_open_tool());
    engine.register_tool(tools::make_browser_eval_tool());
    engine.register_tool(tools::make_browser_text_tool());
    engine.register_tool(tools::make_browser_click_tool());
    engine.register_tool(tools::make_browser_type_tool());
    engine.register_tool(tools::make_browser_screenshot_tool(workspace));
    engine.register_tool(tools::make_browser_close_tool());
    engine.set_approver([](const ApprovalRequest&) {
        return ApprovalDecision::Approved;
    });
    return engine;
}

static tools::ToolResult run(tools::ToolEngine& engine, const std::string& name,
                             const std::string& args) {
    tools::ToolRequest req;
    req.name = name;
    req.arguments_json = args;
    return engine.execute(req);
}

int main() {
    if (!tools::browser_available()) {
        std::cout << "browser_driver_tests: SKIP (no browser binary found)\n";
        return 0;
    }

    const std::string ws = temp_workspace();
    fs::create_directories(ws); // the screenshot lands here
    auto engine = make_engine(ws);

    // A data: URL — the whole test fits in one payload, no network.
    // Note: the onclick quotes must be JSON-escaped (\\") for the url arg.
    const std::string page =
        "data:text/html,<title>LINA</title>"
        "<h1 id=h>Hello Her</h1>"
        "<input id=i>"
        "<button id=b onclick=\\\"document.title='clicked'\\\">Go</button>";

    try {
        // --- Open + read the page. ---
        auto opened = run(engine, "browser.open",
                          "{\"url\":\"" + page + "\"}");
        CHECK(opened.ok);
        if (opened.ok) {
            std::cout << "browser_driver_tests: opened ok\n";
        }

        auto text = run(engine, "browser.text", "{}");
        CHECK(text.ok);
        CHECK(text.output.find("Hello Her") != std::string::npos);

        auto title = run(engine, "browser.eval",
                         "{\"expression\":\"document.title\"}");
        CHECK(title.ok);
        CHECK(title.output.find("LINA") != std::string::npos);

        // --- Type into the input. ---
        auto typed = run(engine, "browser.type",
                         "{\"selector\":\"#i\",\"text\":\"typed-words\"}");
        CHECK(typed.ok);
        auto value = run(engine, "browser.eval",
                         "{\"expression\":\"document.getElementById('i').value\"}");
        CHECK(value.ok);
        CHECK(value.output.find("typed-words") != std::string::npos);

        // --- Click the button (its onclick sets document.title). ---
        auto clicked = run(engine, "browser.click", "{\"selector\":\"#b\"}");
        CHECK(clicked.ok);
        auto after = run(engine, "browser.eval",
                         "{\"expression\":\"document.title\"}");
        CHECK(after.ok);
        CHECK(after.output.find("clicked") != std::string::npos);

        // --- Screenshot lands in the workspace as a real PNG. ---
        auto shot = run(engine, "browser.screenshot",
                        "{\"path\":\"shot.png\"}");
        CHECK(shot.ok);
        const std::string png_path = (fs::path(ws) / "shot.png").string();
        CHECK(fs::exists(png_path));
        if (fs::exists(png_path)) {
            std::ifstream in(png_path, std::ios::binary);
            unsigned char magic[4] = {0, 0, 0, 0};
            in.read(reinterpret_cast<char*>(magic), 4);
            CHECK(magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N'
                  && magic[3] == 'G');
            in.seekg(0, std::ios::end);
            const auto size = in.tellg();
            CHECK(size > 1000);
        }

        // --- Approval gating: denied eval → clean error, no crash. ---
        engine.set_approver([](const ApprovalRequest&) {
            return ApprovalDecision::Denied;
        });
        auto denied = run(engine, "browser.eval",
                          "{\"expression\":\"document.title\"}");
        CHECK(!denied.ok);

        // --- Close. ---
        engine.set_approver([](const ApprovalRequest&) {
            return ApprovalDecision::Approved;
        });
        auto closed = run(engine, "browser.close", "{}");
        CHECK(closed.ok);
        CHECK(closed.output.find("closed") != std::string::npos);
    } catch (const std::exception& e) {
        std::cerr << "browser_driver_tests: FATAL: " << e.what() << "\n";
        // Make sure the browser is not left behind.
        auto closer = tools::make_browser_close_tool();
        closer->run("{}");
        return 1;
    }

    std::cout << "browser_driver_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
