/**
 * llama_adapter.cpp — the voice (D-035)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The llama.cpp symbiote driver. The host model is an unprivileged subordinate
 * compute driver (Invariant 4): this adapter only generates raw text — every
 * candidate passes through the polytope gate inside LinaCore before it reaches
 * any output device (Invariant 5). The adapter links against the pinned
 * llama.cpp tree (commit 9b05354, D-035) via its C API.
 *
 * Implements the full HostModelAdapter contract: raw + streaming generation,
 * chat-template formatting (the model's own tokenizer.chat_template), KV-cache
 * lifecycle, and a thread-safe single context.
 */

#include "host_model_adapter.hpp"

#if defined(LINA_ENABLE_LLAMA)

#include "llama.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace lina::model {

namespace {

// One generation pass over the shared context. Guards with a mutex — a single
// chat pipeline drives it, but the reflection loop (D-037) can issue two
// back-to-back calls from the worker thread.
struct Generator {
    llama_model* model;
    llama_context* ctx;
    const llama_vocab* vocab;
    const std::string& chat_template;

    // Formats system prompt + history through the model's own chat template,
    // ending with the assistant-turn prefix.
    std::string format_prompt(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& history) const
    {
        std::vector<llama_chat_message> messages;
        if (!system_prompt.empty()) {
            messages.push_back({"system", system_prompt.c_str()});
        }
        for (const auto& turn : history) {
            const char* role =
                turn.first == "user" ? "user" : "assistant";
            messages.push_back({role, turn.second.c_str()});
        }

        int32_t len = llama_chat_apply_template(
            chat_template.c_str(), messages.data(), messages.size(),
            /*add_ass=*/true, nullptr, 0);
        if (len < 0) len = -len; // required size may come back negative

        std::string prompt(static_cast<size_t>(len), '\0');
        int32_t written = llama_chat_apply_template(
            chat_template.c_str(), messages.data(), messages.size(),
            /*add_ass=*/true, prompt.data(), len);
        if (written < 0) {
            prompt.resize(static_cast<size_t>(-written));
        } else if (written < static_cast<int32_t>(prompt.size())) {
            prompt.resize(static_cast<size_t>(written));
        }
        return prompt;
    }

    // The generation loop. Calls on_token (if set) per decoded piece.
    std::string run(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& history,
        const GenerationConfig& config,
        const std::function<void(const std::string&)>& on_token) const
    {
        llama_memory_clear(llama_get_memory(ctx), /*data=*/true);

        std::string prompt = format_prompt(system_prompt, history);

        // Tokenize (add_special=true — the template already added BOS markers;
        // llama.cpp strips/adds per the template's own add_bos metadata).
        std::vector<llama_token> prompt_tokens(prompt.size() + 128);
        int32_t n_tok = llama_tokenize(
            vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
            prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()),
            /*add_special=*/true, /*parse_special=*/false);
        if (n_tok < 0) {
            prompt_tokens.resize(static_cast<size_t>(-n_tok));
            n_tok = llama_tokenize(
                vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()),
                /*add_special=*/true, /*parse_special=*/false);
        }
        if (n_tok <= 0) return "";
        prompt_tokens.resize(static_cast<size_t>(n_tok));

        // Prompt pass.
        llama_batch prompt_batch =
            llama_batch_get_one(prompt_tokens.data(), n_tok);
        if (llama_decode(ctx, prompt_batch) < 0) return "";

        // Sampler chain: top-k → top-p → temperature → distribution.
        llama_sampler* sampler =
            llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler,
            llama_sampler_init_top_k(static_cast<int32_t>(config.top_k)));
        llama_sampler_chain_add(sampler,
            llama_sampler_init_top_p(config.top_p, /*min_keep=*/1));
        llama_sampler_chain_add(sampler,
            llama_sampler_init_temp(config.temperature));
        std::random_device rd;
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(rd()));

        std::string output;
        char piece[256];
        for (int i = 0; i < config.max_tokens; ++i) {
            // D-041: turn cancellation (stop button) — checked every step.
            if (config.should_stop && config.should_stop()) break;

            const llama_token id = llama_sampler_sample(sampler, ctx, -1);
            llama_sampler_accept(sampler, id);

            if (llama_vocab_is_eog(vocab, id)) break;

            const int32_t n = llama_token_to_piece(
                vocab, id, piece, static_cast<int32_t>(sizeof(piece)),
                /*lstrip=*/0, /*special=*/true);
            if (n <= 0) continue;

            output.append(piece, static_cast<size_t>(n));
            if (on_token) on_token(std::string(piece, static_cast<size_t>(n)));

            // D-041: a complete <tool_call>…</tool_call> block ends this pass
            // — the door stays open while the driver executes it.
            constexpr std::size_t kToolCallCloseLen = 12; // "</tool_call>"
            if (output.size() >= kToolCallCloseLen
                && output.compare(output.size() - kToolCallCloseLen,
                                  kToolCallCloseLen, "</tool_call>") == 0) {
                break;
            }

            const llama_batch next = llama_batch_get_one(
                const_cast<llama_token*>(&id), 1);
            if (llama_decode(ctx, next) < 0) break;
        }

        llama_sampler_free(sampler);
        return output;
    }
};

} // namespace

struct LlamaCppAdapter::Impl {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    std::string chat_template;
    bool connected = false;
    std::mutex mutex;

    ~Impl() {
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
    }

    bool load(const std::string& model_path) {
        llama_backend_init();
        llama_model_params mparams = llama_model_default_params();
        model = llama_model_load_from_file(model_path.c_str(), mparams);
        if (!model) return false;

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx = 4096;
        cparams.n_batch = 512;
        cparams.n_ubatch = 512;
        cparams.n_threads = 12;
        cparams.n_threads_batch = 12;
        ctx = llama_init_from_model(model, cparams);
        if (!ctx) return false;

        vocab = llama_model_get_vocab(model);
        const char* tmpl = llama_model_chat_template(model, nullptr);
        if (!tmpl) return false;
        chat_template = tmpl;

        connected = true;
        return true;
    }
};

LlamaCppAdapter::LlamaCppAdapter(const std::string& model_path)
    : pimpl_(std::make_unique<Impl>())
{
    pimpl_->connected = pimpl_->load(model_path);
}

LlamaCppAdapter::~LlamaCppAdapter() = default;

std::string LlamaCppAdapter::generate_raw(
    const std::string& system_prompt,
    const std::vector<std::pair<std::string, std::string>>& conversation_history,
    const GenerationConfig& config)
{
    if (!is_connected()) return "";
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    Generator generator{
        pimpl_->model, pimpl_->ctx, pimpl_->vocab, pimpl_->chat_template};
    return generator.run(system_prompt, conversation_history, config, nullptr);
}

void LlamaCppAdapter::generate_stream(
    const std::string& system_prompt,
    const std::vector<std::pair<std::string, std::string>>& conversation_history,
    std::function<void(const std::string&)> on_token,
    const GenerationConfig& config)
{
    if (!is_connected()) return;
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    Generator generator{
        pimpl_->model, pimpl_->ctx, pimpl_->vocab, pimpl_->chat_template};
    generator.run(system_prompt, conversation_history, config, on_token);
}

bool LlamaCppAdapter::is_connected() const {
    return pimpl_ && pimpl_->connected;
}

} // namespace lina::model

#endif // LINA_ENABLE_LLAMA
