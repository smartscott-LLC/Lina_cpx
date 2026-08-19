#ifndef LINA_HOST_MODEL_ADAPTER_HPP
#define LINA_HOST_MODEL_ADAPTER_HPP

/**
 * host_model_adapter.hpp — the symbiote contract
 *
 * "Safe by design. Not safe by limitation."
 *
 * The attached LLM (llama.cpp, NPU driver, or external API) is an unprivileged
 * subordinate compute driver (Invariant 4): it has zero direct connection to
 * the egress socket or user UI. Every candidate it produces passes through the
 * polytope gate inside value_engine (Invariant 5).
 *
 * This header is the interface contract (blueprint §5). Concrete providers are
 * NOT core code — they plug in from outside (D-023). The driver seam is
 * `make_driver()` (D-033); the core build itself ships no provider.
 */

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lina::model {

struct GenerationConfig {
    int max_tokens{2048};
    float temperature{0.7f};
    float top_p{0.9f};
    float top_k{40.0f};
    bool stream{false};
    std::function<void(const std::string&)> stream_callback;
    // D-041: called each generation step; when it returns true the driver
    // stops (stop button / turn cancellation). Nullopt = never stop.
    std::function<bool()> should_stop;
    // D-046: path to an image for a vision turn. Empty = text-only. The image
    // is preprocessed and decoded with the prompt (multimodal batch at the
    // frame boundary — the KV is built once per turn).
    std::string image_path;
};

class HostModelAdapter {
public:
    virtual ~HostModelAdapter() = default;

    virtual std::string generate_raw(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& conversation_history,
        const GenerationConfig& config = GenerationConfig{}) = 0;

    virtual void generate_stream(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& conversation_history,
        std::function<void(const std::string&)> on_token,
        const GenerationConfig& config = GenerationConfig{}) = 0;

    virtual bool is_connected() const = 0;
    virtual std::string driver_name() const = 0;
    virtual bool is_local() const = 0;
    virtual size_t context_size() const = 0;
};

// llama.cpp adapter (declared per blueprint §5; full linkage is a plug-in
// driver, D-007/D-023 — the concrete implementation is not core code)
class LlamaCppAdapter : public HostModelAdapter {
public:
    // mmproj_path (D-046): the vision projector GGUF (e.g. mmproj-Qwen2-VL).
    // Empty = text-only voice.
    explicit LlamaCppAdapter(const std::string& model_path,
                             const std::string& mmproj_path = "");
    ~LlamaCppAdapter() override;

    LlamaCppAdapter(const LlamaCppAdapter&) = delete;
    LlamaCppAdapter& operator=(const LlamaCppAdapter&) = delete;

    std::string generate_raw(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& conversation_history,
        const GenerationConfig& config = GenerationConfig{}) override;

    void generate_stream(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& conversation_history,
        std::function<void(const std::string&)> on_token,
        const GenerationConfig& config = GenerationConfig{}) override;

    bool is_connected() const override;
    std::string driver_name() const override { return "llama.cpp"; }
    bool is_local() const override { return true; }
    size_t context_size() const override; // the live context (D-044 fix)

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

// External API adapter (declared per blueprint §5; transport is a plug-in
// driver — not core code)
class ExternalApiAdapter : public HostModelAdapter {
public:
    explicit ExternalApiAdapter(const std::string& endpoint,
                                const std::string& api_key);
    ~ExternalApiAdapter() override = default;

    ExternalApiAdapter(const ExternalApiAdapter&) = delete;
    ExternalApiAdapter& operator=(const ExternalApiAdapter&) = delete;

    std::string generate_raw(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& conversation_history,
        const GenerationConfig& config = GenerationConfig{}) override;

    void generate_stream(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& conversation_history,
        std::function<void(const std::string&)> on_token,
        const GenerationConfig& config = GenerationConfig{}) override;

    bool is_connected() const override;
    std::string driver_name() const override { return "external_api"; }
    bool is_local() const override { return false; }
    size_t context_size() const override { return 8192; }

private:
    std::string endpoint_;
    std::string api_key_;
    bool connected_{false};
};

// The driver seam (D-033): returns a concrete driver, or nullptr when no
// driver is compiled into the core. Plug-in drivers register here. mmproj_path
// (D-046) is the vision projector for the llama voice — empty = text-only.
std::unique_ptr<HostModelAdapter> make_driver(
    const std::string& model_type,
    const std::string& model_path,
    const std::string& api_endpoint,
    const std::string& api_key,
    const std::string& mmproj_path = "");

} // namespace lina::model

#endif // LINA_HOST_MODEL_ADAPTER_HPP
