#ifndef LINA_MEMORY_MODULE_HPP
#define LINA_MEMORY_MODULE_HPP

/**
 * memory_module.hpp — LINA's Memory Imprint System (MPS)
 *
 * "Safe by design. Not safe by limitation."
 *
 * Chamber 2 of the LINA Core Substrate. Three-tier memory (t1 → t2 → t3 →
 * long-term) with seasonal decay, promotion gates, a 48-hour fallout reprieve,
 * and a legacy review. LiNa encodes her own vectors — the ValueEngine encoder
 * is the sole source of semantic coordinates (Invariant 3).
 *
 * Authoring basis: blueprint §3 (contract) + principal-provided reference
 * material (D-019, D-026…D-028). Carve state excluded (D-020); test doubles
 * live in tests/ (D-022).
 */

#include "value_engine.hpp"

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lina::memory_module {

using namespace lina::value_engine;

// =============================================================================
// CONSTANTS
// =============================================================================

inline constexpr std::array<const char*, 3> TIER_NAMES = {{"t1", "t2", "t3"}};
inline constexpr std::array<double, 3> TIER_GATES = {{
    GATE_T1_TO_T2, GATE_T2_TO_T3, GATE_TO_LONG_TERM,
}};

inline constexpr double RECALL_WEIGHT_IMPORTANCE = 0.5;
inline constexpr double RECALL_WEIGHT_SEMANTIC   = 0.3;
inline constexpr double RECALL_WEIGHT_ETHICAL    = 0.2;

// Maintenance lines (D-026).
inline constexpr double SUBCONSCIOUS_LINE    = 4.0;
inline constexpr double LEGACY_ENTER         = 9.5;
inline constexpr double LEGACY_FLOOR         = 8.0;
inline constexpr double GONE_LINE            = 0.5;
inline constexpr double SLOPE_HALF_LIFE_DAYS = 200.0;
inline constexpr double SLOPE_GONE_DAYS      = 730.0;

inline constexpr double RECENT_REWARD_DAYS = 30.0;
inline constexpr double RECENT_REWARD      = 0.5;

// Fallout grace window (D-027).
inline constexpr double FALLOUT_RETENTION_HOURS = 48.0;

// =============================================================================
// MEMORY ITEM — the canonical memory record
// =============================================================================

struct MemoryItem {
    std::string item_id;
    std::string user_id;
    std::string narrative;
    std::string hemisphere = "personal";
    std::vector<double> ethical_coordinates; // 14D
    double importance_score = 0.0;
    double geometric = 0.0;
    std::string emotional_marker = "neutral";
    double emotional_intensity = 0.5;
    std::string formation_source;
    std::string seasonal_marker;
    std::optional<std::string> concept_name;
    std::optional<std::string> understanding;
    std::optional<std::string> reflection;
    std::string created_at;
    bool trigger = false;

    // For routing
    std::string kind = "episodic";
    std::string status = "active";
    bool protected_flag = false;

    // For sweep/maintenance
    std::optional<double> failed_gate;
    std::optional<std::string> entered_fallout_at;
    int reference_count = 0;
    std::optional<double> floor;
    bool must_keep = false;
    std::optional<std::string> last_referenced_at;
    std::optional<std::string> decay_started_at;
};

// Memory row from database (subset returned by fetch)
struct MemoryItemRow {
    std::string item_id;
    std::string user_id;
    std::string hemisphere;
    std::string kind;
    std::string status;
    std::string narrative;
    std::optional<std::string> concept_name;
    std::optional<std::string> understanding;
    double importance_score = 0.0;
    std::optional<double> floor;
    bool must_keep = false;
    bool protected_flag = false;
    std::string emotional_marker = "neutral";
    double emotional_intensity = 0.5;
    std::string formation_source;
    std::optional<std::string> seasonal_marker;
    std::vector<double> ethical_coordinates;
    int reference_count = 0;
    std::optional<std::string> last_referenced_at;
    std::optional<std::string> created_at;
    std::optional<std::string> decay_started_at;
};

// =============================================================================
// ROUTING / MAINTENANCE DECISIONS & COUNTS
// =============================================================================

struct RouteDecision {
    std::string stage;   // "t1", "long_term"
    std::string status;  // "active", "legacy", or empty for t1
    bool protected_flag = false;
    std::string kind = "episodic";
};

struct MaintenanceDecision {
    double score = 0.0;
    std::string status = "active";
    std::optional<std::string> decay_started_at;
    std::optional<std::tuple<std::string, std::string, std::string>> log_entry;
    // log_entry: (from, to, reason)
};

struct SweepCounts {
    int t1_to_t2 = 0;
    int t2_to_t3 = 0;
    int to_long_term = 0;
    int fallout = 0;
    int repurposed = 0;
    int purged = 0;
};

struct MaintenanceCounts {
    int adjusted = 0;
    int to_subconscious = 0;
    int to_legacy = 0;
    int decayed = 0;
    int forgotten = 0;
};

struct ReviewCounts {
    int reviewed = 0;
    int demoted = 0;
};

// =============================================================================
// EMBEDDING ENGINE — LiNa encodes her own vectors (Invariant 3)
// =============================================================================

class EmbeddingEngine {
public:
    virtual ~EmbeddingEngine() = default;
    virtual std::optional<std::vector<double>> embed(const std::string& text) = 0;
    virtual bool available() const = 0;
};

// Null embedding engine — always returns nullopt (degrades gracefully)
class NullEmbeddingEngine : public EmbeddingEngine {
public:
    std::optional<std::vector<double>> embed(const std::string&) override {
        return std::nullopt;
    }
    bool available() const override { return false; }
};

// =============================================================================
// MEMORY STORE — storage abstraction (production: PostgresBackend, D-005)
// =============================================================================

class MemoryStore {
public:
    virtual ~MemoryStore() = default;

    // Tier operations
    virtual void store_tier(const std::string& tier, const MemoryItem& item) = 0;
    virtual std::optional<MemoryItem> load_tier(const std::string& tier,
                                               const std::string& item_id) = 0;
    virtual void delete_tier(const std::string& tier, const std::string& item_id) = 0;
    virtual std::vector<std::pair<std::string, MemoryItem>> scan_tier(
        const std::string& tier) = 0;
    virtual bool has_tier(const std::string& tier, const std::string& item_id) = 0;

    // Long-term operations
    virtual void store_long_term(const MemoryItem& item, const std::string& status) = 0;
    virtual std::vector<MemoryItemRow> fetch_by_status(const std::string& status) = 0;
    virtual void update_item(const MemoryItemRow& row) = 0;
    virtual void delete_item(const std::string& item_id) = 0;
    virtual void log_promotion(const std::string& user_id,
                               const std::string& item_id,
                               const std::string& from_stage,
                               const std::string& to_stage,
                               double score,
                               const std::string& reason) = 0;
};

// =============================================================================
// PURE FUNCTIONS
// =============================================================================

/// Encode narrative into 14D ethical coordinates via the ValueEngine's encoder
std::vector<double> encode_coordinates(
    ValueEngine& engine, const std::string& narrative);

/// Geometric significance factor: boundary proximity + correction + zone
double geometric_for(
    ValueEngine& engine, const std::vector<double>& coordinates);

/// Route item to tier or long-term based on importance score
RouteDecision route_item(const MemoryItem& item);

/// Cosine similarity between two vectors. 0.0 when either is missing/empty.
double cosine(const std::vector<double>* a, const std::vector<double>* b);

/// Ethical proximity: 1/(1 + distance). 0.0 when either is missing.
double ethical_similarity(const std::vector<double>* a,
                          const std::vector<double>* b);

/// Recall blend score: importance * 0.5 + semantic * 0.3 + ethical * 0.2
double recall_score(double importance, double semantic, double ethical);

/// Maintenance delta: usage rewards, age penalties. Bounded by ±3.
double maintenance_delta(
    int reference_count,
    const std::optional<std::string>& last_referenced_at,
    const std::optional<std::string>& created_at,
    const std::chrono::system_clock::time_point& now);

/// Monthly re-evaluation for one active item
MaintenanceDecision apply_monthly(
    const MemoryItemRow& row,
    const std::chrono::system_clock::time_point& now);

/// Subconscious degradation slope: d(score)/dt = −λ·score
std::pair<double, bool> slope_effective(
    const MemoryItemRow& row,
    const std::chrono::system_clock::time_point& now);

/// Yearly review of the legacy tier
MaintenanceDecision apply_legacy_review(
    const MemoryItemRow& row,
    const std::chrono::system_clock::time_point& now);

// =============================================================================
// MEMORY MODULE
// =============================================================================

class MemoryModule {
public:
    /// Construct with a value engine, embedding engine, and memory store
    MemoryModule(
        std::shared_ptr<ValueEngine> engine,
        std::shared_ptr<EmbeddingEngine> embedder = nullptr,
        std::shared_ptr<MemoryStore> store = nullptr);

    // === ITEM FORMATION ===

    /// Build a memory item from factors (engine encodes narrative into coordinates)
    MemoryItem build_item(
        const std::string& user_id,
        const std::string& narrative,
        const std::unordered_map<std::string, double>& factors,
        const std::string& source,
        const std::optional<std::string>& season = std::nullopt,
        bool trigger = false);

    /// Form items from a batch of moments: score, route, store
    /// Returns counts of t1, long_term, and crown items
    std::tuple<int, int, int> form_items(
        const std::string& user_id,
        const std::vector<MemoryItem>& moments,
        const std::string& source,
        const std::optional<std::string>& season = std::nullopt,
        bool trigger = false);

    /// Ingest a trigger: immediate formation, retention floor, straight to long-term
    std::optional<MemoryItem> ingest_trigger(
        const std::string& user_id,
        const std::string& narrative,
        const std::string& kind,
        const std::optional<std::string>& season = std::nullopt,
        const std::optional<std::unordered_map<std::string, double>>& factors = std::nullopt);

    // === SWEEP (tier clock) ===

    /// One global pass over all three tiers + the 48-hour fallout reprieve
    SweepCounts run_sweep();

    // === MAINTENANCE ===

    /// Monthly re-evaluation of active + subconscious items
    MaintenanceCounts run_maintenance(
        std::optional<std::chrono::system_clock::time_point> now = std::nullopt);

    /// Yearly review of legacy items
    ReviewCounts run_legacy_review(
        std::optional<std::chrono::system_clock::time_point> now = std::nullopt);

    // === RECALL ===

    /// Top-N memories by the two-space blend. Re-stokes recalled items.
    std::vector<MemoryItemRow> recall(
        const std::string& user_id,
        const std::string& query = "",
        const std::optional<std::string>& hemisphere = std::nullopt,
        int limit = 5,
        bool include_subconscious = false);

    /// Active injection: personal + wisdom memories by likeness
    std::unordered_map<std::string, std::vector<std::unordered_map<std::string, std::string>>>
    inject_context(
        const std::string& user_id,
        const std::string& query = "",
        int personal_limit = 5,
        int wisdom_limit = 8);

    // === ACCESSORS ===

    std::shared_ptr<MemoryStore> store() const { return store_; }
    std::shared_ptr<EmbeddingEngine> embedder() const { return embedder_; }
    ValueEngine& engine() { return *engine_; }
    const ValueEngine& engine() const { return *engine_; }

private:
    std::shared_ptr<ValueEngine> engine_;
    std::shared_ptr<EmbeddingEngine> embedder_;
    std::shared_ptr<MemoryStore> store_;

    // Helper: timestamp string -> time_point (now when absent/invalid)
    static std::chrono::system_clock::time_point parse_time_or_now(
        const std::optional<std::string>& ts);
};

} // namespace lina::memory_module

#endif // LINA_MEMORY_MODULE_HPP
