#ifndef LINA_STORAGE_BACKEND_HPP
#define LINA_STORAGE_BACKEND_HPP

/**
 * storage_backend.hpp — LINA's storage abstraction
 *
 * "Safe by design. Not safe by limitation."
 *
 * The contract every storage implementation satisfies (blueprint §4.1):
 * identity, memory items (incl. ethical-vector search), transcripts,
 * sessions, and the human-in-the-loop action ledger. Persistence is
 * PostgreSQL + pgvector by default (Invariant 2) — see PostgresBackend (D-004).
 */

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory_module.hpp"

namespace lina::storage {

struct TranscriptEntry {
    std::string id;
    std::string user_id;
    std::string session_id;
    std::string role;
    std::string content;
    std::string msg_type;
    std::string evaluation_id;
    std::string created_at;
};

struct SessionRecord {
    std::string id;
    std::string user_id;
    int session_number;
    std::string season;
    std::string depth;
    bool finalized;
    std::string created_at;
    std::string finalized_at;
};

struct ActionRecord {
    std::string id;
    std::string tool_name;
    std::string params_json;
    std::string state;
    std::string result;
    std::string error;
    std::string created_at;
    std::string updated_at;
};

// D-043: one technical log line (the telemetry bus — Invariant 6).
struct TelemetryLogRecord {
    int64_t id{0};
    std::string timestamp;
    std::string subsystem;
    std::string message;
    std::string severity;
    bool has_latency{false};
    double latency_ms{0.0};
};

struct IdentityRecord {
    std::string user_id;
    std::string current_season;
    std::string relationship_depth;
    std::string self_description;
    int session_count;
    int total_evaluations;
    double alignment_rate;
    std::string created_at;
    std::string updated_at;
};

class StorageBackend {
public:
    virtual ~StorageBackend() = default;

    // --- Identity ---
    virtual IdentityRecord get_identity(const std::string& user_id) = 0;
    virtual void update_identity(const IdentityRecord& identity) = 0;
    virtual int get_session_number(const std::string& user_id) = 0;

    // --- Memory Vectors ---
    virtual void store_memory_item(const memory_module::MemoryItem& item) = 0;
    virtual std::optional<memory_module::MemoryItem> load_memory_item(
        const std::string& item_id) = 0;
    virtual std::vector<memory_module::MemoryItemRow> fetch_memories_by_status(
        const std::string& status) = 0;
    virtual std::vector<memory_module::MemoryItemRow>
    search_memories_by_ethical_vector(
        const std::vector<double>& query_vector,
        int limit = 10) = 0;
    virtual void update_memory_item(
        const memory_module::MemoryItemRow& row) = 0;
    virtual void delete_memory_item(const std::string& item_id) = 0;
    virtual void log_memory_promotion(
        const std::string& user_id,
        const std::string& item_id,
        const std::string& from_stage,
        const std::string& to_stage,
        double score,
        const std::string& reason) = 0;

    // --- Transcripts ---
    virtual void store_transcript(const TranscriptEntry& entry) = 0;
    virtual std::vector<TranscriptEntry> get_transcripts(
        const std::string& user_id, const std::string& session_id) = 0;

    // --- Sessions ---
    virtual void create_session(const SessionRecord& session) = 0;
    virtual void finalize_session(const std::string& session_id) = 0;
    virtual std::optional<SessionRecord> get_session(
        const std::string& session_id) = 0;

    // --- Actions ---
    virtual void store_action(const ActionRecord& action) = 0;
    virtual std::optional<ActionRecord> load_action(
        const std::string& action_id) = 0;
    virtual void update_action_state(
        const std::string& action_id, const std::string& state) = 0;
    virtual std::vector<ActionRecord> get_pending_actions() = 0;

    // --- Telemetry (D-043) — the technical bus, persistent (Invariant 6) ---
    virtual void append_telemetry_log(
        const std::string& subsystem, const std::string& severity,
        const std::string& message,
        std::optional<double> latency_ms = std::nullopt) = 0;
    virtual std::vector<TelemetryLogRecord> fetch_telemetry_logs(
        int limit = 100) = 0;
};

} // namespace lina::storage

#endif // LINA_STORAGE_BACKEND_HPP
