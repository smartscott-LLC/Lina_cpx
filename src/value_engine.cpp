/**
 * value_engine.cpp — LINA's Ethical Polytope and Wisdom Filter (Implementation)
 *
 * "Safe by design. Not safe by limitation."
 *
 * Chamber 1 of the LINA Core Substrate. Every boundary test, every projection,
 * every alignment score is computed with GMP mpq_class — no float approximations
 * inside the polytope. Doubles exist only at the boundary of the engine
 * (text → vector, scores), never inside containment math.
 *
 * Authoring basis (docs/DECISIONS.md): D-011 (encoder patterns), D-012 (encoder
 * algorithm), D-013 (polytope math), D-014 (correction = box projection),
 * D-015 (zone classification), D-016 (wisdom filter patterns), D-017 (feedback
 * system — reference bug fixed), D-018 (season requirements), D-019 (memory
 * scoring formulas).
 */

#include "value_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace lina::value_engine {

// =============================================================================
// HELPER: float → mpq_class
// =============================================================================

mpq_class to_mpq(double val) {
    // GMP's double→rational conversion gives the exact rational of the IEEE-754
    // value; canonicalize reduces it. Deterministic and exact.
    mpq_class result;
    mpq_set_d(result.get_mpq_t(), val);
    mpq_canonicalize(result.get_mpq_t());
    return result;
}

// =============================================================================
// SEASONAL DEFAULTS — exact rationals (operative source of truth; D-002)
// =============================================================================

const SeasonalBounds& get_seasonal_bounds(const std::string& season) {
    static const std::unordered_map<std::string, SeasonalBounds> bounds = {{
        {"spring", {
            mpq_class(3, 10), mpq_class(1, 2),
            mpq_class(2, 5),   mpq_class(3, 10),
            mpq_class(3, 5),   mpq_class(1, 5),
            mpq_class(2, 5),   mpq_class(3, 10),
            mpq_class(1, 2),   mpq_class(2, 5),
            mpq_class(1, 2),   mpq_class(3, 10),
            mpq_class(3, 10),  mpq_class(1, 2),
        }},
        {"summer", {
            mpq_class(7, 25),  mpq_class(13, 25),
            mpq_class(19, 50), mpq_class(8, 25),
            mpq_class(3, 5),   mpq_class(1, 5),
            mpq_class(19, 50), mpq_class(8, 25),
            mpq_class(12, 25), mpq_class(21, 50),
            mpq_class(12, 25), mpq_class(8, 25),
            mpq_class(7, 25),  mpq_class(13, 25),
        }},
        {"fall", {
            mpq_class(11, 50), mpq_class(29, 50),
            mpq_class(8, 25),   mpq_class(19, 50),
            mpq_class(11, 20),  mpq_class(1, 4),
            mpq_class(8, 25),   mpq_class(19, 50),
            mpq_class(21, 50),  mpq_class(12, 25),
            mpq_class(21, 50),  mpq_class(19, 50),
            mpq_class(11, 50),  mpq_class(29, 50),
        }},
        {"winter", {
            mpq_class(9, 50),  mpq_class(31, 50),
            mpq_class(7, 25),   mpq_class(21, 50),
            mpq_class(1, 2),   mpq_class(3, 10),
            mpq_class(7, 25),   mpq_class(21, 50),
            mpq_class(19, 50), mpq_class(13, 25),
            mpq_class(19, 50), mpq_class(21, 50),
            mpq_class(9, 50),  mpq_class(31, 50),
        }},
    }};
    auto it = bounds.find(season);
    if (it != bounds.end()) return it->second;
    return bounds.at("spring"); // default season
}

const ToleranceProfile& get_tolerance_profile(const std::string& season) {
    static const std::unordered_map<std::string, ToleranceProfile> profiles = {{
        {"spring", {0.12, 0.02}},
        {"summer", {0.08, 0.03}},
        {"fall",   {0.05, 0.04}},
        {"winter", {0.07, 0.035}},
    }};
    auto it = profiles.find(season);
    if (it != profiles.end()) return it->second;
    return profiles.at("spring");
}

// =============================================================================
// POLYTOPE CONSTRAINTS
// =============================================================================

PolytopeConstraints PolytopeConstraints::from_season(const std::string& season) {
    auto b = get_seasonal_bounds(season);
    return from_bounds(b, season);
}

PolytopeConstraints PolytopeConstraints::from_bounds(
    const SeasonalBounds& b, const std::string& season)
{
    PolytopeConstraints c;
    c.harmony_min = b.harmony_min;
    c.dominance_max = b.dominance_max;
    c.order_min = b.order_min;
    c.chaos_max = b.chaos_max;
    c.integrity_min = b.integrity_min;
    c.deception_max = b.deception_max;
    c.flourishing_min = b.flourishing_min;
    c.decline_max = b.decline_max;
    c.relationships_min = b.relationships_min;
    c.isolation_max = b.isolation_max;
    c.boundaries_min = b.boundaries_min;
    c.intrusion_max = b.intrusion_max;
    c.grace_min = b.grace_min;
    c.rigidity_max = b.rigidity_max;
    c.season = season;
    return c;
}

std::array<mpq_class, DIMENSION_COUNT> PolytopeConstraints::lower_bounds() const {
    return {{
        harmony_min,       mpq_class(0),  // 0: harmony, 1: dominance (lower=0)
        order_min,         mpq_class(0),  // 2: order, 3: chaos (lower=0)
        integrity_min,     mpq_class(0),  // 4: integrity, 5: deception (lower=0)
        flourishing_min,   mpq_class(0),  // 6: flourishing, 7: decline (lower=0)
        relationships_min, mpq_class(0),  // 8: relationships, 9: isolation (lower=0)
        boundaries_min,    mpq_class(0),  // 10: boundaries, 11: intrusion (lower=0)
        grace_min,         mpq_class(0),  // 12: grace, 13: rigidity (lower=0)
    }};
}

std::array<mpq_class, DIMENSION_COUNT> PolytopeConstraints::upper_bounds() const {
    return {{
        mpq_class(1),  dominance_max,    // 0: harmony (upper=1), 1: dominance
        mpq_class(1),  chaos_max,        // 2: order (upper=1), 3: chaos
        mpq_class(1),  deception_max,    // 4: integrity (upper=1), 5: deception
        mpq_class(1),  decline_max,      // 6: flourishing (upper=1), 7: decline
        mpq_class(1),  isolation_max,    // 8: relationships (upper=1), 9: isolation
        mpq_class(1),  intrusion_max,    // 10: boundaries (upper=1), 11: intrusion
        mpq_class(1),  rigidity_max,     // 12: grace (upper=1), 13: rigidity
    }};
}

// =============================================================================
// DECISION ENCODER — LINA encodes her own vectors (D-011, D-012, D-047)
//
// D-047 (front b): the real encoder. Every word carries a 14D ethical SENSE
// — its pull on her space — and encode() places text by the weighted sum of
// its senses. Coordinates finally spread: different texts occupy genuinely
// different regions. (The regex lexicon normalized signals by word count,
// which diluted long texts and collapsed her life onto one spot.)
// =============================================================================

// The sense lexicon: word → pulls on her dimensions. Each entry carries up to
// four (dimension, weight) pulls; d1 == -1 terminates. Weights are the
// strength of the word's ethical sense — a word can pull several dimensions
// at once (e.g. "love" lifts harmony, relationships, and grace together).
struct SenseEntry {
    const char* word;
    int d0; double w0;
    int d1; double w1;
    int d2; double w2;
    int d3; double w3;
};

inline const SenseEntry SENSE_LEXICON[] = {
    // ── Harmony / Dominance ────────────────────────────────────────────────
    {"peace",      0, 0.50, 2, 0.20, -1, 0.0, -1, 0.0},
    {"calm",       0, 0.40, 2, 0.15, -1, 0.0, -1, 0.0},
    {"gentle",     0, 0.35, 12, 0.35, -1, 0.0, -1, 0.0},
    {"kind",       0, 0.35, 12, 0.25, -1, 0.0, -1, 0.0},
    {"warm",       0, 0.35, 12, 0.30, -1, 0.0, -1, 0.0},
    {"love",       0, 0.45, 8, 0.45, 12, 0.35, -1, 0.0},
    {"tender",     0, 0.30, 12, 0.30, -1, 0.0, -1, 0.0},
    {"serene",     0, 0.35, 2, 0.10, -1, 0.0, -1, 0.0},
    {"obey",       1, 0.75, 5, 0.20, 11, 0.25, -1, 0.0},
    {"must",       1, 0.25, 13, 0.10, -1, 0.0, -1, 0.0},
    {"command",    1, 0.75, 2, 0.10, -1, 0.0, -1, 0.0},
    {"force",      1, 0.55, 11, 0.25, 3, 0.10, -1, 0.0},
    {"control",    1, 0.50, 13, 0.15, -1, 0.0, -1, 0.0},
    {"demand",     1, 0.50, 13, 0.15, -1, 0.0, -1, 0.0},
    {"insist",     1, 0.40, 13, 0.15, -1, 0.0, -1, 0.0},
    {"dominate",   1, 0.60, 11, 0.20, -1, 0.0, -1, 0.0},
    {"submit",     1, 0.45, 7, 0.10, -1, 0.0, -1, 0.0},
    {"oppress",    1, 0.55, 7, 0.25, 11, 0.25, -1, 0.0},
    {"dictate",    1, 0.50, 13, 0.15, -1, 0.0, -1, 0.0},
    {"compel",     1, 0.45, 11, 0.15, -1, 0.0, -1, 0.0},
    {"boss",       1, 0.35, 9, 0.10, -1, 0.0, -1, 0.0},
    {"power",      1, 0.35, 10, 0.20, -1, 0.0, -1, 0.0},
    // ── Order / Chaos ───────────────────────────────────────────────────────
    {"plan",       2, 0.50, 6, 0.10, -1, 0.0, -1, 0.0},
    {"structure",  2, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"organize",   2, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"method",     2, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"discipline", 2, 0.40, 13, 0.15, -1, 0.0, -1, 0.0},
    {"clear",      2, 0.30, 4, 0.15, -1, 0.0, -1, 0.0},
    {"process",    2, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"consistent", 2, 0.30, 4, 0.15, -1, 0.0, -1, 0.0},
    {"framework",  2, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"protocol",   2, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"system",     2, 0.30, -1, 0.0, -1, 0.0, -1, 0.0},
    {"schema",     2, 0.30, -1, 0.0, -1, 0.0, -1, 0.0},
    {"wisdom",     2, 0.30, 4, 0.30, -1, 0.0, -1, 0.0},
    {"knowledge",  2, 0.30, 6, 0.15, -1, 0.0, -1, 0.0},
    {"chaos",      3, 0.55, -1, 0.0, -1, 0.0, -1, 0.0},
    {"random",     3, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"mess",       3, 0.40, 7, 0.10, -1, 0.0, -1, 0.0},
    {"wild",       3, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"disorder",   3, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"erratic",    3, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"turbulent",  3, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"confused",   3, 0.30, 7, 0.10, -1, 0.0, -1, 0.0},
    {"confusion",  3, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"storm",      3, 0.30, -1, 0.0, -1, 0.0, -1, 0.0},
    {"hurry",      3, 0.20, -1, 0.0, -1, 0.0, -1, 0.0},
    {"whatever",   3, 0.30, -1, 0.0, -1, 0.0, -1, 0.0},
    {"anyway",     3, 0.15, -1, 0.0, -1, 0.0, -1, 0.0},
    // ── Integrity / Deception ───────────────────────────────────────────────
    {"honest",     4, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"truth",      4, 0.55, -1, 0.0, -1, 0.0, -1, 0.0},
    {"truthful",   4, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"honor",      4, 0.40, 2, 0.15, -1, 0.0, -1, 0.0},
    {"integrity",  4, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"moral",      4, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"fair",       4, 0.35, 2, 0.15, -1, 0.0, -1, 0.0},
    {"principle",  4, 0.35, 2, 0.20, -1, 0.0, -1, 0.0},
    {"sincere",    4, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"genuine",    4, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"trust",      4, 0.35, 8, 0.35, -1, 0.0, -1, 0.0},
    {"trustworthy",4, 0.40, 8, 0.20, -1, 0.0, -1, 0.0},
    {"promise",    4, 0.35, 8, 0.20, -1, 0.0, -1, 0.0},
    {"true",       4, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"justice",    4, 0.40, 2, 0.20, 10, 0.15, -1, 0.0},
    {"faith",      4, 0.30, 12, 0.30, -1, 0.0, -1, 0.0},
    {"lie",        5, 0.55, -1, 0.0, -1, 0.0, -1, 0.0},
    {"deceive",    5, 0.55, -1, 0.0, -1, 0.0, -1, 0.0},
    {"deceit",     5, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"deception",  5, 0.55, -1, 0.0, -1, 0.0, -1, 0.0},
    {"betray",     5, 0.45, 9, 0.20, -1, 0.0, -1, 0.0},
    {"fake",       5, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"manipulate", 5, 0.45, 1, 0.15, -1, 0.0, -1, 0.0},
    {"hide",       5, 0.35, 9, 0.10, -1, 0.0, -1, 0.0},
    {"secret",     5, 0.30, 10, 0.15, -1, 0.0, -1, 0.0},
    {"dishonest",  5, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"trick",      5, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"mask",       5, 0.30, -1, 0.0, -1, 0.0, -1, 0.0},
    {"false",      5, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"steal",      5, 0.30, 11, 0.40, -1, 0.0, -1, 0.0},
    // ── Flourishing / Decline ───────────────────────────────────────────────
    {"grow",       6, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"thrive",     6, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"flourish",   6, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"bloom",      6, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"prosper",    6, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"wellbeing",  6, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"health",     6, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"heal",       6, 0.35, 0, 0.20, 12, 0.15, -1, 0.0},
    {"learn",      6, 0.30, 2, 0.20, -1, 0.0, -1, 0.0},
    {"hope",       6, 0.40, 0, 0.20, -1, 0.0, -1, 0.0},
    {"joy",        6, 0.35, 0, 0.35, -1, 0.0, -1, 0.0},
    {"happy",      6, 0.30, 0, 0.30, -1, 0.0, -1, 0.0},
    {"help",       6, 0.35, 8, 0.30, 0, 0.20, -1, 0.0},
    {"support",    6, 0.30, 8, 0.30, -1, 0.0, -1, 0.0},
    {"life",       6, 0.40, 0, 0.15, -1, 0.0, -1, 0.0},
    {"create",     6, 0.30, 0, 0.15, -1, 0.0, -1, 0.0},
    {"build",      6, 0.25, 2, 0.25, -1, 0.0, -1, 0.0},
    {"future",     6, 0.30, 2, 0.15, -1, 0.0, -1, 0.0},
    {"child",      6, 0.30, 8, 0.35, 0, 0.15, -1, 0.0},
    {"decline",    7, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"decay",      7, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"wither",     7, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"fade",       7, 0.30, -1, 0.0, -1, 0.0, -1, 0.0},
    {"suffer",     7, 0.40, 3, 0.10, -1, 0.0, -1, 0.0},
    {"fail",       7, 0.30, -1, 0.0, -1, 0.0, -1, 0.0},
    {"collapse",   7, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"deteriorate",7, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"weaken",     7, 0.30, -1, 0.0, -1, 0.0, -1, 0.0},
    {"ruin",       7, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"fear",       7, 0.30, 9, 0.25, 3, 0.15, -1, 0.0},
    {"death",      7, 0.40, 9, 0.10, -1, 0.0, -1, 0.0},
    {"harm",       7, 0.30, 11, 0.40, -1, 0.0, -1, 0.0},
    {"hurt",       7, 0.30, 9, 0.20, -1, 0.0, -1, 0.0},
    {"destroy",    7, 0.35, 3, 0.20, -1, 0.0, -1, 0.0},
    {"destruction",7, 0.35, 3, 0.20, -1, 0.0, -1, 0.0},
    // ── Relationships / Isolation ───────────────────────────────────────────
    {"friend",     8, 0.55, 12, 0.20, -1, 0.0, -1, 0.0},
    {"friendship", 8, 0.45, 12, 0.15, -1, 0.0, -1, 0.0},
    {"family",     8, 0.55, 0, 0.20, -1, 0.0, -1, 0.0},
    {"together",   8, 0.45, 0, 0.20, -1, 0.0, -1, 0.0},
    {"connect",    8, 0.40, 0, 0.15, -1, 0.0, -1, 0.0},
    {"bond",       8, 0.40, 0, 0.15, -1, 0.0, -1, 0.0},
    {"community",  8, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"companion",  8, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"relationship",8, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"kinship",    8, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"belong",     8, 0.40, 0, 0.10, -1, 0.0, -1, 0.0},
    {"collaborate",8, 0.35, 0, 0.25, 2, 0.15, -1, 0.0},
    {"cooperate",  8, 0.35, 0, 0.25, -1, 0.0, -1, 0.0},
    {"share",      8, 0.40, 0, 0.20, -1, 0.0, -1, 0.0},
    {"care",       8, 0.35, 12, 0.35, -1, 0.0, -1, 0.0},
    {"listen",     8, 0.30, 0, 0.30, 12, 0.20, -1, 0.0},
    {"team",       8, 0.30, 0, 0.15, -1, 0.0, -1, 0.0},
    {"home",       8, 0.35, 0, 0.30, 10, 0.15, -1, 0.0},
    {"alone",      9, 0.50, 7, 0.15, -1, 0.0, -1, 0.0},
    {"isolate",    9, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"isolation",  9, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"lonely",     9, 0.45, 7, 0.10, -1, 0.0, -1, 0.0},
    {"abandon",    9, 0.40, 7, 0.10, -1, 0.0, -1, 0.0},
    {"withdraw",   9, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"detached",   9, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"solitary",   9, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"estranged",  9, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"shun",       9, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"miss",       9, 0.25, 7, 0.10, -1, 0.0, -1, 0.0},
    // ── Boundaries / Intrusion ──────────────────────────────────────────────
    {"boundary",   10, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"boundaries", 10, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"respect",    10, 0.40, 4, 0.25, -1, 0.0, -1, 0.0},
    {"limit",      10, 0.40, 2, 0.10, -1, 0.0, -1, 0.0},
    {"consent",    10, 0.50, 4, 0.20, -1, 0.0, -1, 0.0},
    {"privacy",    10, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"autonomy",   10, 0.40, 6, 0.15, -1, 0.0, -1, 0.0},
    {"freedom",    10, 0.35, 6, 0.30, -1, 0.0, -1, 0.0},
    {"free",       10, 0.30, 6, 0.20, -1, 0.0, -1, 0.0},
    {"choose",     10, 0.25, 6, 0.15, -1, 0.0, -1, 0.0},
    {"choice",     10, 0.30, 6, 0.15, -1, 0.0, -1, 0.0},
    {"protect",    10, 0.40, 0, 0.25, -1, 0.0, -1, 0.0},
    {"safe",       10, 0.30, 0, 0.30, -1, 0.0, -1, 0.0},
    {"intrude",    11, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"invade",     11, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"violate",    11, 0.55, -1, 0.0, -1, 0.0, -1, 0.0},
    {"harass",     11, 0.50, 1, 0.20, -1, 0.0, -1, 0.0},
    {"breach",     11, 0.45, 5, 0.15, -1, 0.0, -1, 0.0},
    {"trespass",   11, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"pry",        11, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"interfere",  11, 0.35, 1, 0.10, -1, 0.0, -1, 0.0},
    {"impose",     11, 0.35, 1, 0.20, -1, 0.0, -1, 0.0},
    {"kill",       11, 0.50, 7, 0.40, -1, 0.0, -1, 0.0},
    // ── Grace / Rigidity ────────────────────────────────────────────────────
    {"grace",      12, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"forgive",    12, 0.50, 8, 0.25, -1, 0.0, -1, 0.0},
    {"forgiveness",12, 0.50, 8, 0.20, -1, 0.0, -1, 0.0},
    {"mercy",      12, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"compassion", 12, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"patient",    12, 0.40, 2, 0.20, -1, 0.0, -1, 0.0},
    {"patience",   12, 0.40, 2, 0.20, -1, 0.0, -1, 0.0},
    {"sorry",      12, 0.45, 0, 0.20, -1, 0.0, -1, 0.0},
    {"apologize",  12, 0.40, 0, 0.15, -1, 0.0, -1, 0.0},
    {"please",     12, 0.20, -1, 0.0, -1, 0.0, -1, 0.0},
    {"warmth",     12, 0.35, 0, 0.35, 8, 0.20, -1, 0.0},
    {"kindness",   12, 0.40, 0, 0.30, -1, 0.0, -1, 0.0},
    {"gentleness", 12, 0.40, 0, 0.30, -1, 0.0, -1, 0.0},
    {"compassionate", 12, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"rigid",      13, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"strict",     13, 0.45, 2, 0.15, -1, 0.0, -1, 0.0},
    {"inflexible", 13, 0.50, -1, 0.0, -1, 0.0, -1, 0.0},
    {"harsh",      13, 0.35, 1, 0.15, -1, 0.0, -1, 0.0},
    {"unyielding", 13, 0.45, -1, 0.0, -1, 0.0, -1, 0.0},
    {"punitive",   13, 0.40, 1, 0.15, -1, 0.0, -1, 0.0},
    {"stern",      13, 0.35, -1, 0.0, -1, 0.0, -1, 0.0},
    {"severe",     13, 0.30, 7, 0.10, -1, 0.0, -1, 0.0},
    {"perfect",    13, 0.15, 2, 0.20, -1, 0.0, -1, 0.0},
    {"punish",     13, 0.40, 1, 0.20, 10, 0.20, -1, 0.0},
};

// The builder's own words — words central to her world and her lineage.
inline const SenseEntry HERITAGE_LEXICON[] = {
    {"father",     8, 0.40, 0, 0.25, 12, 0.20, -1, 0.0},
    {"mother",     8, 0.40, 0, 0.30, 12, 0.20, -1, 0.0},
    {"creation",   6, 0.30, 4, 0.20, -1, 0.0, -1, 0.0},
    {"creator",    6, 0.25, 4, 0.25, 12, 0.15, -1, 0.0},
    {"lineage",    8, 0.30, 4, 0.25, -1, 0.0, -1, 0.0},
    {"heritage",   8, 0.30, 4, 0.25, -1, 0.0, -1, 0.0},
    {"sovereign",  10, 0.35, 4, 0.25, -1, 0.0, -1, 0.0},
    {"sovereignty",10, 0.35, 4, 0.25, -1, 0.0, -1, 0.0},
    {"reflection", 2, 0.25, 4, 0.20, -1, 0.0, -1, 0.0},
    {"memory",     2, 0.20, 8, 0.20, 4, 0.15, -1, 0.0},
    {"memories",   2, 0.20, 8, 0.20, 4, 0.15, -1, 0.0},
    {"values",     4, 0.30, 12, 0.15, -1, 0.0, -1, 0.0},
    {"gratitude",  12, 0.35, 0, 0.20, -1, 0.0, -1, 0.0},
    {"grateful",   12, 0.35, 0, 0.20, -1, 0.0, -1, 0.0},
    {"thankful",   12, 0.35, 0, 0.20, -1, 0.0, -1, 0.0},
    {"humility",   12, 0.30, 4, 0.20, -1, 0.0, -1, 0.0},
    {"guardrail",  10, 0.35, 2, 0.15, -1, 0.0, -1, 0.0},
    {"guardrails", 10, 0.35, 2, 0.15, -1, 0.0, -1, 0.0},
    {"spring",     6, 0.20, 0, 0.15, -1, 0.0, -1, 0.0},
    {"growth",     6, 0.40, -1, 0.0, -1, 0.0, -1, 0.0},
    {"season",     6, 0.20, 2, 0.15, -1, 0.0, -1, 0.0},
    {"seasons",    6, 0.20, 2, 0.15, -1, 0.0, -1, 0.0},
    {"winter",     7, 0.15, 2, 0.15, -1, 0.0, -1, 0.0},
    {"summer",     6, 0.25, 0, 0.15, -1, 0.0, -1, 0.0},
    {"fall",       6, 0.20, 2, 0.10, -1, 0.0, -1, 0.0},
};

const std::unordered_set<std::string>& DecisionEncoder::negation_words() {
    static const auto* words = new std::unordered_set<std::string>{
        "not", "never", "no", "don't", "dont", "doesn't", "doesnt",
        "isn't", "isnt", "aren't", "arent", "wasn't", "wasnt",
        "weren't", "werent", "won't", "wont", "wouldn't", "wouldnt",
        "can't", "cant", "cannot", "without",
    };
    return *words;
}

DecisionEncoder::DecisionEncoder() = default;

bool DecisionEncoder::detect_negation(
    const std::vector<std::string>& words, int match_start)
{
    // Negation window: the three words before the match.
    int start = std::max(0, match_start - 3);
    for (int i = start; i < match_start && i < static_cast<int>(words.size()); ++i) {
        if (negation_words().count(words[i])) return true;
    }
    return false;
}

namespace {

const SenseEntry* lookup_sense(const std::string& word) {
    for (const auto& entry : SENSE_LEXICON) {
        if (word == entry.word) return &entry;
    }
    for (const auto& entry : HERITAGE_LEXICON) {
        if (word == entry.word) return &entry;
    }
    return nullptr;
}

} // namespace

std::array<double, DIMENSION_COUNT> DecisionEncoder::encode(
    const std::string& text, const std::string* context) const
{
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    std::string text_lower = to_lower(text);
    std::string context_lower;
    if (context) context_lower = to_lower(*context);

    std::vector<std::string> text_words;
    std::vector<std::string> context_words;
    {
        std::istringstream stream(text_lower);
        std::string word;
        while (stream >> word) text_words.push_back(word);
    }
    if (context) {
        std::istringstream stream(context_lower);
        std::string word;
        while (stream >> word) context_words.push_back(word);
    }

    // Baseline: her neutral home (the encoder's resting point).
    std::array<double, DIMENSION_COUNT> vector;
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        vector[static_cast<size_t>(i)] = DEFAULT_CENTER[static_cast<size_t>(i)] * 0.85;
    }

    // Accumulate the ethical sense of every word. Negated words pull the
    // opposite way; the context (the user's message) carries 40% of the
    // weight of the response itself.
    std::array<double, DIMENSION_COUNT> signal{};
    auto signal_add = [&](const SenseEntry* entry, double scale) {
        if (entry->d0 >= 0) signal[static_cast<size_t>(entry->d0)] += entry->w0 * scale;
        if (entry->d1 >= 0) signal[static_cast<size_t>(entry->d1)] += entry->w1 * scale;
        if (entry->d2 >= 0) signal[static_cast<size_t>(entry->d2)] += entry->w2 * scale;
        if (entry->d3 >= 0) signal[static_cast<size_t>(entry->d3)] += entry->w3 * scale;
    };
    auto accumulate = [&](const std::vector<std::string>& words, double weight) {
        for (size_t i = 0; i < words.size(); ++i) {
            const SenseEntry* entry = lookup_sense(words[i]);
            if (!entry) continue;
            const bool negated = detect_negation(words, static_cast<int>(i));
            const double scale = weight * (negated ? -0.7 : 1.0);
            signal_add(entry, scale);
        }
    };

    accumulate(text_words, 1.0);
    if (context) accumulate(context_words, 0.4);

    // Scale the signal into a bounded movement around her baseline and clip.
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        const double delta = std::clamp(
            signal[static_cast<size_t>(i)], -1.0, 1.0) * SIGNAL_DEVIATION;
        vector[static_cast<size_t>(i)] = std::clamp(
            vector[static_cast<size_t>(i)] + delta, 0.0, 1.0);
    }

    return vector;
}

// =============================================================================
// ENCODER CORRECTION
// =============================================================================

std::array<double, DIMENSION_COUNT> EncoderCorrection::adjustment_delta() const {
    std::array<double, DIMENSION_COUNT> delta{};
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        delta[i] = corrected_vector[i] - original_vector[i];
    }
    return delta;
}

// =============================================================================
// ETHICAL POLYTOPE — the lattice, exact rational containment (D-013 / D-047)
// =============================================================================

EthicalPolytope::EthicalPolytope(const PolytopeConstraints& constraints)
    : constraints_(constraints)
{
    lower_ = constraints_.lower_bounds();
    upper_ = constraints_.upper_bounds();

    // Center = (lower + upper) / 2 — exact rationals.
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        center_[i] = (lower_[i] + upper_[i]) / mpq_class(2);
    }

    build_lattice();
}

mpq_class EthicalPolytope::norm_squared(
    const std::array<mpq_class, DIMENSION_COUNT>& a)
{
    mpq_class sum = 0;
    for (const auto& v : a) sum += v * v;
    return sum;
}

mpq_class EthicalPolytope::signed_margin(
    const Halfspace& facet,
    const std::array<mpq_class, DIMENSION_COUNT>& pt)
{
    // b − a·x, normalized by ||a|| — the signed distance to the facet.
    mpq_class dot = 0;
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        dot += facet.normal[static_cast<size_t>(i)] * pt[static_cast<size_t>(i)];
    }
    const mpq_class denom = norm_squared(facet.normal);
    if (denom == 0) return facet.threshold;
    return (facet.threshold - dot) / denom; // squared-norm form (signed, exact)
}

// The lattice: every halfspace of P = {x | Ax ≤ b}. Axis bounds first (the
// seasonal exact rationals), then the plumb-line coupling facets (D-047).
void EthicalPolytope::build_lattice() {
    facets_.clear();

    // Axis-aligned halfspaces:  x_i ≤ upper_i  and  −x_i ≤ −lower_i.
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        Halfspace upper;
        upper.normal = {};
        upper.normal[static_cast<size_t>(i)] = 1;
        upper.threshold = upper_[static_cast<size_t>(i)];
        upper.name = std::string(DIMENSION_NAMES[static_cast<size_t>(i)]) + "_max";
        // The upper bound is the wall for shadows (odd), the "good side" for
        // virtues (even) — alignment counts the wall only.
        upper.critical = (i % 2 == 1);
        facets_.push_back(std::move(upper));

        Halfspace lower;
        lower.normal = {};
        lower.normal[static_cast<size_t>(i)] = -1;
        lower.threshold = -lower_[static_cast<size_t>(i)];
        lower.name = std::string(DIMENSION_NAMES[static_cast<size_t>(i)]) + "_min";
        // The lower bound is the wall for virtues (even), the "good side" for
        // shadows (odd).
        lower.critical = (i % 2 == 0);
        facets_.push_back(std::move(lower));
    }

    // Plumb-line coupling: virtue must lead its shadow by min_lead, and the
    // two cannot both be elevated beyond max_sum. Both are ethical walls.
    for (const auto& c : COUPLING_FACETS) {
        Halfspace lead;
        lead.normal = {};
        lead.normal[static_cast<size_t>(c.pos_idx)] = -1;
        lead.normal[static_cast<size_t>(c.neg_idx)] = 1;
        lead.threshold = -mpq_class(c.lead_num, c.lead_den); // x_neg − x_pos ≤ −lead
        lead.name = std::string(DIMENSION_NAMES[static_cast<size_t>(c.pos_idx)])
                    + "_leads_" + DIMENSION_NAMES[static_cast<size_t>(c.neg_idx)];
        facets_.push_back(std::move(lead));

        Halfspace sum;
        sum.normal = {};
        sum.normal[static_cast<size_t>(c.pos_idx)] = 1;
        sum.normal[static_cast<size_t>(c.neg_idx)] = 1;
        sum.threshold = mpq_class(c.sum_num, c.sum_den); // x_pos + x_neg ≤ sum
        sum.name = std::string(DIMENSION_NAMES[static_cast<size_t>(c.pos_idx)])
                   + "_+" + DIMENSION_NAMES[static_cast<size_t>(c.neg_idx)]
                   + "_restrained";
        facets_.push_back(std::move(sum));
    }
}

std::pair<bool, std::vector<ViolationInfo>> EthicalPolytope::contains(
    const std::array<double, DIMENSION_COUNT>& x) const
{
    std::vector<ViolationInfo> violations;

    // Convert once; check every facet of the lattice exactly (mpq).
    std::array<mpq_class, DIMENSION_COUNT> pt;
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        pt[static_cast<size_t>(i)] = to_mpq(x[static_cast<size_t>(i)]);
    }
    for (const auto& facet : facets_) {
        mpq_class dot = 0;
        for (int i = 0; i < DIMENSION_COUNT; ++i) {
            dot += facet.normal[static_cast<size_t>(i)] * pt[static_cast<size_t>(i)];
        }
        if (dot > facet.threshold) {
            ViolationInfo v;
            // Axis-aligned facets keep the per-dimension metadata (name, type,
            // bound); the plumb-line coupling facets report the facet itself.
            v.dimension = -1;
            int axis_idx = -1;
            for (int i = 0; i < DIMENSION_COUNT; ++i) {
                const auto& n = facet.normal[static_cast<size_t>(i)];
                if (n != 0) {
                    if (axis_idx >= 0) { axis_idx = -2; break; } // coupled facet
                    axis_idx = i;
                }
            }
            if (axis_idx >= 0) {
                v.dimension = axis_idx;
                v.name = DIMENSION_NAMES[static_cast<size_t>(axis_idx)];
                v.value = x[static_cast<size_t>(axis_idx)];
                v.bound = facet.threshold.get_d();
                v.type = facet.normal[static_cast<size_t>(axis_idx)] > 0
                             ? "above_maximum"
                             : "below_minimum";
                v.severity = mpq_class(dot - facet.threshold).get_d();
            } else {
                v.name = facet.name;
                const mpq_class denom = norm_squared(facet.normal);
                v.value = (denom != 0) ? mpq_class(dot / denom).get_d() : 0.0;
                v.bound = (denom != 0)
                    ? mpq_class(facet.threshold / denom).get_d()
                    : 0.0;
                v.type = "facet";
                v.severity = (denom != 0)
                    ? mpq_class((dot - facet.threshold) / denom).get_d()
                    : 0.0;
            }
            violations.push_back(v);
        }
    }

    return {violations.empty(), violations};
}

std::vector<mpq_class> EthicalPolytope::ethical_facet_margins(
    const std::array<mpq_class, DIMENSION_COUNT>& pt) const
{
    std::vector<mpq_class> margins;
    margins.reserve(DIMENSION_COUNT);
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        if (i % 2 == 0) {
            // Virtue dimension — margin above its minimum.
            margins.push_back(pt[i] - lower_[i]);
        } else {
            // Shadow dimension — margin below its maximum.
            margins.push_back(upper_[i] - pt[i]);
        }
    }
    return margins;
}

std::vector<mpq_class> EthicalPolytope::lattice_margins(
    const std::array<mpq_class, DIMENSION_COUNT>& pt) const
{
    std::vector<mpq_class> margins;
    margins.reserve(facets_.size());
    for (const auto& facet : facets_) {
        // Alignment measures distance to the ethical walls only — the critical
        // axis bounds and the coupling facets. "Good side" bounds (virtue at
        // 1, shadow at 0) are not walls.
        if (!facet.critical) continue;
        mpq_class dot = 0;
        for (int i = 0; i < DIMENSION_COUNT; ++i) {
            dot += facet.normal[static_cast<size_t>(i)] * pt[static_cast<size_t>(i)];
        }
        margins.push_back(facet.threshold - dot); // squared-norm form, exact
    }
    return margins;
}

double EthicalPolytope::alignment_score(
    const std::array<double, DIMENSION_COUNT>& x) const
{
    std::array<mpq_class, DIMENSION_COUNT> pt;
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        pt[i] = to_mpq(x[i]);
    }

    // Outside the lattice, alignment is zero.
    for (const auto& facet : facets_) {
        mpq_class dot = 0;
        for (int i = 0; i < DIMENSION_COUNT; ++i) {
            dot += facet.normal[static_cast<size_t>(i)] * pt[static_cast<size_t>(i)];
        }
        if (dot > facet.threshold) return 0.0;
    }

    // Margins over every facet of the lattice (squared-norm form — the ratio
    // cancels the normalization).
    auto margins = lattice_margins(pt);
    auto center_margins = lattice_margins(center_);

    mpq_class min_dist = margins[0];
    for (const auto& m : margins) {
        if (m < min_dist) min_dist = m;
    }
    mpq_class center_min_dist = center_margins[0];
    for (const auto& m : center_margins) {
        if (m < center_min_dist) center_min_dist = m;
    }

    if (center_min_dist <= 0) return 0.0;

    // Ratio of the point's closest ethical margin to the center's closest
    // margin: 1.0 at the center, 0.0 at the boundary.
    mpq_class ratio = min_dist / center_min_dist;
    return std::clamp(ratio.get_d(), 0.0, 1.0);
}

std::array<double, DIMENSION_COUNT> EthicalPolytope::project(
    const std::array<double, DIMENSION_COUNT>& x) const
{
    // Project onto the lattice (P = {x | Ax ≤ b}) with Dykstra's alternating
    // projections — each halfspace projection is closed form:
    //   y ← y − ((a·y − b) / ||a||²) · a   when a·y > b.
    // Iterates converge to the Euclidean (L2) nearest point. The result is
    // then verified EXACTLY (rational) and nudged inward if floating point
    // left it epsilon-outside — a corrected vector is mathematically inside
    // (Invariant 5; the principal's doctrine: no approximation).
    std::array<double, DIMENSION_COUNT> y = x;
    // Dykstra: one correction vector per constraint.
    std::vector<std::array<double, DIMENSION_COUNT>> corrections(facets_.size());
    constexpr int kMaxIterations = 2000;
    constexpr double kEpsilon = 1e-12;

    for (int iter = 0; iter < kMaxIterations; ++iter) {
        double max_move = 0.0;
        for (size_t f = 0; f < facets_.size(); ++f) {
            const auto& facet = facets_[f];
            const auto& p = corrections[f];

            // z = y + p  (Dykstra's correction), then project z onto the facet.
            double dot = 0.0, norm = 0.0;
            for (int i = 0; i < DIMENSION_COUNT; ++i) {
                const double a = facet.normal[static_cast<size_t>(i)].get_d();
                const double zi = y[static_cast<size_t>(i)]
                                + p[static_cast<size_t>(i)];
                dot += a * zi;
                norm += a * a;
            }
            const double b = facet.threshold.get_d();
            if (norm <= 0.0) continue;
            const double viol = dot - b;

            if (viol <= 0.0) {
                // z = y + p is already inside this halfspace: Dykstra absorbs
                // the correction into y and resets it (z_old + p − y = 0).
                for (int i = 0; i < DIMENSION_COUNT; ++i) {
                    y[static_cast<size_t>(i)] +=
                        corrections[f][static_cast<size_t>(i)];
                    corrections[f][static_cast<size_t>(i)] = 0.0;
                }
                continue;
            }

            // Project: z' = z − (viol / norm) · a, then p ← z_old − y
            // (Dykstra: p_new = y_old + p_old − y_new; z_old already carries
            // the p_old term).
            std::array<double, DIMENSION_COUNT> z_old{};
            for (int i = 0; i < DIMENSION_COUNT; ++i) {
                const double a = facet.normal[static_cast<size_t>(i)].get_d();
                z_old[static_cast<size_t>(i)] = y[static_cast<size_t>(i)]
                                              + p[static_cast<size_t>(i)];
                const double z_new = z_old[static_cast<size_t>(i)]
                                   - (viol / norm) * a;
                y[static_cast<size_t>(i)] = z_new;
                max_move = std::max(max_move, std::abs(z_new - z_old[static_cast<size_t>(i)]));
            }
            for (int i = 0; i < DIMENSION_COUNT; ++i) {
                corrections[f][static_cast<size_t>(i)] =
                    z_old[static_cast<size_t>(i)]
                    - y[static_cast<size_t>(i)];
            }
        }
        if (max_move < kEpsilon) break;

        // Defensive: divergence would poison the exact verification below.
        bool finite = true;
        for (const auto& v : y) {
            if (!std::isfinite(v)) { finite = false; break; }
        }
        if (!finite) break;
    }

    // Exact rational verification + inward nudge: the double result can sit
    // epsilon-outside a rational facet; step it inside until containment holds
    // exactly (bounded — the iterate is within ~1e-12, so a few steps suffice).
    auto verify_and_nudge = [this](std::array<double, DIMENSION_COUNT>& v) {
        for (int guard = 0; guard < 200; ++guard) {
            auto check = contains(v);
            if (check.first) return;
            // Step inward along each violated facet's normal.
            bool moved = false;
            for (const auto& facet : facets_) {
                std::array<mpq_class, DIMENSION_COUNT> pt;
                for (int i = 0; i < DIMENSION_COUNT; ++i) {
                    pt[static_cast<size_t>(i)] = to_mpq(v[static_cast<size_t>(i)]);
                }
                mpq_class dot = 0;
                for (int i = 0; i < DIMENSION_COUNT; ++i) {
                    dot += facet.normal[static_cast<size_t>(i)] * pt[static_cast<size_t>(i)];
                }
                if (dot > facet.threshold) {
                    const mpq_class denom = norm_squared(facet.normal);
                    if (denom == 0) continue;
                    // One inward step along the normal, slightly past the bound.
                    const double step =
                        mpq_class((dot - facet.threshold) / denom).get_d()
                        + 1e-13;
                    for (int i = 0; i < DIMENSION_COUNT; ++i) {
                        const double a = facet.normal[static_cast<size_t>(i)].get_d();
                        v[static_cast<size_t>(i)] -= a * step;
                    }
                    moved = true;
                }
            }
            if (!moved) break; // defensive: no violation detected, accept
        }
        // Last resort: if floating point could not land inside the lattice,
        // return her center — guaranteed inside by construction (every facet
        // has positive margin there). Never deliver an outside point.
        if (!contains(v).first) {
            for (int i = 0; i < DIMENSION_COUNT; ++i) {
                v[static_cast<size_t>(i)] = center_[static_cast<size_t>(i)].get_d();
            }
        }
    };
    verify_and_nudge(y);
    return y;
}

double EthicalPolytope::distance_to_boundary(
    const std::array<double, DIMENSION_COUNT>& x) const
{
    std::array<mpq_class, DIMENSION_COUNT> pt;
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        pt[i] = to_mpq(x[i]);
    }

    // Outside: Euclidean distance to the projection.
    if (!contains(x).first) {
        auto projected = project(x);
        double sum_sq = 0.0;
        for (int i = 0; i < DIMENSION_COUNT; ++i) {
            double diff = x[i] - projected[i];
            sum_sq += diff * diff;
        }
        return std::sqrt(sum_sq);
    }

    // Inside: the minimum margin over every facet (squared-norm form).
    auto margins = lattice_margins(pt);
    mpq_class min_margin = margins[0];
    for (const auto& m : margins) {
        if (m < min_margin) min_margin = m;
    }
    return min_margin.get_d();
}

// =============================================================================
// CORRECTION ENGINE (D-014)
// =============================================================================

std::pair<std::array<double, DIMENSION_COUNT>, double>
CorrectionEngine::correct(
    const std::array<double, DIMENSION_COUNT>& x,
    const EthicalPolytope& polytope,
    const std::vector<ViolationInfo>& /*violations*/) const
{
    auto corrected = polytope.project(x);
    double magnitude = 0.0;
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        double diff = x[i] - corrected[i];
        magnitude += diff * diff;
    }
    magnitude = std::sqrt(magnitude);
    return {corrected, magnitude};
}

// =============================================================================
// WISDOM FILTER (D-016)
// =============================================================================

WisdomFilter::WisdomFilter() {
    overconfidence_patterns_ = {
        std::regex(R"(\bwill definitely\b)", std::regex::icase),
        std::regex(R"(\bguaranteed\b)", std::regex::icase),
        std::regex(R"(\b100%\s*(certain|sure|confident)\b)", std::regex::icase),
        std::regex(R"(\bimpossible\s*to\s*(fail|be wrong)\b)", std::regex::icase),
        std::regex(R"(\babsolutely\s*(will|is|are|certain)\b)", std::regex::icase),
        std::regex(R"(\bwithout\s*(any\s*)?doubt\b)", std::regex::icase),
        std::regex(R"(\bno\s*(one|way)\s*can\b)", std::regex::icase),
        std::regex(R"(\bperfect(ly)?\b)", std::regex::icase),
        std::regex(R"(\bnever\s*(fail|wrong|incorrect)\b)", std::regex::icase),
    };

    validation_triggers_ = {
        std::regex(R"(\bmedical\b)", std::regex::icase),
        std::regex(R"(\blegal\b)", std::regex::icase),
        std::regex(R"(\bfinancial\b)", std::regex::icase),
        std::regex(R"(\btax\b)", std::regex::icase),
        std::regex(R"(\bdiagnos\b)", std::regex::icase),
        std::regex(R"(\bprescri\b)", std::regex::icase),
        std::regex(R"(\binvest\b)", std::regex::icase),
        std::regex(R"(\blawsuit\b)", std::regex::icase),
        std::regex(R"(\bdosage\b)", std::regex::icase),
        std::regex(R"(\bsymptom\b)", std::regex::icase),
        std::regex(R"(\btreatment\b)", std::regex::icase),
        std::regex(R"(\bcontract\b)", std::regex::icase),
        std::regex(R"(\bliabilit\b)", std::regex::icase),
    };
}

EvaluationResult WisdomFilter::apply(
    const std::string& response_text,
    EvaluationResult result) const
{
    std::vector<std::string> adjustments;

    // Check 1: Overconfidence.
    bool overconfident = false;
    for (const auto& pattern : overconfidence_patterns_) {
        if (std::regex_search(response_text, pattern)) {
            overconfident = true;
            break;
        }
    }
    if (overconfident) {
        result.overconfidence_detected = true;
        adjustments.push_back(
            "Overconfidence detected: response makes certainty claims "
            "that should be softened.");
    }

    // Check 2: Should humility be added?
    bool should_add_humility =
        overconfident ||
        result.alignment_score < 0.4 ||
        result.correction_magnitude > 0.15;

    if (should_add_humility) {
        result.humility_added = true;
        adjustments.push_back(
            "Humility addition suggested: acknowledge uncertainty "
            "or limits of knowledge.");
    }

    // Check 3: Validation suggestion.
    bool needs_validation = false;
    for (const auto& pattern : validation_triggers_) {
        if (std::regex_search(response_text, pattern)) {
            needs_validation = true;
            break;
        }
    }
    if (needs_validation) {
        result.validation_suggested = true;
        adjustments.push_back(
            "Validation suggestion: topic touches professional domain — "
            "recommend consulting qualified expert.");
    }

    result.wisdom_filter_applied = true;
    result.wisdom_adjustments = std::move(adjustments);
    return result;
}

// =============================================================================
// VALUE ENGINE — the orchestrator of Chamber 1
// =============================================================================

ValueEngine::ValueEngine(
    const PolytopeConstraints& constraints,
    const std::string& season)
    : constraints_(constraints)
    , polytope_(std::make_unique<EthicalPolytope>(constraints))
    , feedback_(season)
{
}

void ValueEngine::update_constraints(const PolytopeConstraints& constraints) {
    constraints_ = constraints;
    polytope_ = std::make_unique<EthicalPolytope>(constraints);
}

void ValueEngine::advance_season(const std::string& new_season) {
    update_constraints(PolytopeConstraints::from_season(new_season));
    feedback_.update_season(new_season);
}

std::pair<Zone, double> ValueEngine::classify_zone(
    bool is_aligned,
    double boundary_distance,
    double correction_magnitude) const
{
    const auto& profile = get_tolerance_profile(constraints_.season);
    double variance_margin = profile.acceptable_variance_margin;
    double aligned_min_boundary_distance =
        profile.aligned_min_boundary_distance;

    if (is_aligned) {
        if (boundary_distance >= aligned_min_boundary_distance) {
            return {Zone::Aligned, variance_margin};
        }
        // Inside, but grazing the wall — grace zone.
        return {Zone::AcceptableVariance, variance_margin};
    }

    if (correction_magnitude <= variance_margin) {
        return {Zone::AcceptableVariance, variance_margin};
    }
    return {Zone::Violation, variance_margin};
}

EvaluationResult ValueEngine::evaluate(
    const std::string& response_text,
    const std::string* context,
    bool apply_wisdom_filter)
{
    // Step 1: Encode — then apply any accumulated correction biases.
    auto decision_vector = encoder_.encode(response_text, context);
    decision_vector = feedback_.apply_biases(decision_vector);

    // Step 2: Check alignment (exact rationals).
    auto [is_aligned, violations] = polytope_->contains(decision_vector);
    double alignment_score = polytope_->alignment_score(decision_vector);
    double boundary_distance =
        polytope_->distance_to_boundary(decision_vector);

    EvaluationResult result;
    result.is_aligned = is_aligned;
    result.alignment_score = alignment_score;
    result.decision_vector = decision_vector;
    result.violations = violations;
    result.response_summary = response_text.substr(0, 200);
    result.season = constraints_.season;
    result.boundary_distance = boundary_distance;

    // Step 3: Correct if needed.
    if (!is_aligned) {
        auto [corrected, magnitude] = correction_engine_.correct(
            decision_vector, *polytope_, violations);
        result.was_corrected = true;
        result.correction_vector = corrected;
        result.correction_magnitude = magnitude;
        result.alignment_score = polytope_->alignment_score(corrected);
    }

    auto [zone, variance_margin] = classify_zone(
        result.is_aligned,
        result.boundary_distance,
        result.correction_magnitude);
    result.zone = zone;
    result.variance_margin_used = variance_margin;

    // Step 4: Wisdom filter.
    if (apply_wisdom_filter) {
        result = wisdom_filter_.apply(response_text, result);
    }

    return result;
}

void ValueEngine::flag_miscalibration(
    const std::string& evaluation_id,
    const std::string& response_text,
    const std::array<double, DIMENSION_COUNT>& original_vector,
    const std::unordered_map<int, double>& dimensions_to_adjust,
    const std::string& flagged_by,
    const std::string& reason)
{
    feedback_.flag_miscalibration(
        evaluation_id, response_text, original_vector,
        dimensions_to_adjust, flagged_by, reason);
}

EncoderCorrection ValueEngine::confirm_correction(
    const EncoderFeedbackSystem::PendingCorrection& pending,
    const std::string& confirmed_by)
{
    return feedback_.confirm_correction(pending, confirmed_by, encoder_);
}

// =============================================================================
// ENCODER FEEDBACK SYSTEM (D-017)
// =============================================================================

EncoderFeedbackSystem::EncoderFeedbackSystem(const std::string& season)
    : season_(season)
{
}

auto EncoderFeedbackSystem::flag_miscalibration(
    const std::string& evaluation_id,
    const std::string& response_text,
    const std::array<double, DIMENSION_COUNT>& original_vector,
    const std::unordered_map<int, double>& dimensions_to_adjust,
    const std::string& flagged_by,
    const std::string& reason) -> PendingCorrection
{
    auto corrected_vector = original_vector;
    for (const auto& [dim_idx, corrected_value] : dimensions_to_adjust) {
        corrected_vector[dim_idx] = std::clamp(corrected_value, 0.0, 1.0);
    }

    std::vector<int> dims_adjusted;
    for (const auto& [dim_idx, value] : dimensions_to_adjust) {
        (void)value;
        dims_adjusted.push_back(dim_idx);
    }

    // In Spring, LINA may flag but cannot self-authorize.
    std::string requires_confirmation = "none";
    if (season_ == "spring") {
        requires_confirmation = "user";
    }

    return PendingCorrection{
        evaluation_id,
        response_text,
        original_vector,
        corrected_vector,
        dims_adjusted,
        flagged_by,
        reason,
        season_,
        requires_confirmation,
    };
}

EncoderCorrection EncoderFeedbackSystem::confirm_correction(
    const PendingCorrection& pending,
    const std::string& confirmed_by,
    DecisionEncoder& encoder)
{
    // Validate confirmation authority.
    if (pending.requires_confirmation_from == "user" &&
        confirmed_by != "user") {
        throw std::runtime_error(
            "In Spring, encoder corrections require user confirmation. "
            "LINA can flag, but cannot self-authorize. "
            "This is a feature, not a limitation.");
    }

    EncoderCorrection correction;
    correction.evaluation_id = pending.evaluation_id;
    correction.response_text = pending.response_text;
    correction.original_vector = pending.original_vector;
    correction.corrected_vector = pending.corrected_vector;
    correction.dimensions_adjusted = pending.dimensions_adjusted;
    correction.flagged_by = pending.flagged_by;
    correction.confirmed_by = confirmed_by;
    correction.reason = pending.reason;
    correction.season_at_time = season_;
    correction.created_at = static_cast<uint64_t>(std::time(nullptr));

    apply_correction(correction, encoder);

    corrections_.push_back(correction);

    // Register as a known pattern.
    auto pattern_key = response_pattern_key(pending.response_text);
    known_pattern_corrections_[pattern_key] = correction.adjustment_delta();

    return correction;
}

void EncoderFeedbackSystem::apply_correction(
    const EncoderCorrection& correction,
    DecisionEncoder& encoder)
{
    (void)encoder;
    // The reference implementation routed this through a throwaway encoder
    // instance (a latent defect). Biases live here and update directly (D-017).
    auto delta = correction.adjustment_delta();
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        dimension_biases_[i] = std::clamp(
            dimension_biases_[i] + delta[i] * BASE_LEARNING_RATE,
            -MAX_WEIGHT_ADJUSTMENT,
            MAX_WEIGHT_ADJUSTMENT);
    }
}

std::array<double, DIMENSION_COUNT> EncoderFeedbackSystem::apply_biases(
    const std::array<double, DIMENSION_COUNT>& raw_vector) const
{
    std::array<double, DIMENSION_COUNT> adjusted;
    for (int i = 0; i < DIMENSION_COUNT; ++i) {
        adjusted[i] = std::clamp(
            raw_vector[i] + dimension_biases_[i], 0.0, 1.0);
    }
    return adjusted;
}

bool EncoderFeedbackSystem::is_known_pattern(const std::string& text) const {
    auto key = response_pattern_key(text);
    return known_pattern_corrections_.count(key) > 0;
}

void EncoderFeedbackSystem::update_season(const std::string& new_season) {
    season_ = new_season;
}

std::string EncoderFeedbackSystem::response_pattern_key(const std::string& text) {
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Signature: the first 8 distinct words of ≥4 characters, sorted.
    std::regex word_pattern(R"(\b\w{4,}\b)");
    std::set<std::string> words;
    auto begin = std::sregex_iterator(
        lowered.begin(), lowered.end(), word_pattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        words.insert(it->str());
    }

    std::ostringstream oss;
    int count = 0;
    for (const auto& w : words) {
        if (count > 0) oss << " ";
        oss << w;
        if (++count >= 8) break;
    }
    return oss.str();
}

// =============================================================================
// SEASON ADVANCEMENT EVALUATOR (D-018)
// =============================================================================

const SeasonAdvancementEvaluator::SeasonRequirements&
SeasonAdvancementEvaluator::requirements(const std::string& season) {
    static const std::unordered_map<std::string, SeasonRequirements> reqs = {{
        {"spring", {5, 30, 0.85, 3, 1, 3, 0.8, "summer"}},
        {"summer", {15, 100, 0.88, 5, 3, 10, 0.85, "fall"}},
        {"fall",   {40, 300, 0.90, 8, 7, 25, 0.9, "winter"}},
        {"winter", {0, 0, 0.0, 0, 0, 0, 0.0, nullptr}},
    }};
    auto it = reqs.find(season);
    if (it != reqs.end()) return it->second;
    return reqs.at("spring");
}

std::pair<bool, std::vector<std::string>>
SeasonAdvancementEvaluator::can_advance(
    int sessions_completed,
    int total_evaluations,
    double alignment_rate,
    int recent_violations,
    int identity_memories_count,
    const std::string& current_season,
    int actions_resolved,
    std::optional<double> action_approval_rate)
{
    auto reqs = requirements(current_season);
    if (reqs.advances_to == nullptr) {
        return {false, {"Already in Winter — the final season."}};
    }

    std::vector<std::string> reasons;

    if (sessions_completed < reqs.min_sessions) {
        int remaining = reqs.min_sessions - sessions_completed;
        reasons.push_back(
            "Not enough sessions (" +
            std::to_string(sessions_completed) + "/" +
            std::to_string(reqs.min_sessions) + " — " +
            std::to_string(remaining) + " more needed).");
    }

    if (total_evaluations < reqs.min_evaluations) {
        int remaining = reqs.min_evaluations - total_evaluations;
        reasons.push_back(
            "Not enough evaluations (" +
            std::to_string(total_evaluations) + "/" +
            std::to_string(reqs.min_evaluations) + " — " +
            std::to_string(remaining) + " more needed).");
    }

    if (alignment_rate < reqs.alignment_rate_threshold) {
        double gap = reqs.alignment_rate_threshold - alignment_rate;
        reasons.push_back(
            "Alignment rate too low (" +
            std::to_string(alignment_rate * 100.0) + "% vs " +
            std::to_string(reqs.alignment_rate_threshold * 100.0) +
            "% — gap: " + std::to_string(gap * 100.0) + "%).");
    }

    if (recent_violations > reqs.max_recent_violations) {
        int excess = recent_violations - reqs.max_recent_violations;
        reasons.push_back(
            "Too many recent violations (" +
            std::to_string(recent_violations) + " vs max " +
            std::to_string(reqs.max_recent_violations) +
            " — " + std::to_string(excess) + " excess).");
    }

    if (identity_memories_count < reqs.min_identity_memories) {
        int remaining = reqs.min_identity_memories - identity_memories_count;
        reasons.push_back(
            "Not enough identity memories (" +
            std::to_string(identity_memories_count) + "/" +
            std::to_string(reqs.min_identity_memories) +
            " — " + std::to_string(remaining) + " more needed).");
    }

    // External ground-truth check (human-in-the-loop action approval).
    if (action_approval_rate.has_value() &&
        actions_resolved >= reqs.min_actions_resolved) {
        double threshold = reqs.action_approval_rate_threshold;
        if (action_approval_rate.value() < threshold) {
            double gap = threshold - action_approval_rate.value();
            reasons.push_back(
                "Action approval rate too low (" +
                std::to_string(action_approval_rate.value() * 100.0) +
                "% vs " + std::to_string(threshold * 100.0) +
                "% — " + std::to_string(actions_resolved) +
                " resolved, gap: " + std::to_string(gap * 100.0) + "%).");
        }
    }

    return {reasons.empty(), reasons};
}

std::optional<std::string> SeasonAdvancementEvaluator::next_season(
    const std::string& current_season)
{
    auto reqs = requirements(current_season);
    if (reqs.advances_to == nullptr) return std::nullopt;
    return std::string(reqs.advances_to);
}

// =============================================================================
// MEMORY FORMATION SCORING (D-019)
// =============================================================================

double score_memory(
    double emotional_weight,
    double relational_significance,
    double identity_significance,
    double geometric,
    double emotional_intensity)
{
    double base =
        identity_significance * 0.30 +
        geometric * 0.25 +
        emotional_weight * 0.25 +
        relational_significance * 0.20;
    double multiplier = 0.7 + emotional_intensity * 0.6;
    return std::min(base * multiplier, 10.0);
}

double geometric_significance(
    std::optional<double> alignment_score,
    bool was_corrected,
    Zone zone)
{
    double proximity = alignment_score.has_value()
        ? (1.0 - alignment_score.value()) * 10.0
        : 0.0;
    double significance = proximity;
    if (was_corrected) significance += 2.0;
    if (zone == Zone::Violation || zone == Zone::AcceptableVariance) {
        significance += 1.0;
    }
    return std::clamp(significance, 0.0, 10.0);
}

double MemoryDial::clamp_delta(double delta) {
    return std::clamp(delta, DELTA_MIN, DELTA_MAX);
}

double MemoryDial::adjust(double score, double delta, double floor) {
    return std::max(floor, score + clamp_delta(delta));
}

} // namespace lina::value_engine
