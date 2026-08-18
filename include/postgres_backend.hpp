#ifndef LINA_POSTGRES_BACKEND_HPP
#define LINA_POSTGRES_BACKEND_HPP

/**
 * postgres_backend.hpp — PostgreSQL + pgvector backend
 *
 * "Safe by design. Not safe by limitation."
 *
 * The default persistent store (Invariant 2). Implements both the
 * StorageBackend abstraction (blueprint §4.1) and the MemoryStore tier
 * interface (D-005, D-031): tier-scoped methods map to the `tier` column
 * of lina_memory_items (D-010); `status` remains the lifecycle field.
 *
 * Blueprint §4.2 declares this class inline in the .cpp; the header was
 * introduced per D-004. Fixes over the blueprint's sketch (D-030):
 * dynamic parameter arrays (the fixed-10 array overflows 18-param inserts)
 * and explicit column lists in row mapping.
 */

#include <libpq-fe.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory_module.hpp"
#include "storage_backend.hpp"

namespace lina::storage {

class PostgresBackend : public StorageBackend,
                        public memory_module::MemoryStore {
public:
    explicit PostgresBackend(const std::string& conn_string);
    ~PostgresBackend() override;

    PostgresBackend(const PostgresBackend&) = delete;
    PostgresBackend& operator=(const PostgresBackend&) = delete;

    // --- StorageBackend: Identity ---
    IdentityRecord get_identity(const std::string& user_id) override;
    void update_identity(const IdentityRecord& identity) override;
    int get_session_number(const std::string& user_id) override;

    // --- StorageBackend: Memory Vectors ---
    void store_memory_item(const memory_module::MemoryItem& item) override;
    std::optional<memory_module::MemoryItem> load_memory_item(
        const std::string& item_id) override;
    std::vector<memory_module::MemoryItemRow> fetch_memories_by_status(
        const std::string& status) override;
    std::vector<memory_module::MemoryItemRow>
    search_memories_by_ethical_vector(
        const std::vector<double>& query_vector,
        int limit = 10) override;
    void update_memory_item(const memory_module::MemoryItemRow& row) override;
    void delete_memory_item(const std::string& item_id) override;
    void log_memory_promotion(
        const std::string& user_id,
        const std::string& item_id,
        const std::string& from_stage,
        const std::string& to_stage,
        double score,
        const std::string& reason) override;

    // --- StorageBackend: Transcripts ---
    void store_transcript(const TranscriptEntry& entry) override;
    std::vector<TranscriptEntry> get_transcripts(
        const std::string& user_id, const std::string& session_id) override;

    // --- StorageBackend: Sessions ---
    void create_session(const SessionRecord& session) override;
    void finalize_session(const std::string& session_id) override;
    std::optional<SessionRecord> get_session(
        const std::string& session_id) override;

    // --- StorageBackend: Actions ---
    void store_action(const ActionRecord& action) override;
    std::optional<ActionRecord> load_action(
        const std::string& action_id) override;
    void update_action_state(
        const std::string& action_id, const std::string& state) override;
    std::vector<ActionRecord> get_pending_actions() override;

    // --- MemoryStore: tier-scoped operations (D-031) ---
    void store_tier(const std::string& tier,
                    const memory_module::MemoryItem& item) override;
    std::optional<memory_module::MemoryItem> load_tier(
        const std::string& tier, const std::string& item_id) override;
    void delete_tier(const std::string& tier,
                     const std::string& item_id) override;
    std::vector<std::pair<std::string, memory_module::MemoryItem>>
    scan_tier(const std::string& tier) override;
    bool has_tier(const std::string& tier,
                  const std::string& item_id) override;

    // --- MemoryStore: long-term operations (shared with StorageBackend) ---
    void store_long_term(const memory_module::MemoryItem& item,
                         const std::string& status) override;
    std::vector<memory_module::MemoryItemRow> fetch_by_status(
        const std::string& status) override;
    void update_item(const memory_module::MemoryItemRow& row) override;
    void delete_item(const std::string& item_id) override;
    void log_promotion(const std::string& user_id,
                       const std::string& item_id,
                       const std::string& from_stage,
                       const std::string& to_stage,
                       double score,
                       const std::string& reason) override;

private:
    std::string conn_string_;
    PGconn* conn_ = nullptr;

    void connect();
    void initialize_schema();

    PGresult* execute_query(const std::string& query,
                            const std::vector<std::string>& params = {});

    // Vector <-> PostgreSQL array text serialization.
    std::string vector_to_pgarray(const std::vector<double>& vec);
    std::vector<double> pgarray_to_vector(const char* pg_array);

    std::string now_iso();

    // Explicit-column row mapping (D-030). Column order is the shared
    // MEMORY_COLUMNS list in postgres_backend.cpp.
    memory_module::MemoryItem row_to_memory_item(PGresult* res, int row);
    memory_module::MemoryItemRow row_to_memory_item_row(PGresult* res, int row);
};

} // namespace lina::storage

#endif // LINA_POSTGRES_BACKEND_HPP
