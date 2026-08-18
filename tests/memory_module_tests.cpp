/**
 * memory_module_tests.cpp — unit tests for Chamber 2 (Memory Imprint System)
 *
 * The MPS lifecycle is deterministic by contract: routing, sweep promotion,
 * the 48-hour fallout grace (D-027), monthly maintenance, the subconscious
 * slope, the legacy review, and recall blending. Test doubles (in-memory
 * store, fake embedding engine) live here, not in production headers (D-022).
 */

#include "memory_module.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace lina::memory_module;
using namespace lina::value_engine;
using Clock = std::chrono::system_clock;
using TimePoint = std::chrono::system_clock::time_point;

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

// =============================================================================
// TEST DOUBLES (D-022 — test-only)
// =============================================================================

// Deterministic fake embedding engine: hash-based, 14 dims.
class TestEmbeddingEngine : public EmbeddingEngine {
public:
    explicit TestEmbeddingEngine(int dims = 14) : dims_(dims) {}
    std::optional<std::vector<double>> embed(const std::string& text) override {
        std::vector<double> vec(dims_, 0.0);
        std::hash<std::string> hasher;
        size_t h = hasher(text);
        for (int i = 0; i < dims_; ++i) {
            vec[i] = static_cast<double>((h >> ((i % 8) * 8)) & 0xFF) / 256.0;
        }
        return vec;
    }
    bool available() const override { return true; }

private:
    int dims_;
};

// In-memory MemoryStore: tier maps + long-term map + promotion log.
class InMemoryMemoryStore : public MemoryStore {
public:
    void store_tier(const std::string& tier, const MemoryItem& item) override {
        tiers_[tier][item.item_id] = item;
    }
    std::optional<MemoryItem> load_tier(const std::string& tier,
                                        const std::string& item_id) override {
        auto it = tiers_.find(tier);
        if (it == tiers_.end()) return std::nullopt;
        auto jt = it->second.find(item_id);
        if (jt == it->second.end()) return std::nullopt;
        return jt->second;
    }
    void delete_tier(const std::string& tier, const std::string& item_id) override {
        auto it = tiers_.find(tier);
        if (it != tiers_.end()) it->second.erase(item_id);
    }
    std::vector<std::pair<std::string, MemoryItem>> scan_tier(
        const std::string& tier) override {
        std::vector<std::pair<std::string, MemoryItem>> result;
        auto it = tiers_.find(tier);
        if (it != tiers_.end()) {
            for (auto& [id, item] : it->second) {
                result.emplace_back(id, item);
            }
        }
        return result;
    }
    bool has_tier(const std::string& tier, const std::string& item_id) override {
        auto it = tiers_.find(tier);
        if (it == tiers_.end()) return false;
        return it->second.find(item_id) != it->second.end();
    }

    void store_long_term(const MemoryItem& item, const std::string& status) override {
        MemoryItem stored = item;
        stored.status = status;
        long_term_[item.item_id] = stored;
    }
    std::vector<MemoryItemRow> fetch_by_status(const std::string& status) override {
        std::vector<MemoryItemRow> result;
        for (auto& [id, item] : long_term_) {
            (void)id;
            if (item.status != status) continue;
            MemoryItemRow row;
            row.item_id = item.item_id;
            row.user_id = item.user_id;
            row.hemisphere = item.hemisphere;
            row.kind = item.kind;
            row.status = item.status;
            row.narrative = item.narrative;
            row.concept_name = item.concept_name;
            row.understanding = item.understanding;
            row.importance_score = item.importance_score;
            row.floor = item.floor;
            row.must_keep = item.must_keep;
            row.protected_flag = item.protected_flag;
            row.emotional_marker = item.emotional_marker;
            row.emotional_intensity = item.emotional_intensity;
            row.formation_source = item.formation_source;
            row.seasonal_marker = item.seasonal_marker.empty()
                ? std::nullopt
                : std::optional<std::string>(item.seasonal_marker);
            row.ethical_coordinates = item.ethical_coordinates;
            row.reference_count = item.reference_count;
            row.last_referenced_at = item.last_referenced_at;
            row.created_at = item.created_at.empty()
                ? std::nullopt
                : std::optional<std::string>(item.created_at);
            row.decay_started_at = item.decay_started_at;
            result.push_back(row);
        }
        return result;
    }
    void update_item(const MemoryItemRow& row) override {
        auto it = long_term_.find(row.item_id);
        if (it == long_term_.end()) {
            // Fall back to tiers if present.
            for (auto& [tier, items] : tiers_) {
                (void)tier;
                auto tit = items.find(row.item_id);
                if (tit != items.end()) {
                    auto& item = tit->second;
                    item.importance_score = row.importance_score;
                    item.status = row.status;
                    item.reference_count = row.reference_count;
                    item.last_referenced_at = row.last_referenced_at;
                    item.decay_started_at = row.decay_started_at;
                    item.concept_name = row.concept_name;
                    item.understanding = row.understanding;
                    item.floor = row.floor;
                    item.protected_flag = row.protected_flag;
                    item.must_keep = row.must_keep;
                    return;
                }
            }
            return;
        }
        auto& item = it->second;
        item.importance_score = row.importance_score;
        item.status = row.status;
        item.reference_count = row.reference_count;
        item.last_referenced_at = row.last_referenced_at;
        item.decay_started_at = row.decay_started_at;
        if (row.concept_name.has_value() && !row.concept_name->empty())
            item.concept_name = row.concept_name;
        if (row.understanding.has_value() && !row.understanding->empty())
            item.understanding = row.understanding;
        if (row.floor.has_value()) item.floor = row.floor;
        item.protected_flag = row.protected_flag;
        item.must_keep = row.must_keep;
    }
    void delete_item(const std::string& item_id) override {
        long_term_.erase(item_id);
    }
    void log_promotion(const std::string& user_id, const std::string& item_id,
                       const std::string& from_stage, const std::string& to_stage,
                       double score, const std::string& reason) override {
        promotion_log_.emplace_back(user_id, item_id, from_stage, to_stage,
                                    score, reason);
    }

    size_t tier_size(const std::string& tier) const {
        auto it = tiers_.find(tier);
        return it == tiers_.end() ? 0 : it->second.size();
    }
    size_t long_term_size() const { return long_term_.size(); }
    size_t promotion_count() const { return promotion_log_.size(); }
    std::vector<std::tuple<std::string, std::string, std::string, std::string, double, std::string>>
        promotion_log() const { return promotion_log_; }

private:
    std::unordered_map<std::string, std::unordered_map<std::string, MemoryItem>> tiers_;
    std::unordered_map<std::string, MemoryItem> long_term_;
    std::vector<std::tuple<std::string, std::string, std::string, std::string, double, std::string>>
        promotion_log_;
};

// =============================================================================
// TIME HELPERS for tests
// =============================================================================

static std::string iso_from(const TimePoint& base, double days) {
    using namespace std::chrono;
    auto target = base
        - duration_cast<system_clock::duration>(duration<double>(days * 86400.0));
    auto tt = system_clock::to_time_t(target);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "Z";
    return oss.str();
}

static std::string iso_offset(double days) {
    return iso_from(Clock::now(), days);
}

// =============================================================================
// ROUTING
// =============================================================================

static void test_route_item() {
    MemoryItem crown;
    crown.importance_score = 8.0;
    auto rc = route_item(crown);
    CHECK(rc.stage == "long_term");
    CHECK(rc.status == "legacy");
    CHECK(rc.protected_flag);
    CHECK(rc.kind == "identity");

    MemoryItem strong;
    strong.importance_score = 6.0;
    auto rs = route_item(strong);
    CHECK(rs.stage == "long_term");
    CHECK(rs.status == "active");
    CHECK(!rs.protected_flag);
    CHECK(rs.kind == "episodic");

    MemoryItem weak;
    weak.importance_score = 2.0;
    auto rw = route_item(weak);
    CHECK(rw.stage == "t1");
    CHECK(rw.status == "");
}

// =============================================================================
// BUILD / FORM / TRIGGER
// =============================================================================

static std::shared_ptr<ValueEngine> make_engine() {
    return std::make_shared<ValueEngine>(
        PolytopeConstraints::from_season("spring"), "spring");
}

static void test_build_item() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    MemoryModule module(engine, nullptr, store);

    auto item = module.build_item(
        "u1", "we collaborated and built something together",
        {{"emotional_weight", 5.0},
         {"relational_significance", 5.0},
         {"identity_significance", 3.0},
         {"emotional_marker", 7.0}},   // "care"
        "conversation", "spring", false);

    CHECK(!item.item_id.empty());
    CHECK(item.user_id == "u1");
    CHECK(item.hemisphere == "personal");
    CHECK(item.emotional_marker == "care");
    CHECK(item.ethical_coordinates.size() == DIMENSION_COUNT);
    CHECK(item.importance_score >= 0.0 && item.importance_score <= 10.0);
    CHECK(item.geometric >= 0.0 && item.geometric <= 10.0);

    // Trigger: retention floor applies.
    auto trig = module.build_item(
        "u1", "a small but important moment",
        {{"emotional_weight", 1.0}}, "conversation", std::nullopt, true);
    CHECK(trig.importance_score >= TRIGGER_RETENTION_FLOOR);
}

static void test_form_items() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    MemoryModule module(engine, nullptr, store);

    std::vector<MemoryItem> moments(3);
    moments[0].narrative = "we worked through the problem together";
    moments[1].narrative = "she chose to trust the process";
    moments[2].narrative = "a quiet evening conversation";

    auto [t1, lt, crown] = module.form_items("u1", moments, "session", "spring", false);
    CHECK(t1 == 3);
    CHECK(lt == 0);
    CHECK(crown == 0);
    CHECK(store->tier_size("t1") == 3);
}

static void test_ingest_trigger() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    MemoryModule module(engine, nullptr, store);

    auto item = module.ingest_trigger(
        "u1", "  this moment matters — remember it  ", "identity");
    CHECK(item.has_value());
    CHECK(item->narrative == "this moment matters — remember it"); // trimmed
    CHECK(item->emotional_marker == "care");                      // default factor
    CHECK(item->importance_score >= TRIGGER_RETENTION_FLOOR);
    CHECK(store->long_term_size() == 1);
    CHECK(store->promotion_count() >= 1);

    // Empty narrative → nullopt.
    CHECK(!module.ingest_trigger("u1", "   ", "identity").has_value());
}

// =============================================================================
// SWEEP + 48-HOUR FALLOUT GRACE (D-027)
// =============================================================================

static void test_sweep() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    MemoryModule module(engine, nullptr, store);

    // A: t1 score 4.0 → promotes to t2 (gate 3.0).
    // B: t2 score 4.0 → promotes to t3 (gate 3.5).
    // C: t3 score 6.0 → long-term active (gate 5.0).
    // D: t1 score 2.0 → falls out to the 48h buffer (below gate 3.0).
    MemoryItem a, b, c, d;
    a.item_id = "a"; a.user_id = "u1"; a.narrative = "A"; a.importance_score = 4.0;
    b.item_id = "b"; b.user_id = "u1"; b.narrative = "B"; b.importance_score = 4.0;
    c.item_id = "c"; c.user_id = "u1"; c.narrative = "C"; c.importance_score = 6.0;
    d.item_id = "d"; d.user_id = "u1"; d.narrative = "D"; d.importance_score = 2.0;
    store->store_tier("t1", a);
    store->store_tier("t2", b);
    store->store_tier("t3", c);
    store->store_tier("t1", d);

    auto counts = module.run_sweep();
    CHECK(counts.t1_to_t2 == 1);
    CHECK(counts.t2_to_t3 == 1);
    CHECK(counts.to_long_term == 1);
    CHECK(counts.fallout == 1);
    CHECK(counts.repurposed == 0);
    CHECK(counts.purged == 0);

    CHECK(store->has_tier("t2", "a"));
    CHECK(store->has_tier("t3", "b"));
    CHECK(!store->has_tier("t3", "c"));
    CHECK(store->tier_size("fallout") == 1);

    auto fetched = store->fetch_by_status("active");
    bool found_c = false;
    for (const auto& row : fetched) {
        if (row.item_id == "c") found_c = true;
    }
    CHECK(found_c);

    // Fallout item is still within the 48h grace → untouched on the next pass.
    // (B, at 4.0, now fails the t3→long-term gate of 5.0 and falls out — so
    //  fallout holds D (in grace) and B (just fell out).)
    auto counts2 = module.run_sweep();
    CHECK(counts2.purged == 0);
    CHECK(counts2.repurposed == 0);
    CHECK(store->has_tier("fallout", "d"));
    CHECK(store->tier_size("fallout") == 2);
}

static void test_sweep_fallout_48h() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    MemoryModule module(engine, nullptr, store);

    // E: in fallout for 49h, recovered score 4.0 ≥ gate 3.0 → repurposed to t1.
    // F: in fallout for 49h, score 1.0 < gate 3.0 → purged.
    // G: in fallout for 1h → still in grace, untouched.
    MemoryItem e, f, g;
    e.item_id = "e"; e.user_id = "u1"; e.narrative = "E";
    e.importance_score = 4.0; e.failed_gate = 3.0;
    e.entered_fallout_at = iso_offset(49.0 / 24.0);
    f.item_id = "f"; f.user_id = "u1"; f.narrative = "F";
    f.importance_score = 1.0; f.failed_gate = 3.0;
    f.entered_fallout_at = iso_offset(49.0 / 24.0);
    g.item_id = "g"; g.user_id = "u1"; g.narrative = "G";
    g.importance_score = 2.0; g.failed_gate = 3.0;
    g.entered_fallout_at = iso_offset(1.0 / 24.0);

    store->store_tier("fallout", e);
    store->store_tier("fallout", f);
    store->store_tier("fallout", g);

    auto counts = module.run_sweep();
    CHECK(counts.repurposed == 1);  // e
    CHECK(counts.purged == 1);      // f
    CHECK(store->has_tier("t1", "e"));
    CHECK(!store->has_tier("fallout", "e"));
    CHECK(!store->has_tier("fallout", "f"));
    CHECK(store->has_tier("fallout", "g"));  // still in grace
}

// =============================================================================
// MAINTENANCE
// =============================================================================

static MemoryItemRow make_row(const std::string& id, const std::string& status,
                              double score, double created_days_ago,
                              const TimePoint& now)
{
    MemoryItemRow row;
    row.item_id = id;
    row.user_id = "u1";
    row.hemisphere = "personal";
    row.kind = "episodic";
    row.status = status;
    row.narrative = id;
    row.importance_score = score;
    row.created_at = iso_from(now, created_days_ago);
    return row;
}

static void test_maintenance() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    MemoryModule module(engine, nullptr, store);

    auto now = Clock::now();

    // 1. Score 9.5, referenced 30+ times, recent creation → rises to legacy.
    MemoryItem legacy;
    legacy.item_id = "l1"; legacy.user_id = "u1"; legacy.narrative = "crown";
    legacy.importance_score = 9.5; legacy.status = "active";
    legacy.reference_count = 30;
    legacy.created_at = iso_from(now, 10);
    store->store_long_term(legacy, "active");

    // 2. Score 3.0 → slips below the subconscious line.
    MemoryItem sub;
    sub.item_id = "s1"; sub.user_id = "u1"; sub.narrative = "fading";
    sub.importance_score = 3.0; sub.status = "active";
    sub.created_at = iso_from(now, 10);
    store->store_long_term(sub, "active");

    // 3. Score 6.0 → stays active.
    MemoryItem stay;
    stay.item_id = "st1"; stay.user_id = "u1"; stay.narrative = "steady";
    stay.importance_score = 6.0; stay.status = "active";
    stay.created_at = iso_from(now, 10);
    store->store_long_term(stay, "active");

    // 4. Subconscious, decay started 100 days ago → decays toward gone.
    MemoryItem decay;
    decay.item_id = "d1"; decay.user_id = "u1"; decay.narrative = "slope";
    decay.importance_score = 6.0; decay.status = "subconscious";
    decay.decay_started_at = iso_from(now, 100);
    store->store_long_term(decay, "subconscious");

    // 5. Subconscious, decay started 800 days ago → forgotten.
    MemoryItem gone;
    gone.item_id = "g1"; gone.user_id = "u1"; gone.narrative = "gone";
    gone.importance_score = 6.0; gone.status = "subconscious";
    gone.decay_started_at = iso_from(now, 800);
    store->store_long_term(gone, "subconscious");

    auto counts = module.run_maintenance(now);
    CHECK(counts.adjusted == 3);
    CHECK(counts.to_legacy == 1);
    CHECK(counts.to_subconscious == 1);
    // The just-demoted 's1' also enters the subconscious slope on this pass.
    CHECK(counts.decayed == 2);
    CHECK(counts.forgotten == 1);

    // Legacy item landed in legacy.
    auto legacy_rows = store->fetch_by_status("legacy");
    bool found_l1 = false;
    for (const auto& row : legacy_rows) {
        if (row.item_id == "l1") found_l1 = true;
    }
    CHECK(found_l1);

    // Subconscious item exists with decay start set.
    auto sub_rows = store->fetch_by_status("subconscious");
    bool found_s1 = false;
    for (const auto& row : sub_rows) {
        if (row.item_id == "s1") {
            found_s1 = true;
            CHECK(row.decay_started_at.has_value());
        }
    }
    CHECK(found_s1);

    // Decayed subconscious item has a lower score.
    bool found_d1 = false;
    for (const auto& row : sub_rows) {
        if (row.item_id == "d1") {
            found_d1 = true;
            CHECK(row.importance_score < 6.0);
            CHECK(row.importance_score > 0.5);
        }
    }
    CHECK(found_d1);

    // Gone item deleted.
    bool found_g1 = false;
    for (const auto& row : sub_rows) {
        if (row.item_id == "g1") found_g1 = true;
    }
    CHECK(!found_g1);

    // Steady item still active.
    bool found_st1 = false;
    for (const auto& row : store->fetch_by_status("active")) {
        if (row.item_id == "st1") found_st1 = true;
    }
    CHECK(found_st1);
}

static void test_legacy_review() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    MemoryModule module(engine, nullptr, store);

    auto now = Clock::now();

    // Protected legacy item holds even below the floor.
    MemoryItem protected_item;
    protected_item.item_id = "p1"; protected_item.user_id = "u1";
    protected_item.narrative = "protected"; protected_item.status = "legacy";
    protected_item.importance_score = 7.0; protected_item.protected_flag = true;
    protected_item.created_at = iso_from(now, 10);
    store->store_long_term(protected_item, "legacy");

    // Unprotected legacy item below the floor → demoted.
    MemoryItem demote;
    demote.item_id = "d1"; demote.user_id = "u1"; demote.narrative = "demote";
    demote.status = "legacy"; demote.importance_score = 7.0;
    demote.created_at = iso_from(now, 10);
    store->store_long_term(demote, "legacy");

    auto counts = module.run_legacy_review(now);
    CHECK(counts.reviewed == 2);
    CHECK(counts.demoted == 1);

    auto legacy_rows = store->fetch_by_status("legacy");
    bool found_p1 = false;
    for (const auto& row : legacy_rows) {
        if (row.item_id == "p1") found_p1 = true;
    }
    CHECK(found_p1);

    auto sub_rows = store->fetch_by_status("subconscious");
    bool found_d1 = false;
    for (const auto& row : sub_rows) {
        if (row.item_id == "d1") found_d1 = true;
    }
    CHECK(found_d1);
}

// =============================================================================
// RECALL
// =============================================================================

static void test_recall() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    auto embedder = std::make_shared<TestEmbeddingEngine>(14);
    MemoryModule module(engine, embedder, store);

    // Two identical-narrative items (semantic match 1.0), different importance.
    MemoryItem hi, lo;
    hi.item_id = "hi"; hi.user_id = "u1"; hi.hemisphere = "personal";
    hi.narrative = "the same remembered moment"; hi.importance_score = 9.0;
    lo.item_id = "lo"; lo.user_id = "u1"; lo.hemisphere = "personal";
    lo.narrative = "the same remembered moment"; lo.importance_score = 3.0;

    auto coords_hi = encode_coordinates(*engine, hi.narrative);
    hi.ethical_coordinates = coords_hi;
    lo.ethical_coordinates = coords_hi;

    store->store_long_term(hi, "active");
    store->store_long_term(lo, "active");

    auto results = module.recall("u1", "the same remembered moment", "personal", 2, false);
    CHECK(results.size() == 2);
    CHECK(results[0].item_id == "hi");  // higher importance ranks first
    CHECK(results[1].item_id == "lo");

    // Hemisphere filter excludes impersonal wisdom.
    auto filtered = module.recall("u1", "the same remembered moment", "impersonal", 5, false);
    CHECK(filtered.empty());

    // Unknown user gets nothing.
    auto other = module.recall("u2", "the same remembered moment", std::nullopt, 5, false);
    CHECK(other.empty());
}

static void test_inject_context() {
    auto engine = make_engine();
    auto store = std::make_shared<InMemoryMemoryStore>();
    auto embedder = std::make_shared<TestEmbeddingEngine>(14);
    MemoryModule module(engine, embedder, store);

    MemoryItem personal;
    personal.item_id = "pers"; personal.user_id = "u1";
    personal.hemisphere = "personal"; personal.narrative = "a personal memory";
    personal.importance_score = 8.0;
    store->store_long_term(personal, "active");

    MemoryItem wisdom;
    wisdom.item_id = "wise"; wisdom.user_id = "u1";
    wisdom.hemisphere = "impersonal"; wisdom.narrative = "a hard-won lesson";
    wisdom.importance_score = 9.5; wisdom.kind = "identity";
    wisdom.concept_name = "patience";
    store->store_long_term(wisdom, "legacy");

    auto ctx = module.inject_context("u1", "a personal memory", 5, 8);
    CHECK(ctx["recent_episodic"].size() == 1);
    CHECK(ctx["recent_episodic"][0]["narrative"] == "a personal memory");
    CHECK(ctx["key_semantic"].size() == 1);
    CHECK(ctx["key_semantic"][0]["concept"] == "patience");
    CHECK(ctx["key_semantic"][0]["type"] == "identity");
}

// =============================================================================
// PURE FUNCTIONS
// =============================================================================

static void test_similarity() {
    std::vector<double> x{1.0, 0.0};
    std::vector<double> y{0.0, 1.0};
    std::vector<double> z{0.0, 0.0};

    CHECK_NEAR(cosine(&x, &x), 1.0, 1e-12);
    CHECK_NEAR(cosine(&x, &y), 0.0, 1e-12);
    CHECK_NEAR(cosine(nullptr, &x), 0.0, 1e-12);
    CHECK_NEAR(cosine(&x, &z), 0.0, 1e-12);

    CHECK_NEAR(ethical_similarity(&z, &z), 1.0, 1e-12);
    CHECK_NEAR(ethical_similarity(&x, &y), 1.0 / (1.0 + std::sqrt(2.0)), 1e-9);
    CHECK_NEAR(ethical_similarity(nullptr, &x), 0.0, 1e-12);

    CHECK_NEAR(recall_score(0.5, 0.5, 0.5), 0.5, 1e-12);
    CHECK_NEAR(recall_score(1.0, 0.0, 0.0), 0.5, 1e-12);
    CHECK_NEAR(recall_score(0.0, 1.0, 0.0), 0.3, 1e-12);
    CHECK_NEAR(recall_score(0.0, 0.0, 1.0), 0.2, 1e-12);
}

static void test_maintenance_delta() {
    auto now = Clock::now();

    // Reference rewards.
    CHECK_NEAR(maintenance_delta(30, std::nullopt, std::nullopt, now), 2.0, 1e-12);
    CHECK_NEAR(maintenance_delta(12, std::nullopt, std::nullopt, now), 1.5, 1e-12);
    CHECK_NEAR(maintenance_delta(4, std::nullopt, std::nullopt, now), 1.0, 1e-12);

    // Recent reference reward.
    CHECK_NEAR(maintenance_delta(0, iso_offset(10), std::nullopt, now), 0.5, 1e-12);

    // Never referenced — age penalties.
    CHECK_NEAR(maintenance_delta(0, std::nullopt, iso_offset(100), now), -1.0, 1e-12);
    CHECK_NEAR(maintenance_delta(0, std::nullopt, iso_offset(200), now), -2.0, 1e-12);

    // Both rewards stack.
    CHECK_NEAR(maintenance_delta(30, iso_offset(10), std::nullopt, now), 2.5, 1e-12);
}

static void test_pure_lifecycle_functions() {
    auto now = Clock::now();

    // apply_monthly: legacy entry.
    auto legacy_row = make_row("l", "active", 9.5, 10, now);
    legacy_row.reference_count = 30;
    auto dm = apply_monthly(legacy_row, now);
    CHECK(dm.status == "legacy");
    CHECK(dm.log_entry.has_value());

    // apply_monthly: subconscious slip.
    auto sub_row = make_row("s", "active", 3.0, 10, now);
    auto ds = apply_monthly(sub_row, now);
    CHECK(ds.status == "subconscious");
    CHECK(ds.decay_started_at.has_value());

    // apply_monthly: steady.
    auto steady_row = make_row("st", "active", 6.0, 10, now);
    auto dst = apply_monthly(steady_row, now);
    CHECK(dst.status == "active");
    CHECK(!dst.log_entry.has_value());

    // slope_effective: half-life decay after 100 days (created_at is older
    // than the decay start, so the decay anchor wins).
    auto slope_row = make_row("sl", "subconscious", 6.0, 200, now);
    slope_row.decay_started_at = iso_offset(100);
    auto [effective, gone] = slope_effective(slope_row, now);
    CHECK(!gone);
    // ISO timestamps truncate to the second, so the anchor carries sub-day
    // rounding — compare at 1e-3, not 1e-9.
    CHECK(std::fabs(effective - 4.2426) < 1e-3);

    // slope_effective: gone beyond the horizon.
    auto gone_row = make_row("go", "subconscious", 6.0, 900, now);
    gone_row.decay_started_at = iso_offset(800);
    auto [e2, g2] = slope_effective(gone_row, now);
    CHECK(g2);
    CHECK_NEAR(e2, 0.0, 1e-12);

    // apply_legacy_review: protected holds.
    auto prot_row = make_row("p", "legacy", 7.0, 10, now);
    prot_row.protected_flag = true;
    auto dprot = apply_legacy_review(prot_row, now);
    CHECK(dprot.status == "legacy");

    // apply_legacy_review: unprotected below floor demotes.
    auto dem_row = make_row("d", "legacy", 7.0, 10, now);
    auto ddem = apply_legacy_review(dem_row, now);
    CHECK(ddem.status == "subconscious");
}

// =============================================================================

int main() {
    test_route_item();
    test_build_item();
    test_form_items();
    test_ingest_trigger();
    test_sweep();
    test_sweep_fallout_48h();
    test_maintenance();
    test_legacy_review();
    test_recall();
    test_inject_context();
    test_similarity();
    test_maintenance_delta();
    test_pure_lifecycle_functions();

    std::cout << "memory_module_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
