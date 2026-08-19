/**
 * llama_adapter_tests.cpp — the voice, live (D-035)
 *
 * Loads the real pinned model (Qwen2-VL-2B-Instruct-Q6_K) through the symbiote
 * driver and proves the full contract: raw generation, streaming, and a chat()
 * round trip through LinaCore's polytope gate. Skipped (exit 0) when the model
 * file is not present so CI stays green without the weights.
 *
 * Requires: schema applied; LINA_TEST_DB reachable (default port 5433);
 *           model at LINA_LLAMA_MODEL or models/Qwen2-VL-2B-Instruct-Q6_K.gguf.
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lina_core.hpp"

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

static std::string test_conn_string() {
    const char* env = std::getenv("LINA_TEST_DB");
    return env ? std::string(env) : "postgresql://lina:lina@localhost:5433/lina";
}

static std::string unique_user() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return "itest_llama_" + std::to_string(now);
}

static std::string model_path() {
    const char* env = std::getenv("LINA_LLAMA_MODEL");
    if (env && *env) return std::string(env);
    // Tests run from build/ — try both repo-relative candidates.
    const char* candidates[] = {
        "models/Qwen2-VL-2B-Instruct-Q6_K.gguf",
        "../models/Qwen2-VL-2B-Instruct-Q6_K.gguf",
    };
    for (const char* candidate : candidates) {
        std::FILE* probe = std::fopen(candidate, "rb");
        if (probe) {
            std::fclose(probe);
            return candidate;
        }
    }
    return candidates[0];
}

int main() {
    // Unbuffered output — progress is visible even if a phase is slow.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string model = model_path();
    std::cout << "llama_adapter_tests: model path: " << model << "\n";

    std::FILE* probe = std::fopen(model.c_str(), "rb");
    if (!probe) {
        std::cout << "llama_adapter_tests: SKIP (model not found: " << model
                  << ")\n";
        return 0;
    }
    std::fclose(probe);
    std::cout << "llama_adapter_tests: loading model...\n";

    try {
        // --- The voice loads and reports itself correctly. ---
        auto adapter = std::make_unique<model::LlamaCppAdapter>(model);
        CHECK(adapter->is_connected());
        CHECK(adapter->driver_name() == "llama.cpp");
        CHECK(adapter->is_local());
        CHECK(adapter->context_size() == 8192);

        // --- Raw generation: short, warm, non-empty. ---
        model::GenerationConfig config;
        config.max_tokens = 64;
        config.temperature = 0.6f;
        const std::string reply = adapter->generate_raw(
            "# You are LINA — Language Intuitive Neural Architecture",
            {{"user", "Say hello in one short sentence."}},
            config);
        CHECK(!reply.empty());
        if (!reply.empty()) {
            std::cout << "llama_adapter_tests: sample reply: \""
                      << reply.substr(0, 120) << "...\"\n";
        }

        // --- Streaming: at least one piece arrives. ---
        int pieces = 0;
        adapter->generate_stream(
            "# You are LINA — Language Intuitive Neural Architecture",
            {{"user", "Say hi."}},
            [&pieces](const std::string& piece) {
                if (!piece.empty()) ++pieces;
            },
            config);
        CHECK(pieces > 0);

        // --- Long-frame regression (D-044 follow-up): a prompt larger than
        // n_batch (512) used to abort llama.cpp (n_tokens_all <= n_batch
        // assert). The prompt pass is now chunked — a ~3k-char frame (~750
        // tokens) must decode and generate without crashing. ---
        std::string long_question;
        for (int i = 0; i < 30; ++i) {
            long_question +=
                "This is filler context line " + std::to_string(i) +
                " to push the frame past the single-batch limit. ";
        }
        long_question += "\n\nGiven all that, answer in one short sentence: "
                         "what is the capital of France?";
        config.max_tokens = 48;
        const std::string long_reply = adapter->generate_raw(
            "# You are LINA — Language Intuitive Neural Architecture",
            {{"user", long_question}},
            config);
        CHECK(!long_reply.empty());
        if (!long_reply.empty()) {
            std::cout << "llama_adapter_tests: long-frame reply: \""
                      << long_reply.substr(0, 100) << "...\"\n";
        }

        // --- End-to-end through her gate (D-035): LinaCore + real voice. ---
        LinaConfig lconfig;
        lconfig.db_connection = test_conn_string();
        lconfig.user_id = unique_user();
        lconfig.headless = true;
        LinaCore core(lconfig);
        core.attach_model(std::move(adapter));
        core.begin_session();
        const std::string response = core.chat(
            "Hello LINA. Tell me who you are in one sentence.");
        CHECK(!response.empty());
        CHECK(response.find("no voice") == std::string::npos);
        std::cout << "llama_adapter_tests: chat reply: \""
                  << response.substr(0, 140) << "...\"\n";
        core.end_session();
    } catch (const std::exception& e) {
        std::cerr << "llama_adapter_tests: FATAL: " << e.what() << "\n";
        return 1;
    }

    std::cout << "llama_adapter_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
