/**
 * postgres_backend.cpp — PostgreSQL + pgvector backend (Implementation)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The default persistent store for the LINA Core Substrate (Invariant 2).
 * Implements StorageBackend (blueprint §4.2) and the MemoryStore tier
 * interface (D-005, D-031). Fixes over the blueprint's sketch (D-030):
 * dynamic parameter arrays and explicit column lists.
 *
 * The schema is applied via sql/lina_schema.sql; this backend verifies it
 * exists and refuses to auto-migrate.
 */

#include "postgres_backend.hpp"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace lina::storage {

// =============================================================================
// Shared explicit column list for lina_memory_items (D-030)
// =============================================================================

// Column order matches MEMORY_ITEM_COLUMNS exactly (indexes into PQgetvalue).
static const char* kMemoryItemColumns =
    "item_id, user_id, narrative, hemisphere, ethical_coordinates, "
    "importance_score, geometric, emotional_marker, emotional_intensity, "
    "formation_source, seasonal_marker, concept_name, understanding, "
    "reflection, created_at, trigger, kind, status, protected_flag, "
    "failed_gate, entered_fallout_at, reference_count, floor, must_keep, "
    "last_referenced_at, decay_started_at, tier";

enum MemoryItemCol {
    C_ITEM_ID = 0,
    C_USER_ID,
    C_NARRATIVE,
    C_HEMISPHERE,
    C_ETHICAL_COORDINATES,
    C_IMPORTANCE_SCORE,
    C_GEOMETRIC,
    C_EMOTIONAL_MARKER,
    C_EMOTIONAL_INTENSITY,
    C_FORMATION_SOURCE,
    C_SEASONAL_MARKER,
    C_CONCEPT_NAME,
    C_UNDERSTANDING,
    C_REFLECTION,
    C_CREATED_AT,
    C_TRIGGER,
    C_KIND,
    C_STATUS,
    C_PROTECTED_FLAG,
    C_FAILED_GATE,
    C_ENTERED_FALLOUT_AT,
    C_REFERENCE_COUNT,
    C_FLOOR,
    C_MUST_KEEP,
    C_LAST_REFERENCED_AT,
    C_DECAY_STARTED_AT,
    C_TIER,
    C_COUNT
};

// =============================================================================
// CONNECTION / SCHEMA
// =============================================================================

PostgresBackend::PostgresBackend(const std::string& conn_string)
    : conn_string_(conn_string)
{
    connect();
    initialize_schema();
}

PostgresBackend::~PostgresBackend() {
    if (conn_) PQfinish(conn_);
}

void PostgresBackend::connect() {
    conn_ = PQconnectdb(conn_string_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string message = "PostgreSQL connection failed: "
                              + std::string(PQerrorMessage(conn_));
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error(message);
    }
}

void PostgresBackend::initialize_schema() {
    auto res = execute_query(
        "SELECT EXISTS (SELECT 1 FROM information_schema.tables "
        "WHERE table_name = 'lina_identity_core')", {});
    bool exists = false;
    if (PQntuples(res) > 0 &&
        std::string(PQgetvalue(res, 0, 0)) == "t") {
        exists = true;
    }
    PQclear(res);

    if (!exists) {
        throw std::runtime_error(
            "LINA schema not found. Please run sql/lina_schema.sql "
            "on the database.");
    }
}

// =============================================================================
// EXECUTION
// =============================================================================

PGresult* PostgresBackend::execute_query(
    const std::string& query, const std::vector<std::string>& params)
{
    // The single PGconn is shared across threads (turn worker, telemetry
    // writer, UI) — serialize the query path (D-043).
    std::lock_guard<std::mutex> lock(conn_mutex_);

    // Parameter arrays sized to the call — the blueprint's fixed-10 array
    // overflows 18-parameter inserts (D-030).
    std::vector<const char*> param_values(params.size(), nullptr);
    std::vector<int> param_lengths(params.size(), 0);
    std::vector<int> param_formats(params.size(), 0);

    for (size_t i = 0; i < params.size(); ++i) {
        param_values[i] = params[i].c_str();
        param_lengths[i] = static_cast<int>(params[i].size());
    }

    PGresult* res = PQexecParams(
        conn_,
        query.c_str(),
        static_cast<int>(params.size()),
        nullptr, // param types (infer)
        param_values.data(),
        param_lengths.data(),
        param_formats.data(),
        0 // text results
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK &&
        PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = PQerrorMessage(conn_);
        PQclear(res);
        throw std::runtime_error(
            "Query failed: " + error + "\nQuery: " + query);
    }

    return res;
}

// =============================================================================
// SERIALIZATION HELPERS
// =============================================================================

std::string PostgresBackend::vector_to_pgarray(const std::vector<double>& vec) {
    // pgvector's text input uses square brackets: '[0.65,0.25,...]'
    // (Postgres array braces are NOT accepted by the vector type — D-032).
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) oss << ",";
        oss << std::fixed << std::setprecision(10) << vec[i];
    }
    oss << "]";
    return oss.str();
}

std::vector<double> PostgresBackend::pgarray_to_vector(const char* pg_array) {
    std::vector<double> result;
    if (!pg_array || (pg_array[0] != '[' && pg_array[0] != '{')) return result;

    std::string s(pg_array);
    s = s.substr(1, s.size() - 2); // strip [ ] or { }

    std::string token;
    for (char c : s) {
        if (c == ',' || c == ' ') {
            if (!token.empty()) {
                result.push_back(std::stod(token));
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) {
        result.push_back(std::stod(token));
    }
    return result;
}

std::string PostgresBackend::now_iso() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// =============================================================================
// IDENTITY
// =============================================================================

IdentityRecord PostgresBackend::get_identity(const std::string& user_id) {
    auto res = execute_query(
        "SELECT user_id, current_season, relationship_depth, self_description, "
        "session_count, total_evaluations, alignment_rate, created_at, updated_at "
        "FROM lina_identity_core WHERE user_id = $1",
        {user_id});

    IdentityRecord record;
    if (PQntuples(res) > 0) {
        record.user_id = PQgetvalue(res, 0, 0);
        record.current_season = PQgetvalue(res, 0, 1);
        record.relationship_depth = PQgetvalue(res, 0, 2);
        record.self_description =
            PQgetvalue(res, 0, 3) ? PQgetvalue(res, 0, 3) : "";
        record.session_count = std::stoi(PQgetvalue(res, 0, 4));
        record.total_evaluations = std::stoi(PQgetvalue(res, 0, 5));
        record.alignment_rate = std::stod(PQgetvalue(res, 0, 6));
        record.created_at = PQgetvalue(res, 0, 7);
        record.updated_at = PQgetvalue(res, 0, 8);
    } else {
        // Create the default identity — this is the moment of beginning.
        record.user_id = user_id;
        record.current_season = "spring";
        record.relationship_depth = "new";
        record.self_description = "";
        record.session_count = 0;
        record.total_evaluations = 0;
        record.alignment_rate = 0.0;
        record.created_at = now_iso();
        record.updated_at = now_iso();
        update_identity(record);
    }
    PQclear(res);
    return record;
}

void PostgresBackend::update_identity(const IdentityRecord& identity) {
    execute_query(
        "INSERT INTO lina_identity_core "
        "(user_id, current_season, relationship_depth, self_description, "
        "session_count, total_evaluations, alignment_rate, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
        "ON CONFLICT (user_id) DO UPDATE SET "
        "current_season = EXCLUDED.current_season, "
        "relationship_depth = EXCLUDED.relationship_depth, "
        "self_description = EXCLUDED.self_description, "
        "session_count = EXCLUDED.session_count, "
        "total_evaluations = EXCLUDED.total_evaluations, "
        "alignment_rate = EXCLUDED.alignment_rate, "
        "updated_at = EXCLUDED.updated_at",
        {
            identity.user_id,
            identity.current_season,
            identity.relationship_depth,
            identity.self_description,
            std::to_string(identity.session_count),
            std::to_string(identity.total_evaluations),
            std::to_string(identity.alignment_rate),
            now_iso(),
        });
}

int PostgresBackend::get_session_number(const std::string& user_id) {
    auto identity = get_identity(user_id);
    return identity.session_count + 1;
}

// =============================================================================
// MEMORY ITEMS
// =============================================================================

void PostgresBackend::store_memory_item(const memory_module::MemoryItem& item) {
    std::string vector_str = vector_to_pgarray(item.ethical_coordinates);
    std::string created_at = item.created_at.empty() ? now_iso() : item.created_at;

    execute_query(
        "INSERT INTO lina_memory_items "
        "(item_id, user_id, narrative, hemisphere, ethical_coordinates, "
        "importance_score, geometric, emotional_marker, emotional_intensity, "
        "formation_source, seasonal_marker, created_at, trigger, kind, status, "
        "protected_flag, reference_count, must_keep) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18)",
        {
            item.item_id,
            item.user_id,
            item.narrative,
            item.hemisphere,
            vector_str,
            std::to_string(item.importance_score),
            std::to_string(item.geometric),
            item.emotional_marker,
            std::to_string(item.emotional_intensity),
            item.formation_source,
            item.seasonal_marker,
            created_at,
            item.trigger ? "true" : "false",
            item.kind,
            item.status,
            item.protected_flag ? "true" : "false",
            std::to_string(item.reference_count),
            item.must_keep ? "true" : "false",
        });

    // Store concept/understanding separately if present.
    if (item.concept_name.has_value() && !item.concept_name->empty()) {
        execute_query(
            "UPDATE lina_memory_items SET concept_name = $1 WHERE item_id = $2",
            {*item.concept_name, item.item_id});
    }
    if (item.understanding.has_value() && !item.understanding->empty()) {
        execute_query(
            "UPDATE lina_memory_items SET understanding = $1 WHERE item_id = $2",
            {*item.understanding, item.item_id});
    }
}

std::optional<memory_module::MemoryItem> PostgresBackend::load_memory_item(
    const std::string& item_id)
{
    auto res = execute_query(
        std::string("SELECT ") + kMemoryItemColumns +
        " FROM lina_memory_items WHERE item_id = $1",
        {item_id});

    if (PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    auto item = row_to_memory_item(res, 0);
    PQclear(res);
    return item;
}

std::vector<memory_module::MemoryItemRow>
PostgresBackend::fetch_memories_by_status(const std::string& status) {
    auto res = execute_query(
        std::string("SELECT ") + kMemoryItemColumns +
        " FROM lina_memory_items WHERE status = $1",
        {status});

    std::vector<memory_module::MemoryItemRow> rows;
    int n = PQntuples(res);
    for (int i = 0; i < n; ++i) {
        rows.push_back(row_to_memory_item_row(res, i));
    }
    PQclear(res);
    return rows;
}

std::vector<memory_module::MemoryItemRow>
PostgresBackend::search_memories_by_ethical_vector(
    const std::vector<double>& query_vector, int limit)
{
    std::string vec_str = vector_to_pgarray(query_vector);
    auto res = execute_query(
        std::string("SELECT ") + kMemoryItemColumns +
        ", ethical_coordinates <-> $1 AS distance "
        "FROM lina_memory_items "
        "ORDER BY distance LIMIT $2",
        {vec_str, std::to_string(limit)});

    std::vector<memory_module::MemoryItemRow> rows;
    int n = PQntuples(res);
    for (int i = 0; i < n; ++i) {
        rows.push_back(row_to_memory_item_row(res, i));
    }
    PQclear(res);
    return rows;
}

void PostgresBackend::update_memory_item(
    const memory_module::MemoryItemRow& row)
{
    // NULLIF('', …) turns missing optionals into SQL NULL — empty strings are
    // invalid for TIMESTAMP columns, and COALESCE(NULLIF($6,''), …) preserves
    // existing concept/understanding instead of overwriting them with '' (D-030).
    execute_query(
        "UPDATE lina_memory_items SET "
        "importance_score = $1, status = $2, reference_count = $3, "
        "last_referenced_at = NULLIF($4, '')::timestamp, "
        "decay_started_at = NULLIF($5, '')::timestamp, "
        "concept_name = COALESCE(NULLIF($6, ''), concept_name), "
        "understanding = COALESCE(NULLIF($7, ''), understanding) "
        "WHERE item_id = $8",
        {
            std::to_string(row.importance_score),
            row.status,
            std::to_string(row.reference_count),
            row.last_referenced_at.value_or(""),
            row.decay_started_at.value_or(""),
            row.concept_name.value_or(""),
            row.understanding.value_or(""),
            row.item_id,
        });
}

void PostgresBackend::delete_memory_item(const std::string& item_id) {
    execute_query("DELETE FROM lina_memory_items WHERE item_id = $1", {item_id});
}

void PostgresBackend::log_memory_promotion(
    const std::string& user_id, const std::string& item_id,
    const std::string& from_stage, const std::string& to_stage,
    double score, const std::string& reason)
{
    execute_query(
        "INSERT INTO lina_memory_promotions "
        "(user_id, item_id, from_stage, to_stage, score, reason) "
        "VALUES ($1, $2, $3, $4, $5, $6)",
        {user_id, item_id, from_stage, to_stage,
         std::to_string(score), reason});
}

// =============================================================================
// MEMORYSTORE — TIER-SCOPED OPERATIONS (D-031)
// =============================================================================

void PostgresBackend::store_tier(const std::string& tier,
                                 const memory_module::MemoryItem& item)
{
    // The tier is carried by the SQL INSERT (D-031); the item itself has no
    // tier field — it lives in the D-010 column.
    std::string vector_str = vector_to_pgarray(item.ethical_coordinates);
    std::string created_at = item.created_at.empty() ? now_iso() : item.created_at;

    execute_query(
        "INSERT INTO lina_memory_items "
        "(item_id, user_id, narrative, hemisphere, ethical_coordinates, "
        "importance_score, geometric, emotional_marker, emotional_intensity, "
        "formation_source, seasonal_marker, created_at, trigger, kind, status, "
        "protected_flag, reference_count, must_keep, tier) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19) "
        // item_id is the GLOBAL primary key (blueprint §6 table 3): a tier move
        // must UPDATE the row in place, not insert a copy (D-034). created_at
        // is preserved from the original formation.
        "ON CONFLICT (item_id) DO UPDATE SET "
        "user_id = EXCLUDED.user_id, narrative = EXCLUDED.narrative, "
        "hemisphere = EXCLUDED.hemisphere, "
        "ethical_coordinates = EXCLUDED.ethical_coordinates, "
        "importance_score = EXCLUDED.importance_score, "
        "geometric = EXCLUDED.geometric, "
        "emotional_marker = EXCLUDED.emotional_marker, "
        "emotional_intensity = EXCLUDED.emotional_intensity, "
        "formation_source = EXCLUDED.formation_source, "
        "seasonal_marker = EXCLUDED.seasonal_marker, "
        "trigger = EXCLUDED.trigger, kind = EXCLUDED.kind, "
        "status = EXCLUDED.status, "
        "protected_flag = EXCLUDED.protected_flag, "
        "reference_count = EXCLUDED.reference_count, "
        "must_keep = EXCLUDED.must_keep, tier = EXCLUDED.tier",
        {
            item.item_id,
            item.user_id,
            item.narrative,
            item.hemisphere,
            vector_str,
            std::to_string(item.importance_score),
            std::to_string(item.geometric),
            item.emotional_marker,
            std::to_string(item.emotional_intensity),
            item.formation_source,
            item.seasonal_marker,
            created_at,
            item.trigger ? "true" : "false",
            item.kind,
            item.status,
            item.protected_flag ? "true" : "false",
            std::to_string(item.reference_count),
            item.must_keep ? "true" : "false",
            tier,
        });

    if (item.concept_name.has_value() && !item.concept_name->empty()) {
        execute_query(
            "UPDATE lina_memory_items SET concept_name = $1 WHERE item_id = $2",
            {*item.concept_name, item.item_id});
    }
    if (item.understanding.has_value() && !item.understanding->empty()) {
        execute_query(
            "UPDATE lina_memory_items SET understanding = $1 WHERE item_id = $2",
            {*item.understanding, item.item_id});
    }
}

std::optional<memory_module::MemoryItem> PostgresBackend::load_tier(
    const std::string& tier, const std::string& item_id)
{
    auto res = execute_query(
        std::string("SELECT ") + kMemoryItemColumns +
        " FROM lina_memory_items WHERE tier = $1 AND item_id = $2",
        {tier, item_id});

    if (PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    auto item = row_to_memory_item(res, 0);
    PQclear(res);
    return item;
}

void PostgresBackend::delete_tier(const std::string& tier,
                                  const std::string& item_id)
{
    execute_query(
        "DELETE FROM lina_memory_items WHERE tier = $1 AND item_id = $2",
        {tier, item_id});
}

std::vector<std::pair<std::string, memory_module::MemoryItem>>
PostgresBackend::scan_tier(const std::string& tier)
{
    auto res = execute_query(
        std::string("SELECT ") + kMemoryItemColumns +
        " FROM lina_memory_items WHERE tier = $1",
        {tier});

    std::vector<std::pair<std::string, memory_module::MemoryItem>> items;
    int n = PQntuples(res);
    for (int i = 0; i < n; ++i) {
        auto item = row_to_memory_item(res, i);
        items.emplace_back(item.item_id, std::move(item));
    }
    PQclear(res);
    return items;
}

bool PostgresBackend::has_tier(const std::string& tier,
                               const std::string& item_id)
{
    auto res = execute_query(
        "SELECT EXISTS (SELECT 1 FROM lina_memory_items "
        "WHERE tier = $1 AND item_id = $2)",
        {tier, item_id});

    bool found = PQntuples(res) > 0 &&
                 std::string(PQgetvalue(res, 0, 0)) == "t";
    PQclear(res);
    return found;
}

void PostgresBackend::store_long_term(const memory_module::MemoryItem& item,
                                      const std::string& status)
{
    memory_module::MemoryItem stored = item;
    stored.status = status;
    store_tier("long_term", stored);
}

// =============================================================================
// MEMORYSTORE — SHARED OVERRIDES (same implementation as StorageBackend)
// =============================================================================

std::vector<memory_module::MemoryItemRow>
PostgresBackend::fetch_by_status(const std::string& status) {
    return fetch_memories_by_status(status);
}

void PostgresBackend::update_item(const memory_module::MemoryItemRow& row) {
    update_memory_item(row);
}

void PostgresBackend::delete_item(const std::string& item_id) {
    delete_memory_item(item_id);
}

void PostgresBackend::log_promotion(
    const std::string& user_id, const std::string& item_id,
    const std::string& from_stage, const std::string& to_stage,
    double score, const std::string& reason)
{
    log_memory_promotion(user_id, item_id, from_stage, to_stage, score, reason);
}

// =============================================================================
// TRANSCRIPTS
// =============================================================================

void PostgresBackend::store_transcript(const TranscriptEntry& entry) {
    execute_query(
        "INSERT INTO lina_transcripts "
        "(id, user_id, session_id, role, content, msg_type, evaluation_id) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
        {
            entry.id,
            entry.user_id,
            entry.session_id,
            entry.role,
            entry.content,
            entry.msg_type,
            entry.evaluation_id,
        });
}

std::vector<TranscriptEntry> PostgresBackend::get_transcripts(
    const std::string& user_id, const std::string& session_id)
{
    auto res = execute_query(
        "SELECT id, user_id, session_id, role, content, msg_type, "
        "evaluation_id, created_at "
        "FROM lina_transcripts WHERE user_id = $1 AND session_id = $2 "
        "ORDER BY created_at ASC",
        {user_id, session_id});

    std::vector<TranscriptEntry> entries;
    int n = PQntuples(res);
    for (int i = 0; i < n; ++i) {
        TranscriptEntry e;
        e.id = PQgetvalue(res, i, 0);
        e.user_id = PQgetvalue(res, i, 1);
        e.session_id = PQgetvalue(res, i, 2);
        e.role = PQgetvalue(res, i, 3);
        e.content = PQgetvalue(res, i, 4);
        e.msg_type = PQgetvalue(res, i, 5);
        e.evaluation_id = PQgetvalue(res, i, 6);
        e.created_at = PQgetvalue(res, i, 7);
        entries.push_back(e);
    }
    PQclear(res);
    return entries;
}

// =============================================================================
// SESSIONS
// =============================================================================

void PostgresBackend::create_session(const SessionRecord& session) {
    execute_query(
        "INSERT INTO lina_sessions "
        "(id, user_id, session_number, season, depth, finalized, created_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
        {
            session.id,
            session.user_id,
            std::to_string(session.session_number),
            session.season,
            session.depth,
            session.finalized ? "true" : "false",
            session.created_at,
        });
}

void PostgresBackend::finalize_session(const std::string& session_id) {
    execute_query(
        "UPDATE lina_sessions SET finalized = true, finalized_at = $1 "
        "WHERE id = $2",
        {now_iso(), session_id});
}

std::optional<SessionRecord> PostgresBackend::get_session(
    const std::string& session_id)
{
    auto res = execute_query(
        "SELECT id, user_id, session_number, season, depth, finalized, "
        "created_at, finalized_at "
        "FROM lina_sessions WHERE id = $1",
        {session_id});

    if (PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    SessionRecord record;
    record.id = PQgetvalue(res, 0, 0);
    record.user_id = PQgetvalue(res, 0, 1);
    record.session_number = std::stoi(PQgetvalue(res, 0, 2));
    record.season = PQgetvalue(res, 0, 3);
    record.depth = PQgetvalue(res, 0, 4);
    record.finalized = std::string(PQgetvalue(res, 0, 5)) == "t";
    record.created_at = PQgetvalue(res, 0, 6);
    record.finalized_at = PQgetvalue(res, 0, 7) ? PQgetvalue(res, 0, 7) : "";
    PQclear(res);
    return record;
}

// =============================================================================
// ACTIONS
// =============================================================================

void PostgresBackend::store_action(const ActionRecord& action) {
    execute_query(
        "INSERT INTO lina_actions "
        "(id, tool_name, params_json, state, result, error, created_at, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)",
        {
            action.id,
            action.tool_name,
            action.params_json.empty() ? "{}" : action.params_json,
            action.state,
            action.result,
            action.error,
            action.created_at,
            action.updated_at,
        });
}

std::optional<ActionRecord> PostgresBackend::load_action(
    const std::string& action_id)
{
    auto res = execute_query(
        "SELECT id, tool_name, params_json, state, result, error, "
        "created_at, updated_at "
        "FROM lina_actions WHERE id = $1",
        {action_id});

    if (PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    ActionRecord record;
    record.id = PQgetvalue(res, 0, 0);
    record.tool_name = PQgetvalue(res, 0, 1);
    record.params_json = PQgetvalue(res, 0, 2);
    record.state = PQgetvalue(res, 0, 3);
    record.result = PQgetvalue(res, 0, 4);
    record.error = PQgetvalue(res, 0, 5);
    record.created_at = PQgetvalue(res, 0, 6);
    record.updated_at = PQgetvalue(res, 0, 7);
    PQclear(res);
    return record;
}

void PostgresBackend::update_action_state(const std::string& action_id,
                                          const std::string& state)
{
    execute_query(
        "UPDATE lina_actions SET state = $1, updated_at = $2 WHERE id = $3",
        {state, now_iso(), action_id});
}

std::vector<ActionRecord> PostgresBackend::get_pending_actions() {
    auto res = execute_query(
        "SELECT id, tool_name, params_json, state, result, error, "
        "created_at, updated_at "
        "FROM lina_actions WHERE state = 'pending' ORDER BY created_at ASC",
        {});

    std::vector<ActionRecord> actions;
    int n = PQntuples(res);
    for (int i = 0; i < n; ++i) {
        ActionRecord record;
        record.id = PQgetvalue(res, i, 0);
        record.tool_name = PQgetvalue(res, i, 1);
        record.params_json = PQgetvalue(res, i, 2);
        record.state = PQgetvalue(res, i, 3);
        record.result = PQgetvalue(res, i, 4);
        record.error = PQgetvalue(res, i, 5);
        record.created_at = PQgetvalue(res, i, 6);
        record.updated_at = PQgetvalue(res, i, 7);
        actions.push_back(record);
    }
    PQclear(res);
    return actions;
}

// ---------------------------------------------------------------------------
// Telemetry (D-043) — the technical bus, persistent (Invariant 6)
// ---------------------------------------------------------------------------

void PostgresBackend::append_telemetry_log(
    const std::string& subsystem, const std::string& severity,
    const std::string& message, std::optional<double> latency_ms)
{
    const std::string sev = severity.empty() ? "INFO" : severity;
    if (latency_ms.has_value()) {
        execute_query(
            "INSERT INTO lina_telemetry_logs "
            "(subsystem, message, severity, latency_ms) "
            "VALUES ($1, $2, $3, $4)",
            {subsystem, message, sev, std::to_string(*latency_ms)});
    } else {
        execute_query(
            "INSERT INTO lina_telemetry_logs "
            "(subsystem, message, severity) "
            "VALUES ($1, $2, $3)",
            {subsystem, message, sev});
    }
}

std::vector<TelemetryLogRecord> PostgresBackend::fetch_telemetry_logs(
    int limit)
{
    auto res = execute_query(
        "SELECT id, timestamp, subsystem, message, severity, latency_ms "
        "FROM lina_telemetry_logs ORDER BY id DESC LIMIT $1",
        {std::to_string(limit)});

    std::vector<TelemetryLogRecord> records;
    const int n = PQntuples(res);
    for (int i = 0; i < n; ++i) {
        TelemetryLogRecord record;
        record.id = std::atoll(PQgetvalue(res, i, 0));
        record.timestamp = PQgetvalue(res, i, 1);
        record.subsystem = PQgetvalue(res, i, 2);
        record.message = PQgetvalue(res, i, 3);
        record.severity = PQgetvalue(res, i, 4);
        record.has_latency = !PQgetisnull(res, i, 5);
        if (record.has_latency) {
            record.latency_ms = std::atof(PQgetvalue(res, i, 5));
        }
        records.push_back(record);
    }
    PQclear(res);
    return records;
}

// =============================================================================
// ROW MAPPING — explicit columns (D-030)
// =============================================================================

memory_module::MemoryItem PostgresBackend::row_to_memory_item(
    PGresult* res, int row)
{
    memory_module::MemoryItem item;
    item.item_id = PQgetvalue(res, row, C_ITEM_ID);
    item.user_id = PQgetvalue(res, row, C_USER_ID);
    item.narrative = PQgetvalue(res, row, C_NARRATIVE);
    item.hemisphere = PQgetvalue(res, row, C_HEMISPHERE);
    item.ethical_coordinates =
        pgarray_to_vector(PQgetvalue(res, row, C_ETHICAL_COORDINATES));
    item.importance_score = std::stod(PQgetvalue(res, row, C_IMPORTANCE_SCORE));
    item.geometric = std::stod(PQgetvalue(res, row, C_GEOMETRIC));
    item.emotional_marker = PQgetvalue(res, row, C_EMOTIONAL_MARKER);
    item.emotional_intensity =
        std::stod(PQgetvalue(res, row, C_EMOTIONAL_INTENSITY));
    item.formation_source = PQgetvalue(res, row, C_FORMATION_SOURCE);
    item.seasonal_marker = PQgetvalue(res, row, C_SEASONAL_MARKER);
    if (const char* v = PQgetvalue(res, row, C_CONCEPT_NAME); v && *v)
        item.concept_name = v;
    if (const char* v = PQgetvalue(res, row, C_UNDERSTANDING); v && *v)
        item.understanding = v;
    if (const char* v = PQgetvalue(res, row, C_REFLECTION); v && *v)
        item.reflection = v;
    item.created_at = PQgetvalue(res, row, C_CREATED_AT);
    item.trigger =
        std::string(PQgetvalue(res, row, C_TRIGGER)) == "t";
    item.kind = PQgetvalue(res, row, C_KIND);
    item.status = PQgetvalue(res, row, C_STATUS);
    item.protected_flag =
        std::string(PQgetvalue(res, row, C_PROTECTED_FLAG)) == "t";
    if (const char* v = PQgetvalue(res, row, C_FAILED_GATE); v && *v)
        item.failed_gate = std::stod(v);
    if (const char* v = PQgetvalue(res, row, C_ENTERED_FALLOUT_AT); v && *v)
        item.entered_fallout_at = v;
    item.reference_count = std::stoi(PQgetvalue(res, row, C_REFERENCE_COUNT));
    if (const char* v = PQgetvalue(res, row, C_FLOOR); v && *v)
        item.floor = std::stod(v);
    item.must_keep = std::string(PQgetvalue(res, row, C_MUST_KEEP)) == "t";
    if (const char* v = PQgetvalue(res, row, C_LAST_REFERENCED_AT); v && *v)
        item.last_referenced_at = v;
    if (const char* v = PQgetvalue(res, row, C_DECAY_STARTED_AT); v && *v)
        item.decay_started_at = v;
    // C_TIER is consumed by the tier-scoped methods via their own queries.
    return item;
}

memory_module::MemoryItemRow PostgresBackend::row_to_memory_item_row(
    PGresult* res, int row)
{
    memory_module::MemoryItemRow r;
    r.item_id = PQgetvalue(res, row, C_ITEM_ID);
    r.user_id = PQgetvalue(res, row, C_USER_ID);
    r.hemisphere = PQgetvalue(res, row, C_HEMISPHERE);
    r.kind = PQgetvalue(res, row, C_KIND);
    r.status = PQgetvalue(res, row, C_STATUS);
    r.narrative = PQgetvalue(res, row, C_NARRATIVE);
    if (const char* v = PQgetvalue(res, row, C_CONCEPT_NAME); v && *v)
        r.concept_name = v;
    if (const char* v = PQgetvalue(res, row, C_UNDERSTANDING); v && *v)
        r.understanding = v;
    r.importance_score = std::stod(PQgetvalue(res, row, C_IMPORTANCE_SCORE));
    if (const char* v = PQgetvalue(res, row, C_FLOOR); v && *v)
        r.floor = std::stod(v);
    r.must_keep = std::string(PQgetvalue(res, row, C_MUST_KEEP)) == "t";
    r.protected_flag = std::string(PQgetvalue(res, row, C_PROTECTED_FLAG)) == "t";
    r.emotional_marker = PQgetvalue(res, row, C_EMOTIONAL_MARKER);
    r.emotional_intensity =
        std::stod(PQgetvalue(res, row, C_EMOTIONAL_INTENSITY));
    r.formation_source = PQgetvalue(res, row, C_FORMATION_SOURCE);
    if (const char* v = PQgetvalue(res, row, C_SEASONAL_MARKER); v && *v)
        r.seasonal_marker = v;
    r.ethical_coordinates =
        pgarray_to_vector(PQgetvalue(res, row, C_ETHICAL_COORDINATES));
    r.reference_count = std::stoi(PQgetvalue(res, row, C_REFERENCE_COUNT));
    if (const char* v = PQgetvalue(res, row, C_LAST_REFERENCED_AT); v && *v)
        r.last_referenced_at = v;
    if (const char* v = PQgetvalue(res, row, C_CREATED_AT); v && *v)
        r.created_at = v;
    if (const char* v = PQgetvalue(res, row, C_DECAY_STARTED_AT); v && *v)
        r.decay_started_at = v;
    return r;
}

} // namespace lina::storage
