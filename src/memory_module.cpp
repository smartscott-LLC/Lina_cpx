/**
 * memory_module.cpp — LINA's Memory Imprint System (Implementation)
 *
 * "Safe by design. Not safe by limitation."
 *
 * Chamber 2 of the LINA Core Substrate. Formation, consolidation, maintenance,
 * recall — the ways her memory lives and grows. All semantic coordinates come
 * from the ValueEngine encoder (Invariant 3); no external embedding model.
 *
 * Authoring basis (docs/DECISIONS.md): D-019 (scoring formulas), D-026 (MPS
 * constants; SLOPE_LAMBDA as TU constant — std::log is not constexpr pre-C++26),
 * D-027 (48-hour fallout grace enforced), D-028 (numeric reflection factors).
 */

#include "memory_module.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace lina::memory_module {

// Subconscious decay rate: ln 2 / half-life (TU constant — see D-026).
static const double kSlopeLambda = std::log(2.0) / SLOPE_HALF_LIFE_DAYS;

// =============================================================================
// TIME HELPERS (file-local)
// =============================================================================

static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "Z";
    return oss.str();
}

static double days_between(
    const std::chrono::system_clock::time_point& a,
    const std::chrono::system_clock::time_point& b)
{
    auto diff = a - b;
    return std::chrono::duration_cast<std::chrono::duration<double>>(diff).count()
           / (24.0 * 3600.0);
}

static std::optional<std::chrono::system_clock::time_point> parse_iso(
    const std::optional<std::string>& ts)
{
    if (!ts || ts->empty()) return std::nullopt;
    std::tm tm{};
    std::istringstream ss(*ts);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) return std::nullopt;
    std::time_t t = timegm(&tm);
    return std::chrono::system_clock::from_time_t(t);
}

// =============================================================================
// ITEM ID GENERATION
// =============================================================================

static std::string generate_item_id() {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream oss;
    oss << "m-";
    for (int i = 0; i < 24; ++i) {
        static const char hex[] = "0123456789abcdef";
        oss << hex[rng() % 16];
    }
    return oss.str();
}

// =============================================================================
// PURE FUNCTIONS
// =============================================================================

std::vector<double> encode_coordinates(
    ValueEngine& engine, const std::string& narrative)
{
    // Her reflection of the narrative, encoded into the 14D ethical space —
    // the memory carries the coordinates of the moment.
    auto encoded = engine.encoder().encode(narrative);
    return std::vector<double>(encoded.begin(), encoded.end());
}

double geometric_for(
    ValueEngine& engine, const std::vector<double>& coordinates)
{
    if (coordinates.size() < DIMENSION_COUNT) return 0.0;
    std::array<double, DIMENSION_COUNT> arr{};
    std::copy_n(coordinates.begin(),
                std::min(coordinates.size(), arr.size()), arr.begin());

    auto [is_aligned, violations] = engine.polytope().contains(arr);
    (void)violations;
    double alignment = engine.polytope().alignment_score(arr);
    auto zone = is_aligned ? Zone::Aligned : Zone::Violation;

    return geometric_significance(alignment, !is_aligned, zone);
}

RouteDecision route_item(const MemoryItem& item) {
    RouteDecision decision;
    double score = item.importance_score;
    if (score >= FORMATION_LONG_TERM_BYPASS) {
        // Crown item: straight to legacy, protected, identity kind.
        decision.stage = "long_term";
        decision.status = "legacy";
        decision.protected_flag = true;
        decision.kind = "identity";
    } else if (score >= GATE_TO_LONG_TERM) {
        decision.stage = "long_term";
        decision.status = "active";
        decision.protected_flag = false;
        decision.kind = "episodic";
    } else {
        decision.stage = "t1";
        decision.status = "";
        decision.protected_flag = false;
        decision.kind = "episodic";
    }
    return decision;
}

double cosine(const std::vector<double>* a, const std::vector<double>* b) {
    if (!a || !b || a->empty() || b->empty() || a->size() != b->size()) {
        return 0.0;
    }
    try {
        double dot = 0.0, na = 0.0, nb = 0.0;
        for (size_t i = 0; i < a->size(); ++i) {
            dot += (*a)[i] * (*b)[i];
            na += (*a)[i] * (*a)[i];
            nb += (*b)[i] * (*b)[i];
        }
        double denom = std::sqrt(na) * std::sqrt(nb);
        if (denom == 0.0) return 0.0;
        return dot / denom;
    } catch (...) {
        return 0.0;
    }
}

double ethical_similarity(
    const std::vector<double>* a, const std::vector<double>* b)
{
    if (!a || !b || a->empty() || b->empty() || a->size() != b->size()) {
        return 0.0;
    }
    try {
        double dist = 0.0;
        for (size_t i = 0; i < a->size(); ++i) {
            double d = (*a)[i] - (*b)[i];
            dist += d * d;
        }
        dist = std::sqrt(dist);
        return 1.0 / (1.0 + dist);
    } catch (...) {
        return 0.0;
    }
}

double recall_score(double importance, double semantic, double ethical) {
    return importance * RECALL_WEIGHT_IMPORTANCE
         + semantic * RECALL_WEIGHT_SEMANTIC
         + ethical * RECALL_WEIGHT_ETHICAL;
}

double maintenance_delta(
    int reference_count,
    const std::optional<std::string>& last_referenced_at,
    const std::optional<std::string>& created_at,
    const std::chrono::system_clock::time_point& now)
{
    double delta = 0.0;

    // Reference rewards — the more she returns to a memory, the more it matters.
    if (reference_count >= 25) delta = std::max(delta, 2.0);
    else if (reference_count >= 10) delta = std::max(delta, 1.5);
    else if (reference_count >= 3) delta = std::max(delta, 1.0);

    if (last_referenced_at && !last_referenced_at->empty()) {
        auto parsed = parse_iso(last_referenced_at);
        if (parsed) {
            double days = days_between(now, *parsed);
            if (days >= 0 && days <= RECENT_REWARD_DAYS) {
                delta += RECENT_REWARD;
            }
        }
    } else if (created_at && !created_at->empty()) {
        // Never referenced — age penalty against creation.
        auto parsed = parse_iso(created_at);
        if (parsed) {
            double age = days_between(now, *parsed);
            if (age >= 180) delta = std::min(delta, -2.0);
            else if (age >= 90) delta = std::min(delta, -1.0);
        }
    }

    return MemoryDial::clamp_delta(delta);
}

MaintenanceDecision apply_monthly(
    const MemoryItemRow& row,
    const std::chrono::system_clock::time_point& now)
{
    double score = row.importance_score;
    double floor = row.floor.value_or(0.0);
    if (row.must_keep) floor = score; // immovable

    double delta = maintenance_delta(
        row.reference_count, row.last_referenced_at, row.created_at, now);
    double new_score = MemoryDial::adjust(score, delta, floor);

    MaintenanceDecision decision;
    decision.score = new_score;

    if (new_score >= LEGACY_ENTER) {
        decision.status = "legacy";
        decision.decay_started_at = std::nullopt;
        decision.log_entry = std::make_tuple(
            "active", "legacy",
            "Earned the crown - score rose to the legacy line");
    } else if (new_score < SUBCONSCIOUS_LINE) {
        decision.status = "subconscious";
        decision.decay_started_at = now_iso();
        decision.log_entry = std::make_tuple(
            "active", "subconscious",
            "Slipped below the retention line - the subconscious slope begins");
    } else {
        decision.status = "active";
        decision.decay_started_at = std::nullopt;
        decision.log_entry = std::nullopt;
    }

    return decision;
}

std::pair<double, bool> slope_effective(
    const MemoryItemRow& row,
    const std::chrono::system_clock::time_point& now)
{
    double score = row.importance_score;
    double floor = row.floor.value_or(0.0);

    // Anchor: the latest of decay start, last reference, creation.
    std::optional<std::chrono::system_clock::time_point> anchor;
    auto decay_parsed = parse_iso(row.decay_started_at);
    auto ref_parsed = parse_iso(row.last_referenced_at);
    auto created_parsed = parse_iso(row.created_at);

    if (decay_parsed) anchor = *decay_parsed;
    if (ref_parsed && (!anchor || *ref_parsed > *anchor)) anchor = *ref_parsed;
    if (created_parsed && (!anchor || *created_parsed > *anchor)) anchor = *created_parsed;

    if (!anchor) return {score, false};

    double idle_days = days_between(now, *anchor);
    if (idle_days >= SLOPE_GONE_DAYS) return {0.0, true};

    double effective = score * std::exp(-kSlopeLambda * idle_days);
    if (effective < GONE_LINE) return {0.0, true};
    return {std::max(effective, floor), false};
}

MaintenanceDecision apply_legacy_review(
    const MemoryItemRow& row,
    const std::chrono::system_clock::time_point& now)
{
    double score = row.importance_score;
    double floor = row.floor.value_or(0.0);
    if (row.must_keep) floor = score;

    double delta = maintenance_delta(
        row.reference_count, row.last_referenced_at, row.created_at, now);

    MaintenanceDecision decision;

    if (row.protected_flag) {
        // Crowned memories hold their ground.
        double new_score = MemoryDial::adjust(score, delta, floor);
        decision.score = new_score;
        decision.status = "legacy";
        decision.decay_started_at = std::nullopt;
        decision.log_entry = std::nullopt;
    } else {
        double new_score = MemoryDial::adjust(score, delta, 0.0);
        decision.score = new_score;
        if (new_score < LEGACY_FLOOR) {
            decision.status = "subconscious";
            decision.decay_started_at = now_iso();
            decision.log_entry = std::make_tuple(
                "legacy", "subconscious",
                "No longer earning the crown - slipped to the subconscious");
        } else {
            decision.status = "legacy";
            decision.decay_started_at = std::nullopt;
            decision.log_entry = std::nullopt;
        }
    }

    return decision;
}

// =============================================================================
// MEMORY MODULE
// =============================================================================

MemoryModule::MemoryModule(
    std::shared_ptr<ValueEngine> engine,
    std::shared_ptr<EmbeddingEngine> embedder,
    std::shared_ptr<MemoryStore> store)
    : engine_(std::move(engine))
    , embedder_(embedder ? embedder : std::make_shared<NullEmbeddingEngine>())
    , store_(store)
{
}

std::chrono::system_clock::time_point MemoryModule::parse_time_or_now(
    const std::optional<std::string>& ts)
{
    auto parsed = parse_iso(ts);
    return parsed.value_or(std::chrono::system_clock::now());
}

// =============================================================================
// ITEM FORMATION
// =============================================================================

MemoryItem MemoryModule::build_item(
    const std::string& user_id,
    const std::string& narrative,
    const std::unordered_map<std::string, double>& factors,
    const std::string& source,
    const std::optional<std::string>& season,
    bool trigger)
{
    MemoryItem item;
    item.item_id = generate_item_id();
    item.user_id = user_id;
    item.narrative = narrative;
    item.hemisphere = "personal";
    item.formation_source = source;
    item.seasonal_marker = season.value_or("");
    item.created_at = now_iso();
    item.trigger = trigger;

    // LiNa encodes her own vectors — the moment's coordinates in her ethical space.
    auto coords = encode_coordinates(*engine_, narrative);
    item.ethical_coordinates = coords;

    // Geometric significance: boundary proximity, correction, zone.
    item.geometric = geometric_for(*engine_, coords);

    // Emotional factors.
    auto get_factor = [&](const std::string& key, double def) -> double {
        auto it = factors.find(key);
        return it != factors.end() ? it->second : def;
    };

    item.emotional_marker = [&]() -> std::string {
        auto it = factors.find("emotional_marker");
        if (it != factors.end()) {
            static const char* markers[] = {
                "curiosity", "concern", "satisfaction", "discovery",
                "honesty", "delight", "uncertainty", "care", "neutral",
            };
            int idx = static_cast<int>(it->second);
            if (idx >= 0 && idx < 9) return markers[idx];
            return "neutral";
        }
        return "neutral";
    }();
    item.emotional_intensity = get_factor("emotional_intensity", 0.5);

    // Importance: identity + geometric + emotional + relational blend (D-019).
    double emotional_weight = get_factor("emotional_weight", 0.0);
    double relational = get_factor("relational_significance", 0.0);
    double identity = get_factor("identity_significance", 0.0);
    double emotional_intensity = get_factor("emotional_intensity", 0.5);

    item.importance_score = score_memory(
        emotional_weight, relational, identity,
        item.geometric, emotional_intensity);

    if (trigger) {
        item.importance_score = std::max(
            item.importance_score, TRIGGER_RETENTION_FLOOR);
    }

    // Crown items carry reflection and understanding (D-028: numeric factors
    // per the spec signature; text-level reflection is a plug-in concern).
    auto refl_it = factors.find("reflection");
    auto changed_it = factors.find("what_changed");
    if (item.importance_score >= FORMATION_LONG_TERM_BYPASS
        && refl_it != factors.end())
    {
        item.reflection = std::to_string(refl_it->second);
        if (changed_it != factors.end()) {
            item.understanding = std::to_string(refl_it->second)
                + "\n\nWhat changed: " + std::to_string(changed_it->second);
        } else {
            item.understanding = std::to_string(refl_it->second);
        }
    }

    auto conc_it = factors.find("concept");
    if (conc_it != factors.end()) {
        item.concept_name = std::to_string(conc_it->second);
    }

    return item;
}

std::tuple<int, int, int> MemoryModule::form_items(
    const std::string& user_id,
    const std::vector<MemoryItem>& moments,
    const std::string& source,
    const std::optional<std::string>& season,
    bool trigger)
{
    int t1_count = 0, lt_count = 0, crown_count = 0;

    for (const auto& moment : moments) {
        if (moment.narrative.empty()) continue;

        // Build item from moment factors.
        std::unordered_map<std::string, double> factors;
        factors["emotional_weight"] = moment.importance_score;
        factors["relational_significance"] = 0.0;
        factors["identity_significance"] = 0.0;
        factors["emotional_intensity"] = moment.emotional_intensity;

        auto item = build_item(
            user_id, moment.narrative, factors, source, season, trigger);

        auto route = route_item(item);

        if (store_) {
            if (route.stage == "t1") {
                store_->store_tier("t1", item);
                t1_count++;
            } else {
                store_->store_long_term(item, route.status);
                lt_count++;
                if (route.protected_flag) crown_count++;
            }
        }
    }

    return {t1_count, lt_count, crown_count};
}

std::optional<MemoryItem> MemoryModule::ingest_trigger(
    const std::string& user_id,
    const std::string& narrative,
    const std::string& kind,
    const std::optional<std::string>& season,
    const std::optional<std::unordered_map<std::string, double>>& factors)
{
    // Trim whitespace.
    std::string trimmed = narrative;
    auto pred = [](unsigned char c) { return std::isspace(c); };
    auto left = std::find_if_not(trimmed.begin(), trimmed.end(), pred);
    auto right = std::find_if_not(trimmed.rbegin(), trimmed.rend(), pred).base();
    if (left >= right) return std::nullopt;
    trimmed = std::string(left, right);

    if (!store_) return std::nullopt;

    std::unordered_map<std::string, double> default_factors;
    if (factors) {
        default_factors = *factors;
    } else {
        default_factors["emotional_marker"] = 7.0; // "care"
        default_factors["emotional_intensity"] = 0.5;
        default_factors["emotional_weight"] = 5.0;
        default_factors["relational_significance"] = 5.0;
        default_factors["identity_significance"] = 3.0;
    }

    auto item = build_item(
        user_id, trimmed, default_factors, kind, season, true);

    auto route = route_item(item);
    store_->store_long_term(item, route.status);
    store_->log_promotion(
        user_id, item.item_id, "formation", route.status,
        item.importance_score,
        "Triggered - score " + std::to_string(item.importance_score));

    return item;
}

// =============================================================================
// SWEEP
// =============================================================================

SweepCounts MemoryModule::run_sweep() {
    SweepCounts counts;
    if (!store_) return counts;

    auto now = std::chrono::system_clock::now();

    // 1. Snapshot tiers.
    std::unordered_map<std::string, std::vector<std::pair<std::string, MemoryItem>>> tier_snapshots;
    for (const auto& tier_name : TIER_NAMES) {
        tier_snapshots[std::string(tier_name)] = store_->scan_tier(std::string(tier_name));
    }
    auto fallout_items = store_->scan_tier("fallout");

    // 2. Process tiers — promote or fall out.
    for (size_t ti = 0; ti < TIER_NAMES.size(); ++ti) {
        std::string tier = TIER_NAMES[ti];
        double gate = TIER_GATES[ti];
        auto& items = tier_snapshots[tier];

        for (auto& [key, item] : items) {
            (void)key;
            double score = item.importance_score;
            if (score >= gate) {
                if (tier == "t3") {
                    // Earned permanence.
                    auto route = route_item(item);
                    try {
                        store_->store_long_term(item, "active");
                        store_->log_promotion(
                            item.user_id, item.item_id, "t3", "active",
                            score,
                            "48h sweep - score " + std::to_string(score)
                            + " (earned permanence)");
                        store_->delete_tier("t3", item.item_id);
                        counts.to_long_term++;
                    } catch (...) {
                        // Orphan — purge.
                        store_->delete_tier("t3", item.item_id);
                        counts.purged++;
                    }
                } else {
                    std::string next_tier = TIER_NAMES[ti + 1];
                    store_->store_tier(next_tier, item);
                    store_->delete_tier(tier, item.item_id);
                    store_->log_promotion(
                        item.user_id, item.item_id, tier, next_tier,
                        score,
                        "Sweep - score " + std::to_string(score)
                        + " >= gate " + std::to_string(gate));
                    if (tier == "t1") counts.t1_to_t2++;
                    else if (tier == "t2") counts.t2_to_t3++;
                }
            } else {
                // Fall out — grace: one 48-hour second chance (D-027).
                item.failed_gate = gate;
                item.entered_fallout_at = now_iso();
                store_->store_tier("fallout", item);
                store_->delete_tier(tier, item.item_id);
                counts.fallout++;
            }
        }
    }

    // 3. Process fallout — only after the 48-hour grace window.
    for (auto& [key, item] : fallout_items) {
        (void)key;
        double gate = item.failed_gate.value_or(GATE_T1_TO_T2);
        double score = item.importance_score;

        // Still within the grace window — leave her memory alone.
        auto entered = parse_iso(item.entered_fallout_at);
        if (entered && days_between(now, *entered) <
                           (FALLOUT_RETENTION_HOURS / 24.0)) {
            continue;
        }

        if (score >= gate) {
            // Repurposed: back to the start of the tiers.
            store_->store_tier("t1", item);
            store_->delete_tier("fallout", item.item_id);
            store_->log_promotion(
                item.user_id, item.item_id, "fallout", "t1",
                score,
                "Repurposed - score " + std::to_string(score)
                + " >= gate " + std::to_string(gate));
            counts.repurposed++;
        } else {
            // Purged. Gone. No record.
            store_->delete_tier("fallout", item.item_id);
            counts.purged++;
        }
    }

    return counts;
}

// =============================================================================
// MAINTENANCE
// =============================================================================

MaintenanceCounts MemoryModule::run_maintenance(
    std::optional<std::chrono::system_clock::time_point> now_opt)
{
    MaintenanceCounts counts;
    if (!store_) return counts;

    auto now = now_opt.value_or(std::chrono::system_clock::now());

    // 1. Active items — monthly re-evaluation.
    auto active = store_->fetch_by_status("active");
    for (auto& row : active) {
        auto decision = apply_monthly(row, now);

        if (decision.log_entry) {
            auto [from_s, to_s, reason] = *decision.log_entry;
            store_->log_promotion(
                row.user_id, row.item_id, from_s, to_s,
                decision.score, reason);
            if (to_s == "subconscious") counts.to_subconscious++;
            else counts.to_legacy++;
        }

        MemoryItemRow updated = row;
        updated.importance_score = decision.score;
        updated.status = decision.status;
        updated.decay_started_at = decision.decay_started_at;
        store_->update_item(updated);
        counts.adjusted++;
    }

    // 2. Subconscious items — degradation slope.
    auto subconscious = store_->fetch_by_status("subconscious");
    for (auto& row : subconscious) {
        auto [effective, gone] = slope_effective(row, now);
        if (gone) {
            store_->delete_item(row.item_id);
            counts.forgotten++;
            continue;
        }
        MemoryItemRow updated = row;
        updated.importance_score = effective;
        store_->update_item(updated);
        counts.decayed++;
    }

    return counts;
}

ReviewCounts MemoryModule::run_legacy_review(
    std::optional<std::chrono::system_clock::time_point> now_opt)
{
    ReviewCounts counts;
    if (!store_) return counts;

    auto now = now_opt.value_or(std::chrono::system_clock::now());

    auto legacy = store_->fetch_by_status("legacy");
    for (auto& row : legacy) {
        auto decision = apply_legacy_review(row, now);

        if (decision.log_entry) {
            auto [from_s, to_s, reason] = *decision.log_entry;
            store_->log_promotion(
                row.user_id, row.item_id, from_s, to_s,
                decision.score, reason);
            counts.demoted++;
        }

        MemoryItemRow updated = row;
        updated.importance_score = decision.score;
        updated.status = decision.status;
        updated.decay_started_at = decision.decay_started_at;
        store_->update_item(updated);
        counts.reviewed++;
    }

    return counts;
}

// =============================================================================
// RECALL
// =============================================================================

std::vector<MemoryItemRow> MemoryModule::recall(
    const std::string& user_id,
    const std::string& query,
    const std::optional<std::string>& hemisphere,
    int limit,
    bool include_subconscious)
{
    std::vector<MemoryItemRow> results;
    if (!store_) return results;

    // Current query vectors: semantic (embedder) + ethical (encoder).
    std::optional<std::vector<double>> query_embedding;
    std::vector<double> query_coords;

    if (!query.empty()) {
        query_embedding = embedder_->embed(query);
        query_coords = encode_coordinates(*engine_, query);
    }

    // Fetch applicable items.
    std::vector<MemoryItemRow> candidates;
    auto active = store_->fetch_by_status("active");
    auto legacy = store_->fetch_by_status("legacy");
    candidates.insert(candidates.end(), active.begin(), active.end());
    candidates.insert(candidates.end(), legacy.begin(), legacy.end());

    if (include_subconscious) {
        auto sub = store_->fetch_by_status("subconscious");
        candidates.insert(candidates.end(), sub.begin(), sub.end());
    }

    // Score each candidate.
    std::vector<std::pair<double, MemoryItemRow*>> scored;
    for (auto& row : candidates) {
        if (row.user_id != user_id) continue;
        if (hemisphere && row.hemisphere != *hemisphere) continue;

        std::optional<std::vector<double>> row_embedding;
        if (query_embedding && !row.narrative.empty()) {
            row_embedding = embedder_->embed(row.narrative);
        }

        double sem = cosine(
            query_embedding ? &*query_embedding : nullptr,
            row_embedding ? &*row_embedding : nullptr);
        double eth = ethical_similarity(
            query_coords.empty() ? nullptr : &query_coords,
            row.ethical_coordinates.empty() ? nullptr : &row.ethical_coordinates);
        double importance = row.importance_score / 10.0;

        double score = recall_score(importance, sem, eth);
        scored.emplace_back(score, &row);
    }

    // Sort descending by score, take top N.
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    int taken = 0;
    for (auto& [score, row] : scored) {
        (void)score;
        if (taken >= limit) break;
        results.push_back(*row);
        taken++;
    }

    return results;
}

std::unordered_map<std::string, std::vector<std::unordered_map<std::string, std::string>>>
MemoryModule::inject_context(
    const std::string& user_id,
    const std::string& query,
    int personal_limit,
    int wisdom_limit)
{
    std::unordered_map<std::string, std::vector<std::unordered_map<std::string, std::string>>> result;

    auto personal = recall(user_id, query, "personal", personal_limit, false);
    auto wisdom = recall(user_id, query, "impersonal", wisdom_limit, false);

    std::vector<std::unordered_map<std::string, std::string>> recent_episodic;
    for (const auto& row : personal) {
        std::unordered_map<std::string, std::string> entry;
        entry["narrative"] = row.narrative;
        entry["emotional_marker"] = row.emotional_marker;
        entry["importance"] = std::to_string(row.importance_score);
        recent_episodic.push_back(entry);
    }

    std::vector<std::unordered_map<std::string, std::string>> key_semantic;
    for (const auto& row : wisdom) {
        std::unordered_map<std::string, std::string> entry;
        entry["concept"] = row.concept_name.value_or(row.narrative.substr(0, 80));
        entry["understanding"] = row.understanding.value_or(row.narrative);
        entry["type"] = row.kind;
        key_semantic.push_back(entry);
    }

    result["recent_episodic"] = recent_episodic;
    result["key_semantic"] = key_semantic;
    return result;
}

} // namespace lina::memory_module
