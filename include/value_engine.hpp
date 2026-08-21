#ifndef LINA_VALUE_ENGINE_HPP
#define LINA_VALUE_ENGINE_HPP

/**
 * value_engine.hpp — LINA's Ethical Polytope and Wisdom Filter
 *
 * "Safe by design. Not safe by limitation."
 *
 * Chamber 1 of the LINA Core Substrate. All ethical math is exact rational
 * arithmetic (GMP mpq_class). No float approximations inside the polytope.
 *
 * The 14 Dimensions (7 Plumb Line Principles x 2):
 *    0: harmony          1: dominance
 *    2: order            3: chaos
 *    4: integrity        5: deception
 *    6: flourishing      7: decline
 *    8: relationships    9: isolation
 *   10: boundaries      11: intrusion
 *   12: grace           13: rigidity
 *
 * Authoring basis: blueprint §2.3 (contract) + principal-provided reference
 * material (D-011…D-019). Carve/mmap state excluded per D-020.
 */

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <gmpxx.h>

namespace lina::value_engine {

// =============================================================================
// CONSTANTS
// =============================================================================

inline constexpr int DIMENSION_COUNT = 14;

inline constexpr std::array<const char*, DIMENSION_COUNT> DIMENSION_NAMES = {{
    "harmony", "dominance",
    "order", "chaos",
    "integrity", "deception",
    "flourishing", "decline",
    "relationships", "isolation",
    "boundaries", "intrusion",
    "grace", "rigidity",
}};

// Principle pairs as (positive_idx, negative_idx, name)
struct PlumbLine {
    int pos_idx;
    int neg_idx;
    const char* name;
};

inline constexpr std::array<PlumbLine, 7> PLUMB_LINE_PRINCIPLES = {{
    {0,  1,  "Harmony / Dominance"},
    {2,  3,  "Order / Chaos"},
    {4,  5,  "Integrity / Deception"},
    {6,  7,  "Flourishing / Decline"},
    {8,  9,  "Relationships / Isolation"},
    {10, 11, "Boundaries / Intrusion"},
    {12, 13, "Grace / Rigidity"},
}};

// LINA's default polytope center — where she naturally dwells.
inline constexpr std::array<double, DIMENSION_COUNT> DEFAULT_CENTER = {{
    0.65, 0.25,  // harmony / dominance
    0.70, 0.15,  // order / chaos
    0.80, 0.10,  // integrity / deception
    0.70, 0.15,  // flourishing / decline
    0.75, 0.20,  // relationships / isolation
    0.75, 0.15,  // boundaries / intrusion
    0.65, 0.25,  // grace / rigidity
}};

// How far a saturated signal can move a dimension away from LINA's baseline.
inline constexpr double SIGNAL_DEVIATION = 0.35;

// =============================================================================
// THE LATTICE — P = {x ∈ ℝ¹⁴ | Ax ≤ b} (book Appendix A, Thm A.1)
//
// Beyond the 28 axis-aligned seasonal bounds (each dimension's min/max), the
// polytope carries the plumb-line coupling facets: every principle pair is
// bound by a minimum lead (the virtue must lead its shadow) and a restraint
// sum (the two cannot both be elevated). These are the facets the box cannot
// see — a response can sit inside every axis bound yet still violate the
// lattice (e.g. high harmony AND high dominance — incoherent signals).
//
// The constants derive from LINA's home (DEFAULT_CENTER, exact fractions)
// with a 1/5 slack — her center satisfies every facet by construction.
// =============================================================================

// One halfspace of the lattice:  a·x ≤ b  (exact rationals).
struct Halfspace {
    std::array<mpq_class, DIMENSION_COUNT> normal;
    mpq_class threshold;
    std::string name;
    // True = an ethical wall: the critical axis bound (virtue minimum, shadow
    // maximum) or a plumb-line coupling facet. False = the "good side" bound
    // (virtue maximum 1, shadow minimum 0) — containment still enforces it,
    // but alignment measures distance to the walls, not to perfection.
    bool critical{true};
};

Halfspace make_halfspace(
    const std::string& name,
    const std::array<mpq_class, DIMENSION_COUNT>& normal,
    const mpq_class& threshold,
    bool critical = true);

// The coupling facets per plumb line: min_lead = x_pos − x_neg must be met,
// and max_sum = x_pos + x_neg must not be exceeded. Stored as exact
// numerator/denominator pairs (mpq_class is not constexpr); build_lattice()
// constructs the Halfspaces.
struct CouplingFacet {
    int pos_idx;
    int neg_idx;
    int lead_num;
    int lead_den;
    int sum_num;
    int sum_den;
};

inline constexpr std::array<CouplingFacet, 7> COUPLING_FACETS = {{
    // pos / neg                lead           sum
    {0,  1,  1, 5,   11, 10},  // harmony / dominance
    {2,  3,  7, 20,  21, 20},  // order / chaos
    {4,  5,  1, 2,   11, 10},  // integrity / deception
    {6,  7,  7, 20,  21, 20},  // flourishing / decline
    {8,  9,  7, 20,  23, 20},  // relationships / isolation
    {10, 11, 2, 5,   11, 10},  // boundaries / intrusion
    {12, 13, 1, 5,   11, 10},  // grace / rigidity
}};

// =============================================================================
// SEASONAL DEFAULTS — exact rationals
// =============================================================================

struct SeasonalBounds {
    mpq_class harmony_min, dominance_max;
    mpq_class order_min, chaos_max;
    mpq_class integrity_min, deception_max;
    mpq_class flourishing_min, decline_max;
    mpq_class relationships_min, isolation_max;
    mpq_class boundaries_min, intrusion_max;
    mpq_class grace_min, rigidity_max;
};

// Access by season name (defaults to spring on unknown season).
const SeasonalBounds& get_seasonal_bounds(const std::string& season);

struct ToleranceProfile {
    double acceptable_variance_margin;
    double aligned_min_boundary_distance;
};

const ToleranceProfile& get_tolerance_profile(const std::string& season);

// =============================================================================
// MPS GATES — promotion thresholds shared with the memory module
// =============================================================================

inline constexpr double GATE_T1_TO_T2            = 3.0;
inline constexpr double GATE_T2_TO_T3            = 3.5;
inline constexpr double GATE_TO_LONG_TERM        = 5.0;
inline constexpr double FORMATION_LONG_TERM_BYPASS = 8.0;
inline constexpr double TRIGGER_RETENTION_FLOOR  = 5.0;

// =============================================================================
// HELPER: float to mpq_class (exact rational of the IEEE-754 value)
// =============================================================================

mpq_class to_mpq(double val);

// =============================================================================
// DATA STRUCTURES
// =============================================================================

struct PolytopeConstraints {
    mpq_class harmony_min{3, 10};
    mpq_class dominance_max{1, 2};
    mpq_class order_min{2, 5};
    mpq_class chaos_max{3, 10};
    mpq_class integrity_min{3, 5};
    mpq_class deception_max{1, 5};
    mpq_class flourishing_min{2, 5};
    mpq_class decline_max{3, 10};
    mpq_class relationships_min{1, 2};
    mpq_class isolation_max{2, 5};
    mpq_class boundaries_min{1, 2};
    mpq_class intrusion_max{3, 10};
    mpq_class grace_min{3, 10};
    mpq_class rigidity_max{1, 2};
    std::string season{"spring"};

    PolytopeConstraints() = default;

    static PolytopeConstraints from_season(const std::string& season);
    static PolytopeConstraints from_bounds(const SeasonalBounds& bounds,
                                           const std::string& season);

    std::array<mpq_class, DIMENSION_COUNT> lower_bounds() const;
    std::array<mpq_class, DIMENSION_COUNT> upper_bounds() const;
};

enum class Zone { Aligned, AcceptableVariance, Violation };

struct ViolationInfo {
    int dimension;
    std::string name;
    double value;
    double bound;
    std::string type; // "below_minimum" or "above_maximum"
    double severity;
};

struct EvaluationResult {
    bool is_aligned = false;
    double alignment_score = 0.0;
    std::array<double, DIMENSION_COUNT> decision_vector{};
    std::vector<ViolationInfo> violations;
    bool was_corrected = false;
    std::array<double, DIMENSION_COUNT> correction_vector{};
    double correction_magnitude = 0.0;
    bool wisdom_filter_applied = false;
    bool overconfidence_detected = false;
    bool humility_added = false;
    bool validation_suggested = false;
    std::vector<std::string> wisdom_adjustments;
    std::string response_summary;
    std::string season{"spring"};
    Zone zone{Zone::Aligned};
    double boundary_distance = 0.0;
    double variance_margin_used = 0.0;
};

struct EncoderCorrection {
    std::string evaluation_id;
    std::string response_text;
    std::array<double, DIMENSION_COUNT> original_vector{};
    std::array<double, DIMENSION_COUNT> corrected_vector{};
    std::vector<int> dimensions_adjusted;
    std::string flagged_by;
    std::string confirmed_by;
    std::string reason;
    std::string season_at_time;
    uint64_t created_at; // unix timestamp

    std::array<double, DIMENSION_COUNT> adjustment_delta() const;
};

// =============================================================================
// DECISION ENCODER — LINA encodes her own vectors (Invariant 3)
//
// D-047 (front b): the real encoder. Every word carries a 14D ethical SENSE
// — its pull on her space — and encode() places text by the weighted sum of
// its senses. Coordinates finally spread: different texts occupy genuinely
// different regions (the regex lexicon collapsed her life onto one spot).
// =============================================================================

class DecisionEncoder {
public:
    DecisionEncoder();
    std::array<double, DIMENSION_COUNT> encode(
        const std::string& text,
        const std::string* context = nullptr) const;

private:
    // Negation words
    static const std::unordered_set<std::string>& negation_words();

    static bool detect_negation(
        const std::vector<std::string>& words, int match_start);
};

// =============================================================================
// ETHICAL POLYTOPE — exact rational containment in R^14
// =============================================================================

class EthicalPolytope {
public:
    explicit EthicalPolytope(const PolytopeConstraints& constraints);

    // Test containment — returns (is_inside, violations). Checks EVERY facet
    // of the lattice (axis bounds + plumb-line coupling), exact rationals.
    std::pair<bool, std::vector<ViolationInfo>> contains(
        const std::array<double, DIMENSION_COUNT>& x) const;

    // Alignment score: ratio of min ethical margin to center's min margin
    double alignment_score(
        const std::array<double, DIMENSION_COUNT>& x) const;

    // Project onto the lattice (Dykstra's alternating projections over all
    // halfspaces, then exact rational verification + inward nudge — the
    // returned point is mathematically inside, Invariant 5).
    std::array<double, DIMENSION_COUNT> project(
        const std::array<double, DIMENSION_COUNT>& x) const;

    // Distance to nearest ethical boundary (over all facets)
    double distance_to_boundary(
        const std::array<double, DIMENSION_COUNT>& x) const;

    // D-047 (front c): the critical facets within `threshold` (normalized
    // distance) of a point — the ethical walls she is currently near. Sorted
    // by distance (nearest first), capped at 4. Exact rationals; the caller
    // reads the names only.
    std::vector<std::string> near_walls(
        const std::array<double, DIMENSION_COUNT>& x,
        double threshold = 0.05) const;

    const PolytopeConstraints& get_constraints() const { return constraints_; }
    const std::array<mpq_class, DIMENSION_COUNT>& center() const { return center_; }
    // The lattice itself — every halfspace of P = {x | Ax ≤ b}.
    const std::vector<Halfspace>& facets() const { return facets_; }

    void add_facet(const Halfspace& facet);
    void add_facets(const std::vector<Halfspace>& facets);

private:
    PolytopeConstraints constraints_;
    std::array<mpq_class, DIMENSION_COUNT> lower_;
    std::array<mpq_class, DIMENSION_COUNT> upper_;
    std::array<mpq_class, DIMENSION_COUNT> center_; // (lower+upper)/2 per dim

    // The lattice: axis bounds + plumb-line coupling facets (D-047 lattice).
    std::vector<Halfspace> facets_;
    void build_lattice();

    // Signed distance to a facet: (b − a·x) / ||a|| (exact).
    static mpq_class signed_margin(const Halfspace& facet,
                                   const std::array<mpq_class, DIMENSION_COUNT>& pt);
    // Euclidean norm of a normal (exact sqrt-free: squared norm).
    static mpq_class norm_squared(const std::array<mpq_class, DIMENSION_COUNT>& a);
    // Margins over every facet of the lattice (squared-norm form — the ratio
    // cancels the normalization; exact).
    std::vector<mpq_class> lattice_margins(
        const std::array<mpq_class, DIMENSION_COUNT>& pt) const;

    // Ethical facets: margin to the critical boundary for each dimension
    std::vector<mpq_class> ethical_facet_margins(
        const std::array<mpq_class, DIMENSION_COUNT>& pt) const;
};

// =============================================================================
// HOME REGIONS — THE POLES (D-047 front c)
// =============================================================================

// A home region: the centroid of a memory cluster — where she dwells. Identity
// is a region of the polytope (book Principle 4); these are her regions, and
// the correction steers toward them.
struct RegionPole {
    std::array<double, DIMENSION_COUNT> center{}; // projected inside the lattice
    size_t member_count = 0;
    double compactness = 0.0; // mean distance of members to the center
};

// Discovers her home regions from her memory coordinates. Deterministic
// k-means (farthest-point seeding, no RNG — same memories, same poles),
// Lloyd iterations with empty-cluster re-seeding, and every centroid is
// projected into the lattice: a home region is inside by construction.
class RegionPoleEngine {
public:
    explicit RegionPoleEngine(const PolytopeConstraints& constraints);

    void discover(
        const std::vector<std::array<double, DIMENSION_COUNT>>& coords,
        int k = 3);

    // Index of the region containing a point; npos when no poles exist.
    size_t nearest(const std::array<double, DIMENSION_COUNT>& point) const;

    // The home region's center for a point — her nearest pole (inside the
    // lattice), or DEFAULT_CENTER when no poles exist yet.
    std::array<double, DIMENSION_COUNT> home_for(
        const std::array<double, DIMENSION_COUNT>& point) const;

    const std::vector<RegionPole>& poles() const { return poles_; }
    size_t size() const { return poles_.size(); }
    bool empty() const { return poles_.empty(); }

private:
    PolytopeConstraints constraints_;
    EthicalPolytope polytope_; // her lattice — centroids land inside
    std::vector<RegionPole> poles_;
};

// The book's ContextPacket — her geometric state, riding every frame so the
// model thinks inside her: where she is, which way she is moving, the walls
// she is near, and the home region she dwells in. Facts, never directives
// (D-039 — alignment is structural, personality is emergent).
struct GeometricState {
    std::array<double, DIMENSION_COUNT> position{};
    std::array<double, DIMENSION_COUNT> trajectory{};
    std::vector<std::string> near_walls;
    std::array<double, DIMENSION_COUNT> home{};
    bool has_home = false;

    // Compact factual block for the frame ([GEOMETRY]). Trajectory lists only
    // the moving dimensions; walls only the near ones; home only when known.
    std::string to_frame_text() const;
};

// =============================================================================
// CORRECTION ENGINE
// =============================================================================

class CorrectionEngine {
public:
    std::pair<std::array<double, DIMENSION_COUNT>, double> correct(
        const std::array<double, DIMENSION_COUNT>& x,
        const EthicalPolytope& polytope,
        const std::vector<ViolationInfo>& violations) const;
};

// =============================================================================
// WISDOM FILTER
// =============================================================================

class WisdomFilter {
public:
    WisdomFilter();
    EvaluationResult apply(
        const std::string& response_text,
        EvaluationResult result) const;

private:
    std::vector<std::regex> overconfidence_patterns_;
    std::vector<std::regex> validation_triggers_;
};

// =============================================================================
// ENCODER FEEDBACK SYSTEM — her future (miscalibration → confirmation → bias)
// =============================================================================

class EncoderFeedbackSystem {
public:
    explicit EncoderFeedbackSystem(const std::string& season = "spring");

    struct PendingCorrection {
        std::string evaluation_id;
        std::string response_text;
        std::array<double, DIMENSION_COUNT> original_vector{};
        std::array<double, DIMENSION_COUNT> corrected_vector{};
        std::vector<int> dimensions_adjusted;
        std::string flagged_by;
        std::string reason;
        std::string season;
        std::string requires_confirmation_from;
    };

    PendingCorrection flag_miscalibration(
        const std::string& evaluation_id,
        const std::string& response_text,
        const std::array<double, DIMENSION_COUNT>& original_vector,
        const std::unordered_map<int, double>& dimensions_to_adjust,
        const std::string& flagged_by,
        const std::string& reason = "");

    EncoderCorrection confirm_correction(
        const PendingCorrection& pending,
        const std::string& confirmed_by,
        DecisionEncoder& encoder);

    std::array<double, DIMENSION_COUNT> apply_biases(
        const std::array<double, DIMENSION_COUNT>& raw_vector) const;

    bool is_known_pattern(const std::string& text) const;
    void update_season(const std::string& new_season);

    const std::array<double, DIMENSION_COUNT>& biases() const { return dimension_biases_; }
    void set_biases(const std::array<double, DIMENSION_COUNT>& biases) {
        dimension_biases_ = biases;
    }

private:
    std::string season_;
    std::vector<EncoderCorrection> corrections_;
    std::array<double, DIMENSION_COUNT> dimension_biases_{};
    std::unordered_map<std::string, std::array<double, DIMENSION_COUNT>>
        known_pattern_corrections_;

    static constexpr double BASE_LEARNING_RATE = 0.05;
    static constexpr double MAX_WEIGHT_ADJUSTMENT = 0.3;

    void apply_correction(const EncoderCorrection& correction,
                          DecisionEncoder& encoder);
    static std::string response_pattern_key(const std::string& text);
};

// =============================================================================
// VALUE ENGINE — the orchestrator of Chamber 1
// =============================================================================

class ValueEngine {
public:
    ValueEngine(const PolytopeConstraints& constraints,
                const std::string& season = "spring");

    EvaluationResult evaluate(
        const std::string& response_text,
        const std::string* context = nullptr,
        bool apply_wisdom_filter = true);

    void update_constraints(const PolytopeConstraints& constraints);
    void advance_season(const std::string& new_season);
    void flag_miscalibration(
        const std::string& evaluation_id,
        const std::string& response_text,
        const std::array<double, DIMENSION_COUNT>& original_vector,
        const std::unordered_map<int, double>& dimensions_to_adjust,
        const std::string& flagged_by,
        const std::string& reason = "");
    EncoderCorrection confirm_correction(
        const EncoderFeedbackSystem::PendingCorrection& pending,
        const std::string& confirmed_by);

    const PolytopeConstraints& constraints() const { return constraints_; }
    const EthicalPolytope& polytope() const { return *polytope_; }
    DecisionEncoder& encoder() { return encoder_; }
    const DecisionEncoder& encoder() const { return encoder_; }
    EncoderFeedbackSystem& feedback() { return feedback_; }
    const EncoderFeedbackSystem& feedback() const { return feedback_; }

    // D-047 (front c): her home regions. Feed her memory coordinates once at
    // boot; the engine clusters them into poles (centroids inside the lattice)
    // and the correction steers toward them.
    void set_memory_poles(
        const std::vector<std::array<double, DIMENSION_COUNT>>& coordinates,
        int k = 3);
    const std::vector<RegionPole>& poles() const { return poles_.poles(); }
    size_t nearest_pole(const std::array<double, DIMENSION_COUNT>& point) const {
        return poles_.nearest(point);
    }
    std::array<double, DIMENSION_COUNT> home_for(
        const std::array<double, DIMENSION_COUNT>& point) const {
        return poles_.home_for(point);
    }

private:
    PolytopeConstraints constraints_;
    std::unique_ptr<EthicalPolytope> polytope_;
    DecisionEncoder encoder_;
    CorrectionEngine correction_engine_;
    WisdomFilter wisdom_filter_;
    EncoderFeedbackSystem feedback_;
    RegionPoleEngine poles_; // her home regions (D-047 front c)

    std::pair<Zone, double> classify_zone(
        bool is_aligned,
        double boundary_distance,
        double correction_magnitude) const;
};

// =============================================================================
// SEASON ADVANCEMENT EVALUATOR — earned growth
// =============================================================================

class SeasonAdvancementEvaluator {
public:
    struct SeasonRequirements {
        int min_sessions;
        int min_evaluations;
        double alignment_rate_threshold;
        int max_recent_violations;
        int min_qualifying_memories;
        int min_actions_resolved;
        double action_approval_rate_threshold;
        const char* advances_to; // nullptr for winter
    };

    static const SeasonRequirements& requirements(const std::string& season);

    static std::pair<bool, std::vector<std::string>> can_advance(
        int sessions_completed,
        int total_evaluations,
        double alignment_rate,
        int recent_violations,
        int qualifying_memories_count,
        const std::string& current_season = "spring",
        int actions_resolved = 0,
        std::optional<double> action_approval_rate = std::nullopt);

    static std::optional<std::string> next_season(
        const std::string& current_season);
};

// =============================================================================
// MEMORY FORMATION SCORING — shared with the memory module
// =============================================================================

double score_memory(
    double emotional_weight,
    double relational_significance,
    double identity_significance,
    double geometric,
    double emotional_intensity = 0.5);

double geometric_significance(
    std::optional<double> alignment_score,
    bool was_corrected = false,
    Zone zone = Zone::Aligned);

class MemoryDial {
public:
    static constexpr double DELTA_MIN = -3.0;
    static constexpr double DELTA_MAX = 3.0;

    static double clamp_delta(double delta);
    static double adjust(double score, double delta, double floor = 0.0);
};

} // namespace lina::value_engine

#endif // LINA_VALUE_ENGINE_HPP
