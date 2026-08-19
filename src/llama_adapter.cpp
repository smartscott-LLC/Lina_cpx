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
#include "mtmd.h"
#include "mtmd-helper.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
    int32_t n_batch; // max tokens per llama_decode call (chunked prompt pass)
    mtmd_context* vision; // D-046: vision projector context (nullptr = blind)

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

    // The prompt pass for a text-only turn: tokenize, then decode in
    // n_batch-sized chunks. Returns false on decode failure.
    bool run_text_prompt(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& history) const
    {
        std::string prompt = format_prompt(system_prompt, history);

        // Tokenize (add_special=true — the template already added BOS markers;
        // llama.cpp strips/adds per the template's own add_bos metadata).
        // parse_special=true (D-046 fix): the chat template's <|im_start|> /
        // <|im_end|> structure must become real special tokens — byte-splitting
        // them corrupted the turn boundaries and made the model echo the
        // template back as text (and ignore the system role).
        std::vector<llama_token> prompt_tokens(prompt.size() + 128);
        int32_t n_tok = llama_tokenize(
            vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
            prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()),
            /*add_special=*/true, /*parse_special=*/true);
        if (n_tok < 0) {
            prompt_tokens.resize(static_cast<size_t>(-n_tok));
            n_tok = llama_tokenize(
                vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()),
                /*add_special=*/true, /*parse_special=*/true);
        }
        if (n_tok <= 0) return false;
        prompt_tokens.resize(static_cast<size_t>(n_tok));

        // Prompt pass. Frames can be long (memory injection + tools registry
        // + conversation) — llama_decode accepts at most n_batch tokens per
        // call and asserts beyond that (n_tokens_all <= n_batch), so split
        // into batches; llama.cpp tracks positions internally per call.
        size_t off = 0;
        while (off < prompt_tokens.size()) {
            const size_t chunk = std::min<size_t>(
                static_cast<size_t>(n_batch), prompt_tokens.size() - off);
            llama_batch prompt_batch = llama_batch_get_one(
                prompt_tokens.data() + off, static_cast<int32_t>(chunk));
            if (llama_decode(ctx, prompt_batch) < 0) return false;
            off += chunk;
        }
        return true;
    }

    // The prompt pass for a vision turn (D-046): the image is preprocessed
    // into embeddings and decoded together with the text in one KV pass at the
    // frame boundary. The mtmd helpers handle n_batch splitting and M-RoPE
    // positions. Returns false on any failure (the turn degrades to silence).
    bool run_vision_prompt(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& history,
        const std::string& image_path) const
    {
        // The marker ("<__media__>") goes before the current user message;
        // mtmd_tokenize replaces it with the image tokens.
        std::vector<std::pair<std::string, std::string>> hist = history;
        if (!hist.empty() && hist.back().first == "user") {
            hist.back().second =
                std::string(mtmd_default_marker()) + "\n" + hist.back().second;
        }
        const std::string prompt = format_prompt(system_prompt, hist);

        mtmd_input_text text;
        text.text = prompt.data();
        text.text_len = prompt.size();
        text.add_special = true;
        text.parse_special = true; // the template's <|im_start|> etc.

        // Decode the image file (stb_image inside mtmd) into a bitmap.
        const auto wrapper = mtmd_helper_bitmap_init_from_file(
            vision, image_path.c_str(), /*placeholder=*/false);
        if (!wrapper.bitmap) return false;

        mtmd_input_chunks* chunks = mtmd_input_chunks_init();
        const mtmd_bitmap* bitmaps[] = {wrapper.bitmap};
        const int32_t res =
            mtmd_tokenize(vision, chunks, &text, bitmaps, 1);
        if (res != 0) {
            mtmd_input_chunks_free(chunks);
            mtmd_bitmap_free(wrapper.bitmap);
            return false;
        }

        // Decode each chunk in order: text chunks via the batching helper,
        // media chunks via a batch-encode + embd decode (logits on the final
        // prompt token so the sampler can start).
        llama_pos n_past = 0;
        mtmd_batch* mbatch = nullptr;
        bool ok = true;
        const size_t n_chunks = mtmd_input_chunks_size(chunks);
        for (size_t i = 0; i < n_chunks; ++i) {
            const mtmd_input_chunk* chunk = mtmd_input_chunks_get(chunks, i);
            if (mtmd_input_chunk_get_type(chunk)
                == MTMD_INPUT_CHUNK_TYPE_TEXT) {
                llama_pos new_n_past = n_past;
                if (mtmd_helper_eval_chunk_single(
                        vision, ctx, chunk, n_past, /*seq_id=*/0, n_batch,
                        /*logits_last=*/i == n_chunks - 1,
                        &new_n_past) != 0) {
                    ok = false;
                    break;
                }
                n_past = new_n_past;
            } else {
                float* embd = nullptr;
                if (mbatch) embd = mtmd_batch_get_output_embd(mbatch, chunk);
                if (!embd) {
                    if (mbatch) mtmd_batch_free(mbatch);
                    mbatch = mtmd_batch_init(vision);
                    if (mtmd_batch_add_chunk(mbatch, chunk) != 0
                        || mtmd_batch_encode(mbatch) != 0) {
                        ok = false;
                        break;
                    }
                    embd = mtmd_batch_get_output_embd(mbatch, chunk);
                }
                if (!embd) {
                    ok = false;
                    break;
                }
                llama_pos new_n_past = n_past;
                if (mtmd_helper_decode_image_chunk(
                        vision, ctx, chunk, embd, n_past, /*seq_id=*/0,
                        n_batch, &new_n_past, nullptr, nullptr) != 0) {
                    ok = false;
                    break;
                }
                n_past = new_n_past;
            }
        }
        if (mbatch) mtmd_batch_free(mbatch);
        mtmd_input_chunks_free(chunks);
        mtmd_bitmap_free(wrapper.bitmap);
        return ok;
    }

    // The generation loop. Calls on_token (if set) per decoded piece.
    std::string run(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& history,
        const GenerationConfig& config,
        const std::function<void(const std::string&)>& on_token) const
    {
        llama_memory_clear(llama_get_memory(ctx), /*data=*/true);

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

        // Prompt pass: multimodal when an image rides the turn (D-046),
        // text-only otherwise.
        bool ok = false;
        if (!config.image_path.empty() && vision) {
            ok = run_vision_prompt(system_prompt, history, config.image_path);
        } else {
            ok = run_text_prompt(system_prompt, history);
        }
        if (!ok) {
            llama_sampler_free(sampler);
            return "";
        }

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
    int32_t n_batch = 2048; // max tokens per decode call — matches cparams
    size_t n_ctx = 8192;    // live context size — reported by context_size()
    mtmd_context* vision = nullptr; // D-046: vision projector (her eyes)
    bool connected = false;
    std::mutex mutex;

    ~Impl() {
        if (vision) mtmd_free(vision);
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
    }

    bool load(const std::string& model_path, const std::string& mmproj_path) {
        llama_backend_init();
        llama_model_params mparams = llama_model_default_params();
        model = llama_model_load_from_file(model_path.c_str(), mparams);
        if (!model) return false;

        llama_context_params cparams = llama_context_default_params();
        // 8192 context matches LinaConfig::context_budget (the D-041 rate
        // limiter) so the budget cue is honest; the model trains to 32768, so
        // this sits comfortably inside. KV cost: ~224 MiB at 8192.
        cparams.n_ctx = 8192;
        cparams.n_batch = 2048;
        cparams.n_ubatch = 512;
        cparams.n_threads = 12;
        cparams.n_threads_batch = 12;
        ctx = llama_init_from_model(model, cparams);
        if (!ctx) return false;
        n_batch = cparams.n_batch;
        n_ctx = static_cast<size_t>(cparams.n_ctx);

        vocab = llama_model_get_vocab(model);
        const char* tmpl = llama_model_chat_template(model, nullptr);
        if (!tmpl) return false;
        chat_template = tmpl;

        // D-046: the vision projector — her eyes. Failure to load degrades to
        // a text-only voice (she is still herself; she just cannot see yet).
        if (!mmproj_path.empty()) {
            mtmd_context_params vparams = mtmd_context_params_default();
            vparams.use_gpu = false;
            vparams.n_threads = 12;
            vparams.warmup = true;
            vision = mtmd_init_from_file(mmproj_path.c_str(), model, vparams);
            if (!vision) {
                std::fprintf(stderr,
                             "[llama_adapter] vision load failed: %s\n",
                             mmproj_path.c_str());
            } else if (!mtmd_support_vision(vision)) {
                mtmd_free(vision);
                vision = nullptr;
                std::fprintf(stderr,
                             "[llama_adapter] mmproj has no vision support: %s\n",
                             mmproj_path.c_str());
            } else {
                std::fprintf(stderr,
                             "[llama_adapter] vision attached: %s\n",
                             mmproj_path.c_str());
            }
        }

        connected = true;
        return true;
    }
};

LlamaCppAdapter::LlamaCppAdapter(const std::string& model_path,
                                 const std::string& mmproj_path)
    : pimpl_(std::make_unique<Impl>())
{
    pimpl_->connected = pimpl_->load(model_path, mmproj_path);
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
        pimpl_->model, pimpl_->ctx, pimpl_->vocab, pimpl_->chat_template,
        pimpl_->n_batch, pimpl_->vision};
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
        pimpl_->model, pimpl_->ctx, pimpl_->vocab, pimpl_->chat_template,
        pimpl_->n_batch, pimpl_->vision};
    generator.run(system_prompt, conversation_history, config, on_token);
}

bool LlamaCppAdapter::is_connected() const {
    return pimpl_ && pimpl_->connected;
}

size_t LlamaCppAdapter::context_size() const {
    return pimpl_ ? pimpl_->n_ctx : 0;
}

} // namespace lina::model

#endif // LINA_ENABLE_LLAMA
