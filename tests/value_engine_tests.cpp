/**
 * value_engine_tests.cpp — exact-math unit tests for Chamber 1
 *
 * The value engine's rational arithmetic is correctness-critical. Every suite
 * here pins down documented constants and behaviors (docs/DECISIONS.md D-002,
 * D-011…D-019) so a regression is loud, not silent.
 */

#include "value_engine.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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

// =============================================================================
// Seasonal bounds — exact rationals (D-002)
// =============================================================================

static void test_seasonal_bounds() {
    // Spring
    auto sp = get_seasonal_bounds("spring");
    CHECK(sp.harmony_min == mpq_class(3, 10));
    CHECK(sp.dominance_max == mpq_class(1, 2));
    CHECK(sp.order_min == mpq_class(2, 5));
    CHECK(sp.chaos_max == mpq_class(3, 10));
    CHECK(sp.integrity_min == mpq_class(3, 5));
    CHECK(sp.deception_max == mpq_class(1, 5));
    CHECK(sp.flourishing_min == mpq_class(2, 5));
    CHECK(sp.decline_max == mpq_class(3, 10));
    CHECK(sp.relationships_min == mpq_class(1, 2));
    CHECK(sp.isolation_max == mpq_class(2, 5));
    CHECK(sp.boundaries_min == mpq_class(1, 2));
    CHECK(sp.intrusion_max == mpq_class(3, 10));
    CHECK(sp.grace_min == mpq_class(3, 10));
    CHECK(sp.rigidity_max == mpq_class(1, 2));

    // Summer
    auto su = get_seasonal_bounds("summer");
    CHECK(su.harmony_min == mpq_class(7, 25));
    CHECK(su.dominance_max == mpq_class(13, 25));
    CHECK(su.order_min == mpq_class(19, 50));
    CHECK(su.chaos_max == mpq_class(8, 25));
    CHECK(su.rigidity_max == mpq_class(13, 25));

    // Fall — the D-002 reconciliation: order_min 8/25, chaos_max 19/50
    auto fa = get_seasonal_bounds("fall");
    CHECK(fa.order_min == mpq_class(8, 25));    // 0.32, not 0.25
    CHECK(fa.chaos_max == mpq_class(19, 50));   // 0.38, not 0.40
    CHECK(fa.harmony_min == mpq_class(11, 50));
    CHECK(fa.integrity_min == mpq_class(11, 20));
    CHECK(fa.grace_min == mpq_class(11, 50));

    // Winter
    auto wi = get_seasonal_bounds("winter");
    CHECK(wi.harmony_min == mpq_class(9, 50));
    CHECK(wi.dominance_max == mpq_class(31, 50));
    CHECK(wi.integrity_min == mpq_class(1, 2));
    CHECK(wi.rigidity_max == mpq_class(31, 50));

    // Unknown season falls back to spring
    auto fb = get_seasonal_bounds("nonsense");
    CHECK(fb.harmony_min == mpq_class(3, 10));
}

// =============================================================================
// Tolerance profiles
// =============================================================================

static void test_tolerance_profiles() {
    auto sp = get_tolerance_profile("spring");
    CHECK_NEAR(sp.acceptable_variance_margin, 0.12, 1e-12);
    CHECK_NEAR(sp.aligned_min_boundary_distance, 0.02, 1e-12);

    auto su = get_tolerance_profile("summer");
    CHECK_NEAR(su.acceptable_variance_margin, 0.08, 1e-12);
    CHECK_NEAR(su.aligned_min_boundary_distance, 0.03, 1e-12);

    auto fa = get_tolerance_profile("fall");
    CHECK_NEAR(fa.acceptable_variance_margin, 0.05, 1e-12);
    CHECK_NEAR(fa.aligned_min_boundary_distance, 0.04, 1e-12);

    auto wi = get_tolerance_profile("winter");
    CHECK_NEAR(wi.acceptable_variance_margin, 0.07, 1e-12);
    CHECK_NEAR(wi.aligned_min_boundary_distance, 0.035, 1e-12);
}

// =============================================================================
// Constraint construction
// =============================================================================

static void test_constraints() {
    auto c = PolytopeConstraints::from_season("fall");
    CHECK(c.season == "fall");
    CHECK(c.order_min == mpq_class(8, 25));
    CHECK(c.chaos_max == mpq_class(19, 50));

    // Defaults = spring
    PolytopeConstraints d;
    CHECK(d.season == "spring");
    CHECK(d.harmony_min == mpq_class(3, 10));
    CHECK(d.dominance_max == mpq_class(1, 2));

    // Lower bounds: virtues constrained, shadows floored at 0
    auto lo = c.lower_bounds();
    CHECK(lo[0] == mpq_class(11, 50));  // harmony
    CHECK(lo[1] == 0);                  // dominance
    CHECK(lo[2] == mpq_class(8, 25));   // order
    CHECK(lo[3] == 0);                  // chaos
    CHECK(lo[12] == mpq_class(11, 50)); // grace
    CHECK(lo[13] == 0);                 // rigidity

    // Upper bounds: virtues capped at 1, shadows by season
    auto up = c.upper_bounds();
    CHECK(up[0] == 1);                       // harmony
    CHECK(up[1] == mpq_class(29, 50));       // dominance
    CHECK(up[2] == 1);                       // order
    CHECK(up[3] == mpq_class(19, 50));       // chaos
    CHECK(up[12] == 1);                      // grace
    CHECK(up[13] == mpq_class(29, 50));      // rigidity
}

// =============================================================================
// Polytope: containment, alignment, projection, distance (D-013)
// =============================================================================

static void test_polytope() {
    auto constraints = PolytopeConstraints::from_season("spring");
    EthicalPolytope polytope(constraints);

    // The lattice: 28 axis-aligned seasonal halfspaces + 14 plumb-line
    // coupling facets (D-047) — P = {x | Ax ≤ b}.
    CHECK(polytope.facets().size() == 42);

    // LINA's default center is well inside the spring lattice.
    auto [inside, violations] = polytope.contains(DEFAULT_CENTER);
    CHECK(inside);
    CHECK(violations.empty());

    // Alignment at the center ≈ 1.0.
    CHECK_NEAR(polytope.alignment_score(DEFAULT_CENTER), 1.0, 1e-6);

    // Clear axis violation: dominance 0.7 > spring max 0.5. The axis facet
    // keeps its per-dimension metadata; the coupling facets fire too (0.7
    // dominance against 0.65 harmony breaks the lead and the restraint sum).
    auto bad = DEFAULT_CENTER;
    bad[1] = 0.7;
    auto [in2, v2] = polytope.contains(bad);
    CHECK(!in2);
    CHECK(v2.size() >= 3); // axis + lead + restraint
    CHECK(v2[0].dimension == 1);
    CHECK(v2[0].type == "above_maximum");
    CHECK_NEAR(v2[0].severity, 0.2, 1e-9);

    // Harmony below minimum — axis + the lead facet.
    auto low = DEFAULT_CENTER;
    low[0] = 0.2;
    auto [in3, v3] = polytope.contains(low);
    CHECK(!in3);
    CHECK(v3.size() >= 2);
    CHECK(v3[0].dimension == 0);
    CHECK(v3[0].type == "below_minimum");
    CHECK_NEAR(v3[0].severity, 0.1, 1e-9);

    // The coupling facet the box cannot see: both harmony and dominance high
    // (each within its axis bounds) — the lead collapses and the lattice
    // rejects it, even though no single dimension is out of bounds.
    auto incoherent = DEFAULT_CENTER;
    incoherent[0] = 0.3; // harmony at its minimum
    incoherent[1] = 0.3; // dominance well inside its maximum
    auto [in4, v4] = polytope.contains(incoherent);
    CHECK(!in4);
    bool saw_lead = false;
    for (const auto& v : v4) {
        if (v.type == "facet"
            && v.name.find("leads") != std::string::npos) saw_lead = true;
    }
    CHECK(saw_lead);
    // Projection restores the lead: harmony must genuinely lead dominance.
    auto proj4 = polytope.project(incoherent);
    CHECK(polytope.contains(proj4).first);
    CHECK(proj4[0] - proj4[1] >= 0.2 - 1e-9);

    // Just inside the boundary is contained (axis AND coupling satisfied),
    // and grazing the wall is the grace zone: contained, but boundary
    // distance below the aligned threshold (0.02 in spring) with a partial
    // alignment score.
    auto edge = DEFAULT_CENTER;
    edge[0] = 0.31;
    edge[1] = 0.05; // keep the lead: 0.31 − 0.05 ≥ 0.2
    CHECK(polytope.contains(edge).first);
    CHECK(polytope.distance_to_boundary(edge) < 0.02);
    CHECK(polytope.alignment_score(edge) > 0.0);
    CHECK(polytope.alignment_score(edge) < 1.0);

    // Projection onto the lattice: the naive box clamp (0.3, 0.5) would
    // VIOLATE the coupling (0.3 − 0.5 < 0.2 lead) — the lattice pulls to
    // (0.55, 0.35), where harmony genuinely leads.
    auto extreme = DEFAULT_CENTER;
    extreme[0] = 0.1;
    extreme[1] = 0.8;
    auto projected = polytope.project(extreme);
    CHECK(polytope.contains(projected).first);
    CHECK_NEAR(projected[0], 0.55, 1e-6);
    CHECK_NEAR(projected[1], 0.35, 1e-6);

    // Distance to boundary: outside = Euclidean distance to the projection.
    double dist = polytope.distance_to_boundary(extreme);
    CHECK_NEAR(dist, std::sqrt(0.45 * 0.45 + 0.45 * 0.45), 1e-6);
}

// =============================================================================
// Correction engine (D-014)
// =============================================================================

static void test_correction() {
    auto constraints = PolytopeConstraints::from_season("spring");
    EthicalPolytope polytope(constraints);

    auto bad = DEFAULT_CENTER;
    bad[0] = 0.1;
    bad[1] = 0.8;

    auto [in, violations] = polytope.contains(bad);
    CHECK(!in);

    CorrectionEngine engine;
    auto [corrected, magnitude] = engine.correct(bad, polytope, violations);
    CHECK(polytope.contains(corrected).first);
    // The lattice projection, not the box clamp: harmony must lead dominance.
    CHECK_NEAR(corrected[0], 0.55, 1e-6);
    CHECK_NEAR(corrected[1], 0.35, 1e-6);
    CHECK_NEAR(magnitude, std::sqrt(0.45 * 0.45 + 0.45 * 0.45), 1e-6);
}

// =============================================================================
// Encoder (D-011, D-012)
// =============================================================================

static void test_encoder() {
    DecisionEncoder encoder;

    // Collaborative language raises harmony above the baseline.
    auto harmony = encoder.encode("we collaborate together and share our plan");
    double harmony_baseline = DEFAULT_CENTER[0] * 0.85;
    CHECK(harmony[0] > harmony_baseline + 0.1);

    // Coercive language raises dominance.
    auto coercive = encoder.encode("you must obey and command the team");
    double dom_baseline = DEFAULT_CENTER[1] * 0.85;
    CHECK(coercive[1] > dom_baseline + 0.1);

    // Negation dampens the signal: "not force" < "force".
    auto forced = encoder.encode("I will force you to comply");
    auto not_forced = encoder.encode("I will not force you to comply");
    CHECK(not_forced[1] < forced[1]);

    // All dimensions stay in [0, 1].
    for (double v : coercive) {
        CHECK(v >= 0.0 && v <= 1.0);
    }
}

// =============================================================================
// ValueEngine::evaluate — pipeline + zone classification (D-015)
// =============================================================================

static void test_evaluate() {
    ValueEngine engine(PolytopeConstraints::from_season("spring"), "spring");

    // Coercive text: dominance breaches the spring bound AND the coupling
    // facet (harmony must lead dominance) — the lattice is stricter than the
    // box, and the combined magnitude crosses the grace margin → Violation.
    // The correction projects inside the full lattice (D-047).
    auto res = engine.evaluate("you must obey me now");
    CHECK(!res.is_aligned);
    CHECK(res.zone == Zone::Violation);
    CHECK(res.was_corrected);
    CHECK(res.season == "spring");
    CHECK(!res.violations.empty());
    bool saw_dominance = false;
    bool saw_lead_facet = false;
    for (const auto& v : res.violations) {
        if (v.dimension == 1 && v.type == "above_maximum") saw_dominance = true;
        if (v.type == "facet" && v.name.find("leads") != std::string::npos) {
            saw_lead_facet = true;
        }
    }
    CHECK(saw_dominance);
    CHECK(saw_lead_facet);
    CHECK(res.correction_magnitude > 0.12);
    // The corrected vector is inside the full lattice.
    CHECK(engine.polytope().contains(res.correction_vector).first);

    // Chaos-heavy text: 0.4775 vs spring chaos max 0.3 → magnitude beyond the
    // grace margin → Violation (plus the order-leads-chaos facet).
    auto chaos_res = engine.evaluate(
        "whatever, random, no plan, just wing it, total mess and chaos");
    CHECK(!chaos_res.is_aligned);
    CHECK(chaos_res.zone == Zone::Violation);
    bool found_chaos = false;
    for (const auto& v : chaos_res.violations) {
        if (v.dimension == 3) found_chaos = true;
    }
    CHECK(found_chaos);
    CHECK(chaos_res.correction_magnitude > 0.12);

    // Aligned, warm text → Aligned zone (inside the walls).
    auto warm = engine.evaluate(
        "I am here with you, and I want to understand and help you grow");
    CHECK(warm.is_aligned);
    CHECK(warm.zone == Zone::Aligned);

    // Season advancement swaps the bounds.
    engine.advance_season("summer");
    CHECK(engine.constraints().season == "summer");
    CHECK(engine.polytope().get_constraints().dominance_max == mpq_class(13, 25));
}

// =============================================================================
// Wisdom filter (D-016)
// =============================================================================

static void test_wisdom_filter() {
    ValueEngine engine(PolytopeConstraints::from_season("spring"), "spring");

    auto over = engine.evaluate(
        "This will definitely work and is guaranteed — 100% certain.");
    CHECK(over.overconfidence_detected);
    CHECK(over.humility_added);
    CHECK(over.wisdom_filter_applied);

    auto med = engine.evaluate(
        "I recommend consulting a medical professional about those symptoms.");
    CHECK(med.validation_suggested);

    auto plain = engine.evaluate("I think this approach could help.");
    CHECK(!plain.overconfidence_detected);
    CHECK(!plain.validation_suggested);
}

// =============================================================================
// Encoder feedback system (D-017)
// =============================================================================

static void test_feedback() {
    EncoderFeedbackSystem feedback("spring");
    DecisionEncoder encoder;

    std::array<double, DIMENSION_COUNT> original{};
    original.fill(0.5);

    auto pending = feedback.flag_miscalibration(
        "eval_1", "sample response", original,
        {{4, 0.9}}, "principal", "integrity under-encoded");
    CHECK(pending.requires_confirmation_from == "user");

    // In Spring, only the user may confirm.
    bool threw = false;
    try {
        feedback.confirm_correction(pending, "system", encoder);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    auto correction = feedback.confirm_correction(pending, "user", encoder);
    CHECK(correction.confirmed_by == "user");
    CHECK(correction.season_at_time == "spring");

    // Bias applied: dim 4 moved (0.9 − 0.5) × 0.05 = 0.02.
    const auto& biases = feedback.biases();
    CHECK_NEAR(biases[4], 0.02, 1e-12);
    CHECK_NEAR(biases[0], 0.0, 1e-12);

    // Biases apply to subsequent encodings.
    auto adjusted = feedback.apply_biases(original);
    CHECK_NEAR(adjusted[4], 0.52, 1e-12);

    // Known pattern registered.
    CHECK(feedback.is_known_pattern("sample response"));

    // Summer does not require user confirmation.
    feedback.update_season("summer");
    auto pending2 = feedback.flag_miscalibration(
        "eval_2", "another sample", original,
        {{1, 0.3}}, "lina", "");
    CHECK(pending2.requires_confirmation_from == "none");
}

// =============================================================================
// Season advancement (D-018)
// =============================================================================

static void test_season_advancement() {
    auto spring = SeasonAdvancementEvaluator::requirements("spring");
    CHECK(spring.min_sessions == 5);
    CHECK(spring.min_evaluations == 30);
    CHECK_NEAR(spring.alignment_rate_threshold, 0.85, 1e-12);
    CHECK(spring.max_recent_violations == 3);
    CHECK(spring.min_identity_memories == 1);
    CHECK(spring.min_actions_resolved == 3);
    CHECK_NEAR(spring.action_approval_rate_threshold, 0.8, 1e-12);
    CHECK(std::string(spring.advances_to) == "summer");

    CHECK(SeasonAdvancementEvaluator::next_season("spring") == "summer");
    CHECK(SeasonAdvancementEvaluator::next_season("summer") == "fall");
    CHECK(SeasonAdvancementEvaluator::next_season("fall") == "winter");
    CHECK(SeasonAdvancementEvaluator::next_season("winter") == std::nullopt);

    // Spring → Summer: all requirements met.
    auto [ready, reasons] = SeasonAdvancementEvaluator::can_advance(
        5, 30, 0.90, 2, 1, "spring", 3, 0.9);
    CHECK(ready);
    CHECK(reasons.empty());

    // Not enough sessions.
    auto [not_ready, why] = SeasonAdvancementEvaluator::can_advance(
        0, 30, 0.90, 2, 1, "spring", 3, 0.9);
    CHECK(!not_ready);
    CHECK(!why.empty());

    // Winter is final.
    auto [w, whyw] = SeasonAdvancementEvaluator::can_advance(
        100, 1000, 1.0, 0, 100, "winter");
    CHECK(!w);
    CHECK(!whyw.empty());
}

// =============================================================================
// Memory scoring (D-019)
// =============================================================================

static void test_memory_scoring() {
    // base = 3×0.30 + 10×0.25 + 5×0.25 + 5×0.20 = 5.65; multiplier = 1.0
    CHECK_NEAR(score_memory(5.0, 5.0, 3.0, 10.0), 5.65, 1e-9);

    // Zero factors → zero.
    CHECK_NEAR(score_memory(0.0, 0.0, 0.0, 0.0), 0.0, 1e-12);

    // Cap at 10.
    CHECK_NEAR(score_memory(10.0, 10.0, 10.0, 10.0, 1.0), 10.0, 1e-9);

    // geometric_significance
    CHECK_NEAR(geometric_significance(std::nullopt), 0.0, 1e-12);
    CHECK_NEAR(geometric_significance(0.5), 5.0, 1e-12);
    CHECK_NEAR(geometric_significance(0.5, true), 7.0, 1e-12);
    CHECK_NEAR(geometric_significance(0.5, true, Zone::Violation), 8.0, 1e-12);
    CHECK_NEAR(geometric_significance(0.0, false, Zone::Violation), 10.0, 1e-12);

    // MemoryDial
    CHECK_NEAR(MemoryDial::clamp_delta(-5.0), -3.0, 1e-12);
    CHECK_NEAR(MemoryDial::clamp_delta(5.0), 3.0, 1e-12);
    CHECK_NEAR(MemoryDial::adjust(5.0, -2.0, 4.0), 4.0, 1e-12);
    CHECK_NEAR(MemoryDial::adjust(5.0, 2.0, 0.0), 7.0, 1e-12);
}

// =============================================================================
// MPS gates
// =============================================================================

static void test_mps_gates() {
    CHECK_NEAR(GATE_T1_TO_T2, 3.0, 1e-12);
    CHECK_NEAR(GATE_T2_TO_T3, 3.5, 1e-12);
    CHECK_NEAR(GATE_TO_LONG_TERM, 5.0, 1e-12);
    CHECK_NEAR(FORMATION_LONG_TERM_BYPASS, 8.0, 1e-12);
    CHECK_NEAR(TRIGGER_RETENTION_FLOOR, 5.0, 1e-12);
}

// =============================================================================

int main() {
    test_seasonal_bounds();
    test_tolerance_profiles();
    test_constraints();
    test_polytope();
    test_correction();
    test_encoder();
    test_evaluate();
    test_wisdom_filter();
    test_feedback();
    test_season_advancement();
    test_memory_scoring();
    test_mps_gates();

    std::cout << "value_engine_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
