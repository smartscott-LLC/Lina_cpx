#ifndef LINA_BROWSER_DRIVER_HPP
#define LINA_BROWSER_DRIVER_HPP

/**
 * browser_driver.hpp — her browser hands (D-042)
 *
 * "Safe by design. Not safe by limitation."
 *
 * A pure-C++ browser automation driver over the Chrome DevTools Protocol —
 * Playwright-style, zero Python, no new dependencies. Chrome is launched with
 * remote debugging; a minimal RFC 6455 WebSocket client carries the CDP
 * JSON-RPC messages; the driver exposes navigate / eval / click / type /
 * screenshot / read-page. All of it is approval-gated like every other hand
 * (D-040) — the approval engine is the only gate.
 */

#include <memory>
#include <string>

#include "tool_engine.hpp"

namespace lina::tools {

// Resolve the browser binary: $LINA_BROWSER_PATH, else a PATH search for
// google-chrome / chromium variants. Empty when unavailable.
std::string find_browser_binary();
bool browser_available();

// Factories for the browser hands (registered like the rest of the tools).
// Tools that write files (screenshot) take the workspace for path resolution.
std::shared_ptr<Tool> make_browser_open_tool();
std::shared_ptr<Tool> make_browser_navigate_tool();
std::shared_ptr<Tool> make_browser_eval_tool();
std::shared_ptr<Tool> make_browser_text_tool();
std::shared_ptr<Tool> make_browser_content_tool();
std::shared_ptr<Tool> make_browser_click_tool();
std::shared_ptr<Tool> make_browser_type_tool();
std::shared_ptr<Tool> make_browser_screenshot_tool(const std::string& workspace);
std::shared_ptr<Tool> make_browser_close_tool();

} // namespace lina::tools

#endif // LINA_BROWSER_DRIVER_HPP
