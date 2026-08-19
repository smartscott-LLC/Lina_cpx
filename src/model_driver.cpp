/**
 * model_driver.cpp — the symbiote driver seam (D-033)
 *
 * "Safe by design. Not safe by limitation."
 *
 * Providers plug INTO the module (D-023); this is the seam. The core build
 * ships no provider unless one is compiled in: with `LINA_ENABLE_LLAMA=ON`,
 * `make_driver("llama", …)` returns the real voice (D-035); otherwise it
 * returns nullptr and LinaCore degrades gracefully — no voice, identity intact.
 */

#include "host_model_adapter.hpp"

namespace lina::model {

std::unique_ptr<HostModelAdapter> make_driver(
    const std::string& model_type,
    const std::string& model_path,
    const std::string& api_endpoint,
    const std::string& api_key)
{
    // api_endpoint/api_key are the external provider's LOCAL config — they
    // travel straight into ExternalApiAdapter's private members (blueprint §5)
    // and never surface in logs, telemetry, or storage. Unused in a llama-only
    // build, hence the (void).
    (void)api_endpoint;
    (void)api_key;
#if defined(LINA_ENABLE_LLAMA)
    if (model_type == "llama") {
        return std::make_unique<LlamaCppAdapter>(model_path);
    }
#else
    (void)model_type;
    (void)model_path;
#endif
    // No driver is compiled into the core. Concrete drivers plug in here.
    return nullptr;
}

} // namespace lina::model
