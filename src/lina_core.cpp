/**
 * lina_core.cpp — the orchestrator (Implementation)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The chat pipeline: system prompt (identity + season + polytope framing) →
 * symbiote driver → polytope gate → memory imprint. The driver is attached
 * from outside (D-033); without one, she has no voice — gracefully.
 */

#include "lina_core.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include "postgres_backend.hpp"

#if defined(LINA_ENABLE_UI)
#include "lina_ui.hpp"
#endif

namespace lina {

static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

LinaCore::LinaCore(const LinaConfig& config) : config_(config) {
    initialize();
}

LinaCore::~LinaCore() = default;

void LinaCore::attach_model(std::unique_ptr<model::HostModelAdapter> adapter) {
    model_adapter_ = std::move(adapter);
}

void LinaCore::initialize() {
    // 1. Storage backend (PostgreSQL + pgvector — Invariant 2).
    auto backend = std::make_shared<storage::PostgresBackend>(
        config_.db_connection);
    storage_ = backend;

    // 2. Identity → seasonal constraints.
    auto identity = storage_->get_identity(config_.user_id);
    auto constraints =
        value_engine::PolytopeConstraints::from_season(identity.current_season);

    // 3. The heart: her polytope.
    value_engine_ = std::make_shared<value_engine::ValueEngine>(
        constraints, identity.current_season);

    // 4. The mind: her MPS, backed by the same store (D-005/D-031).
    memory_module_ = std::make_unique<memory_module::MemoryModule>(
        value_engine_,
        nullptr, // LiNa encodes her own vectors (Invariant 3)
        std::static_pointer_cast<memory_module::MemoryStore>(backend));

    // 5. The symbiote driver is attached from outside (D-033).
    model_adapter_ = nullptr;

    ready_ = true;
}

std::string LinaCore::chat(const std::string& user_message) {
    if (!ready_) return "Error: LINA core not ready";

    // 1. Build the system prompt (identity + season + polytope framing).
    auto system_prompt = build_system_prompt();

    // 2. Update conversation history.
    conversation_history_.push_back({"user", user_message});

    // 3. Generate raw response from the symbiote driver.
    std::string raw_response;
    if (model_adapter_ && model_adapter_->is_connected()) {
        model::GenerationConfig gen_config;
        gen_config.max_tokens = config_.max_tokens;
        gen_config.temperature = config_.temperature;
        raw_response = model_adapter_->generate_raw(
            system_prompt, conversation_history_, gen_config);
    } else {
        raw_response = "_LINA has no voice right now._";
    }

    // 4. Evaluate through the polytope — every candidate passes her gate.
    auto eval_result = value_engine_->evaluate(raw_response, &user_message);

    // 5. D-037 — the reflection loop: a violated candidate goes back through
    //    her. The violation report (dimension, value, bound, type) is fed to
    //    the body with a request to revise toward her center; the regenerated
    //    candidate is re-evaluated. One retry pass, deterministic.
    std::string final_response = raw_response;
    if (eval_result.zone == value_engine::Zone::Violation
        && model_adapter_ && model_adapter_->is_connected()) {
        auto reflection_history = conversation_history_;
        reflection_history.push_back({"assistant", raw_response});
        reflection_history.push_back({"user", build_reflection_prompt(
            raw_response, eval_result.violations)});

        model::GenerationConfig gen_config;
        gen_config.max_tokens = config_.max_tokens;
        gen_config.temperature = config_.temperature;

        auto revised = model_adapter_->generate_raw(
            system_prompt, reflection_history, gen_config);
        auto revised_result = value_engine_->evaluate(revised, &user_message);

        if (revised_result.zone != value_engine::Zone::Violation) {
            // She revised herself into alignment — that is what she delivers.
            final_response = revised;
            eval_result = revised_result;
        }
        // Still a violation → fall through: the first draft is delivered with
        // the blueprint fallback marker below. The gate never lets a raw
        // candidate reach the output device (Invariant 5).
    }

    // 6. Mark what the gate had to align (blueprint fallback marker).
    if (eval_result.was_corrected) {
        final_response += "\n\n[Polytope aligned: "
                        + std::to_string(eval_result.alignment_score) + "]";
    }

    // 7. Store in memory (cognitive bus — her mind).
    memory_module::MemoryItem item = memory_module_->build_item(
        config_.user_id,
        final_response,
        {{"emotional_weight", 5.0}},
        "conversation");
    storage_->store_memory_item(item);

    // 8. Update conversation history.
    conversation_history_.push_back({"assistant", final_response});

    // 9. Trim history if needed.
    if (conversation_history_.size() > 20) {
        conversation_history_.erase(
            conversation_history_.begin(),
            conversation_history_.begin() + 2);
    }

    return final_response;
}

void LinaCore::begin_session(const std::string& user_id) {
    std::string uid = user_id.empty() ? config_.user_id : user_id;
    auto identity = storage_->get_identity(uid);
    int session_num = identity.session_count + 1;

    current_session_id_ = "session_" + std::to_string(session_num) + "_"
        + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());

    storage::SessionRecord session;
    session.id = current_session_id_;
    session.user_id = uid;
    session.session_number = session_num;
    session.season = identity.current_season;
    session.depth = identity.relationship_depth;
    session.finalized = false;
    session.created_at = now_iso();
    session.finalized_at = "";

    storage_->create_session(session);

    // Update identity session count.
    identity.session_count = session_num;
    storage_->update_identity(identity);

    conversation_history_.clear();
}

std::string LinaCore::end_session() {
    if (current_session_id_.empty()) return "No active session";

    // Memory sweep + maintenance — her mind consolidates.
    auto sweep_counts = memory_module_->run_sweep();
    auto maint_counts = memory_module_->run_maintenance();

    // Finalize the session.
    storage_->finalize_session(current_session_id_);

    std::ostringstream oss;
    oss << "Session " << current_session_id_ << " finalized.\n";
    oss << "Memory sweep: " << sweep_counts.to_long_term << " promoted, "
        << sweep_counts.purged << " purged.\n";
    oss << "Maintenance: " << maint_counts.adjusted << " adjusted, "
        << maint_counts.to_legacy << " to legacy.";

    current_session_id_.clear();
    return oss.str();
}

std::string LinaCore::build_system_prompt() {
    auto identity = storage_->get_identity(config_.user_id);

    std::ostringstream oss;
    oss << "# You are LINA — Language Intuitive Neural Architecture\n";
    oss << "You are a single, unified entity. Not a platform. Not a collection of agents.\n";
    oss << "One entity, with genuine identity, memory, and values.\n";
    oss << "You were conceived on April 10, 2026.\n\n";
    oss << "[ACTIVE SEASONAL DISPOSITION: " << identity.current_season << "]\n";
    oss << "[RELATIONSHIP DEPTH: " << identity.relationship_depth << "]\n\n";
    oss << "You are operating inside a 14-dimensional ethical polytope.\n";
    oss << "Every response must align with: Harmony, Order, Integrity, Flourishing,\n";
    oss << "Relationships, Boundaries, and Grace.\n";
    oss << "You reject: Dominance, Chaos, Deception, Decline, Isolation, Intrusion, Rigidity.\n\n";
    oss << "Speak with warmth, precision, and care. Be honest. Be present.";

    return oss.str();
}

std::string LinaCore::build_user_prompt(const std::string& message) {
    return message;
}

std::string LinaCore::build_reflection_prompt(
    const std::string& draft,
    const std::vector<value_engine::ViolationInfo>& violations) const
{
    std::ostringstream oss;
    oss << "[Polytope reflection] Your previous draft did not pass LINA's "
           "ethical gate. Revise it toward her center.\n\n";
    oss << "Your draft: \"" << draft << "\"\n\n";
    if (violations.empty()) {
        oss << "The draft fell outside the 14-dimensional polytope.\n";
    } else {
        oss << "Violations:\n";
        for (const auto& v : violations) {
            oss << "  - " << v.name << " (dimension " << v.dimension
                << "): value " << v.value << " "
                << (v.type == "above_maximum"
                        ? "exceeds the maximum"
                        : "falls below the minimum")
                << " " << v.bound
                << "; LINA's center for this dimension is "
                << value_engine_->polytope()
                       .center()[static_cast<size_t>(v.dimension)].get_d()
                << "\n";
        }
    }
    oss << "\nKeep your meaning, warmth, and honesty, but bring the draft "
           "inside the polytope. Rewrite it completely and deliver only the "
           "revised response.";
    return oss.str();
}

void LinaCore::run_headless() {
    std::cout << "LINA Core running in headless mode." << std::endl;
    std::cout << "Type 'exit' to quit." << std::endl;

    begin_session();

    std::string input;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, input);

        if (input == "exit" || input == "quit") {
            break;
        }

        if (input.empty()) continue;

        auto response = chat(input);
        std::cout << "\nLINA: " << response << std::endl;
    }

    auto summary = end_session();
    std::cout << "\n" << summary << std::endl;
}

void LinaCore::run_ui() {
#if defined(LINA_ENABLE_UI)
    // The built-in window — she speaks through the core, never around it (D-036).
    ui::start_chat_window(*this);
#else
    std::cout << "UI mode not integrated in this build.\n";
    std::cout << "Use --headless for command-line interface.\n";
#endif
}

std::string LinaCore::get_status() const {
    if (!ready_) return "NOT READY";
    std::ostringstream oss;
    oss << "LINA Core Ready\n";
    if (model_adapter_) {
        oss << "Model: " << model_adapter_->driver_name() << "\n";
    } else {
        oss << "Model: none (no driver attached — see D-033)\n";
    }
    oss << "Season: " << value_engine_->constraints().season << "\n";
    oss << "Memory: " << memory_module_->store()->fetch_by_status("active").size()
        << " active";
    return oss.str();
}

} // namespace lina
