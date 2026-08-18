/**
 * model_driver.cpp — the symbiote driver seam (D-033)
 *
 * "Safe by design. Not safe by limitation."
 *
 * Providers plug INTO the module (D-023); this is the seam. The core build
 * ships no provider: make_driver() returns nullptr until a concrete driver
 * (llama.cpp, external API, NPU) registers here at its own milestone (D-007).
 * Without a driver, LinaCore degrades gracefully — no voice, identity intact.
 */

#include "host_model_adapter.hpp"

namespace lina::model {

std::unique_ptr<HostModelAdapter> make_driver(
    const std::string& /*model_type*/,
    const std::string& /*model_path*/,
    const std::string& /*api_endpoint*/,
    const std::string& /*api_key*/)
{
    // No driver is compiled into the core. Concrete drivers plug in here.
    return nullptr;
}

} // namespace lina::model
