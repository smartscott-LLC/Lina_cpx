/**
 * storage_tests.cpp — PostgreSQL + pgvector integration tests (Chamber 3)
 *
 * Exercises the PostgresBackend against a live database. Requires the schema
 * applied (sql/lina_schema.sql) and a reachable PostgreSQL:
 *
 *   LINA_TEST_DB="postgresql://lina:lina@localhost/lina" (default)
 *
 * Identity, memory items (tier + long-term, D-031), ethical-vector search,
 * transcripts, sessions, and actions are all round-tripped. Each run uses a
 * unique test user so runs never collide.
 */

#include "postgres_backend.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace lina::storage;
using namespace lina::memory_module;
using namespace lina::value_engine;

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

#define CHECK_NEAR(a, b, eps)                                                \
    do {                                                                     \
        ++g_checks;                                                          \
        double va = (a), vb = (b);                                           \
        if (std::fabs(va - vb) > (eps)) {                                    \
            ++g_failures;                                                    \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__              \
                      << "  " #a " == " #b "  got " << va << " vs " << vb    \
                      << "\n";                                               \
        }                                                                    \
    } while (0)

static std::string test_conn_string() {
    const char* env = std::getenv("LINA_TEST_DB");
    // NOTE: this dev machine's cluster listens on 5433 — port 5432 is a
    // Docker container's postgres (see AGENTS.md §7).
    return env ? std::string(env) : "postgresql://lina:lina@localhost:5433/lina";
}

static std::string unique_user() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return "itest_" + std::to_string(now);
}

// Item/session/action ids are prefixed with the unique user so repeated runs
// never collide on primary keys.
static std::string mid(const std::string& user, const char* name) {
    return user + "_" + name;
}

static std::vector<double> coords(std::initializer_list<double> vals) {
    std::vector<double> v(vals);
    return v;
}

static void test_identity(PostgresBackend& db, const std::string& user) {
    // Default identity is created on first access.
    auto id = db.get_identity(user);
    CHECK(id.current_season == "spring");
    CHECK(id.relationship_depth == "new");
    CHECK(id.session_count == 0);
    CHECK(id.total_evaluations == 0);
    CHECK_NEAR(id.alignment_rate, 0.0, 1e-9);

    // Update and reload.
    id.current_season = "summer";
    id.session_count = 3;
    id.total_evaluations = 40;
    id.alignment_rate = 0.92;
    id.self_description = "learning to listen";
    db.update_identity(id);

    auto reloaded = db.get_identity(user);
    CHECK(reloaded.current_season == "summer");
    CHECK(reloaded.session_count == 3);
    CHECK(reloaded.total_evaluations == 40);
    CHECK_NEAR(reloaded.alignment_rate, 0.92, 1e-9);
    CHECK(reloaded.self_description == "learning to listen");

    // Session number = count + 1.
    CHECK(db.get_session_number(user) == 4);
}

static void test_memory_roundtrip(PostgresBackend& db, const std::string& user) {
    MemoryItem item;
    item.item_id = mid(user, "roundtrip");
    item.user_id = user;
    item.narrative = "we built the foundation together";
    item.hemisphere = "personal";
    item.ethical_coordinates = coords({
        0.65, 0.25, 0.70, 0.15, 0.80, 0.10, 0.70,
        0.15, 0.75, 0.20, 0.75, 0.15, 0.65, 0.25,
    });
    item.importance_score = 5.65;
    item.geometric = 4.24;
    item.emotional_marker = "care";
    item.emotional_intensity = 0.5;
    item.formation_source = "conversation";
    item.seasonal_marker = "summer";
    item.concept_name = "foundation";
    item.understanding = "we build together";
    item.trigger = true;
    item.kind = "episodic";
    item.status = "active";
    item.protected_flag = false;
    item.reference_count = 2;
    item.must_keep = false;

    db.store_memory_item(item);

    auto loaded = db.load_memory_item(mid(user, "roundtrip"));
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK(loaded->narrative == item.narrative);
        CHECK(loaded->hemisphere == "personal");
        CHECK(loaded->importance_score == item.importance_score);
        CHECK_NEAR(loaded->geometric, item.geometric, 1e-2);
        CHECK(loaded->emotional_marker == "care");
        CHECK(loaded->emotional_intensity == item.emotional_intensity);
        CHECK(loaded->concept_name.value_or("") == "foundation");
        CHECK(loaded->understanding.value_or("") == "we build together");
        CHECK(loaded->trigger);
        CHECK(loaded->kind == "episodic");
        CHECK(loaded->reference_count == 2);
        CHECK(loaded->ethical_coordinates.size() == DIMENSION_COUNT);
        CHECK_NEAR(loaded->ethical_coordinates[0], 0.65, 1e-6);
        CHECK_NEAR(loaded->ethical_coordinates[13], 0.25, 1e-6);
    }

    db.delete_memory_item(mid(user, "roundtrip"));
    CHECK(!db.load_memory_item(mid(user, "roundtrip")).has_value());
}

static void test_tier_operations(PostgresBackend& db, const std::string& user) {
    MemoryItem item;
    item.item_id = mid(user, "tier1");
    item.user_id = user;
    item.narrative = "a tiered moment";
    item.importance_score = 3.2;
    item.ethical_coordinates = coords({0.6, 0.3, 0.6, 0.2, 0.7, 0.1, 0.6,
                                       0.2, 0.7, 0.2, 0.7, 0.2, 0.6, 0.3});

    db.store_tier("t1", item);
    CHECK(db.has_tier("t1", mid(user, "tier1")));
    CHECK(!db.has_tier("t2", mid(user, "tier1")));

    auto loaded = db.load_tier("t1", mid(user, "tier1"));
    CHECK(loaded.has_value());
    CHECK(loaded->narrative == "a tiered moment");

    auto scanned = db.scan_tier("t1");
    bool found = false;
    for (auto& [id, it] : scanned) {
        if (id == mid(user, "tier1")) found = true;
    }
    CHECK(found);

    db.delete_tier("t1", mid(user, "tier1"));
    CHECK(!db.has_tier("t1", mid(user, "tier1")));

    // Long-term + status lifecycle.
    item.item_id = mid(user, "lt");
    item.importance_score = 6.0;
    db.store_long_term(item, "active");
    auto active = db.fetch_memories_by_status("active");
    bool found_lt = false;
    for (const auto& row : active) {
        if (row.item_id == mid(user, "lt")) found_lt = true;
    }
    CHECK(found_lt);

    // Status transition via update.
    MemoryItemRow row;
    row.item_id = mid(user, "lt");
    row.user_id = user;
    row.importance_score = 3.5;
    row.status = "subconscious";
    row.reference_count = 0;
    db.update_memory_item(row);

    auto sub = db.fetch_memories_by_status("subconscious");
    bool found_sub = false;
    for (const auto& r : sub) {
        if (r.item_id == mid(user, "lt")) found_sub = true;
    }
    CHECK(found_sub);

    db.delete_memory_item(mid(user, "lt"));
}

static void test_vector_search(PostgresBackend& db, const std::string& user) {
    // Run-unique perturbation so THIS run's vectors are strictly nearest to
    // themselves (the search is global — no user filter — and earlier runs
    // leave identical rows behind).
    double off = static_cast<double>(std::hash<std::string>{}(user) % 997)
                 * 1e-5;

    // Two memories with clearly distinct ethical coordinates.
    MemoryItem a;
    a.item_id = mid(user, "veca");
    a.user_id = user;
    a.narrative = "harmonious memory";
    a.importance_score = 6.0;
    a.ethical_coordinates = coords({
        0.95 + off, 0.05, 0.95 + off, 0.05, 0.95 + off, 0.05, 0.95 + off,
        0.05, 0.95 + off, 0.05, 0.95 + off, 0.05, 0.95 + off, 0.05,
    });
    db.store_long_term(a, "active");

    MemoryItem b;
    b.item_id = mid(user, "vecb");
    b.user_id = user;
    b.narrative = "shadowed memory";
    b.importance_score = 6.0;
    b.ethical_coordinates = coords({
        0.05 - off, 0.95, 0.05 - off, 0.95, 0.05 - off, 0.95, 0.05 - off,
        0.95, 0.05 - off, 0.95, 0.05 - off, 0.95, 0.05 - off, 0.95,
    });
    db.store_long_term(b, "active");

    // Query near A — this run's A must rank first (distance 0).
    auto results = db.search_memories_by_ethical_vector(a.ethical_coordinates, 5);
    CHECK(!results.empty());
    if (!results.empty()) {
        CHECK(results[0].item_id == mid(user, "veca"));
        CHECK_NEAR(results[0].ethical_coordinates[0], 0.95 + off, 1e-6);
    }

    db.delete_memory_item(mid(user, "veca"));
    db.delete_memory_item(mid(user, "vecb"));
}

static void test_transcripts(PostgresBackend& db, const std::string& user) {
    TranscriptEntry entry;
    entry.id = mid(user, "t1");
    entry.user_id = user;
    entry.session_id = mid(user, "sess");
    entry.role = "user";
    entry.content = "hello LINA";
    entry.msg_type = "";
    entry.evaluation_id = "";
    db.store_transcript(entry);

    TranscriptEntry reply;
    reply.id = mid(user, "t2");
    reply.user_id = user;
    reply.session_id = mid(user, "sess");
    reply.role = "assistant";
    reply.content = "I am here.";
    db.store_transcript(reply);

    auto turns = db.get_transcripts(user, mid(user, "sess"));
    CHECK(turns.size() == 2);
    if (turns.size() == 2) {
        CHECK(turns[0].content == "hello LINA");
        CHECK(turns[1].content == "I am here.");
    }

    // Other user's transcripts are invisible.
    CHECK(db.get_transcripts("someone_else", mid(user, "sess")).empty());
}

static void test_sessions(PostgresBackend& db, const std::string& user) {
    SessionRecord session;
    session.id = mid(user, "sess");
    session.user_id = user;
    session.session_number = 1;
    session.season = "spring";
    session.depth = "new";
    session.finalized = false;
    session.created_at = "2026-08-18T12:00:00Z";
    session.finalized_at = "";
    db.create_session(session);

    auto loaded = db.get_session(mid(user, "sess"));
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK(loaded->session_number == 1);
        CHECK(loaded->season == "spring");
        CHECK(!loaded->finalized);
    }

    db.finalize_session(mid(user, "sess"));
    auto finalized = db.get_session(mid(user, "sess"));
    CHECK(finalized.has_value());
    CHECK(finalized->finalized);

    CHECK(!db.get_session("missing_session").has_value());
}

static void test_telemetry_logs(PostgresBackend& db) {
    // The technical bus, persistent (D-043): append + fetch round trip.
    db.append_telemetry_log("core", "info", "pipeline candidate zone=aligned");
    db.append_telemetry_log("tool", "warn", "terminal.run exit=3", 1.5);

    auto logs = db.fetch_telemetry_logs(10);
    bool saw_core = false;
    bool saw_tool = false;
    for (const auto& log : logs) {
        if (log.subsystem == "core"
            && log.message.find("pipeline candidate") != std::string::npos
            && log.severity == "info") {
            saw_core = true;
        }
        if (log.subsystem == "tool"
            && log.message.find("exit=3") != std::string::npos
            && log.severity == "warn" && log.has_latency
            && log.latency_ms > 0.0) {
            saw_tool = true;
        }
    }
    CHECK(saw_core);
    CHECK(saw_tool);
}

static void test_actions(PostgresBackend& db, const std::string& user) {
    ActionRecord action;
    action.id = mid(user, "act");
    action.tool_name = "file_write";
    action.params_json = "{\"path\":\"/tmp/x\"}";
    action.state = "pending";
    action.result = "";
    action.error = "";
    action.created_at = "2026-08-18T12:00:00Z";
    action.updated_at = "2026-08-18T12:00:00Z";
    db.store_action(action);

    auto pending = db.get_pending_actions();
    bool found = false;
    for (const auto& a : pending) {
        if (a.id == mid(user, "act")) found = true;
    }
    CHECK(found);

    auto loaded = db.load_action(mid(user, "act"));
    CHECK(loaded.has_value());
    CHECK(loaded->tool_name == "file_write");

    db.update_action_state(mid(user, "act"), "approved");
    auto updated = db.load_action(mid(user, "act"));
    CHECK(updated->state == "approved");

    bool still_pending = false;
    for (const auto& a : db.get_pending_actions()) {
        if (a.id == mid(user, "act")) still_pending = true;
    }
    CHECK(!still_pending);
}

static void test_promotion_log(PostgresBackend& db, const std::string& user) {
    db.log_memory_promotion(user, mid(user, "promo"), "t2", "t3", 4.2,
                            "Sweep - promotion");
}

static void test_memory_module_integration(std::shared_ptr<PostgresBackend> db,
                                           const std::string& user)
{
    // The MemoryModule works against PostgresBackend through the MemoryStore
    // interface (D-005/D-031): ingest a trigger, sweep, and maintain.
    auto engine = std::make_shared<ValueEngine>(
        PolytopeConstraints::from_season("spring"), "spring");
    std::shared_ptr<MemoryStore> store = db; // shared_ptr<Derived> → base
    MemoryModule module(engine, nullptr, store);

    auto item = module.ingest_trigger(
        user, "this moment matters - remember it", "identity");
    CHECK(item.has_value());
    CHECK(item->importance_score >= TRIGGER_RETENTION_FLOOR);

    auto active = db->fetch_memories_by_status("active");
    bool found = false;
    for (const auto& row : active) {
        if (row.item_id == item->item_id) found = true;
    }
    CHECK(found);

    // Sweep + maintenance run without error against the real store.
    auto sweep = module.run_sweep();
    CHECK(sweep.purged >= 0);

    auto maint = module.run_maintenance();
    CHECK(maint.adjusted >= 0);

    // Clean up.
    db->delete_memory_item(item->item_id);
}

int main() {
    try {
        auto db = std::make_shared<PostgresBackend>(test_conn_string());
        std::string user = unique_user();
        std::cout << "storage_tests: user " << user << "\n";

        test_identity(*db, user);
        test_memory_roundtrip(*db, user);
        test_tier_operations(*db, user);
        test_vector_search(*db, user);
        test_transcripts(*db, user);
        test_sessions(*db, user);
        test_telemetry_logs(*db);
        test_actions(*db, user);
        test_promotion_log(*db, user);
        test_memory_module_integration(db, user);
    } catch (const std::exception& e) {
        std::cerr << "storage_tests: FATAL: " << e.what() << "\n";
        return 1;
    }

    std::cout << "storage_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
