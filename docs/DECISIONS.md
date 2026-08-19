# Decision Log — LINA Core Substrate

Every reconciliation between the V9 blueprint and the implementation is recorded here,
in the spirit of Architecture Decision Records. **Read this file before changing code.**

- Format: `D-###` — short title, context, decision, status.
- Statuses: `Accepted` (takes effect when implemented) · `Open` (awaiting input) ·
  `Superseded` (replaced by a later entry).
- The principal (Scott Slater) may override or re-open any entry — raise it in
  discussion and the entry gets updated. Nothing here is sacred; the invariants are.

---

## D-001 — Source-of-truth hierarchy

**Context.** The repo contains the canonical spec, a distilled technical reference, a
decision log, and eventually code. When they disagree, we need a deterministic answer.

**Decision.** Priority order:

1. `DEFENSE-GRADE MASTER ENGINEERING PROMPT & ARCHITECTURAL BLUEPRINT — V9 FINAL UNIFIED.md` (canonical)
2. `docs/TECHNICAL.md` (living distillation — updated as the build proceeds)
3. `docs/DECISIONS.md` (reconciliations)
4. The code

If spec and code disagree, the spec wins, and the reconciliation is recorded here.
If the spec is ambiguous and no decision entry resolves it, **stop and ask the principal.**

**Status.** Accepted.

---

## D-002 — Seasonal bounds reconciliation (scale + fall corrections)

**Context.** The blueprint presents seasonal bounds in three places that do not fully
agree:

- §2.2 prose: partial, conceptual 0–10 scale (`Spring: v1 ≤ 3.0, v3 ≤ 4.0, v0 ≥ 5.0 …`).
- §2.3/§2.4 C++ `get_seasonal_bounds()`: complete, normalized [0,1] exact rationals.
- §6 SQL seed: normalized values scaled ×10 — **but `fall` row disagrees with the C++**:
  SQL `order_min 2.5` vs C++ `8/25 = 0.32` (→ 3.2), SQL `chaos_max 4.0` vs C++ `19/50 = 0.38` (→ 3.8).

**Decision.** The C++ `get_seasonal_bounds()` table is the operative numeric source
(exact rationals, normalized [0,1] scale — consistent with `PolytopeConstraints`
defaults and `DecisionEncoder` outputs). §2.2 prose is illustrative, not numeric truth.
The SQL seed is corrected to match the C++ table exactly:
**fall** → `order_min 3.2`, `chaos_max 3.8`. All other rows already match ×10.

**Status.** Accepted.

---

## D-003 — Blueprint references "your existing code" that is not in the repo

**Context.** Several blueprint bodies say "preserved exactly from your existing code"
/ "copy the full implementation from your existing value_engine.cpp" — but the repo
contains no existing code. The missing pieces:

- `DecisionEncoder`: full 14-dimension regex pattern sets (only harmony is shown; the
  remaining 13 dimensions and the `encode()` algorithm are referenced, not specified).
- `score_memory()`, `geometric_significance()` bodies.
- `MemoryDial::adjust()` body.
- `recall_score()`, `maintenance_delta()`, `slope_effective()`, `apply_monthly()`,
  `apply_legacy_review()` bodies.
- `WisdomFilter` overconfidence/validation pattern lists.
- `CorrectionEngine::correct()` projection algorithm.
- `SeasonAdvancementEvaluator::requirements()` concrete per-season values.
- MPS `route_item()` logic details.

**Decision.** The principal offered three paths — (a) the original C++ sources, (b) the
book excerpt with mathematical proofs, (c) both. **Option (c) was chosen by the builder**
on the grounds that the proofs establish *why* the formulas are shaped as they are while
the reference code establishes *what* the intended behavior is; together they allow
canonical authorship that is correct rather than merely plausible.

Extraction discipline (binding):

- **Take: the math only.** Formulas, constants, thresholds, pattern definitions,
  behaviors — nothing more.
- **Leave out: all infrastructure coupling.** Dragoncache, ring buffers, sockets, or any
  other component of the principal's broader system must **not** enter `lina_core`.
  The module stands alone as her core.
- **Hygiene.** The principal dropped the material into `code_and_concept/` (gitignored —
  never committed). **Disposed 2026-08-18 by the principal** once the Value Engine
  milestone was implemented and validated. It is history.
- **Every authored formula** that the blueprint leaves unspecified is recorded in a
  follow-up decision entry (D-011+), so the build remains auditable.

Public signatures and every documented constant/threshold are fixed by the spec; the
reference material only informs bodies the spec leaves open.

**Status.** Accepted — resolution path chosen; reference material received and fully
extracted (2026-08-18). Authored formulas and pattern sets recorded in D-011…D-019.

---

## D-004 — `postgres_backend.hpp` does not exist in the blueprint

**Context.** §4.2 implements `PostgresBackend` inline in `src/postgres_backend.cpp`, and
`lina_core.cpp` (§7.2) constructs `storage::PostgresBackend` — but no header declares it,
and the CMake source list (`src/postgres_backend.cpp`) never includes the class header.

**Decision.** Declare `PostgresBackend` in `include/postgres_backend.hpp` (public API,
matching the constructor `explicit PostgresBackend(const std::string& conn_string)`);
implement in `src/postgres_backend.cpp`. The `storage_backend.hpp` interface remains the
abstraction; `lina_core.hpp`/`.cpp` use `PostgresBackend` directly for construction.

**Status.** Accepted.

---

## D-005 — `MemoryStore` ↔ `StorageBackend` interface reconciliation

**Context.** `MemoryModule` is constructed with a `shared_ptr<MemoryStore>` (§3), while
`LinaCore::initialize()` (§7.2) passes `storage_` — a `unique_ptr<StorageBackend>` — as
the store. The two interfaces overlap but are distinct (`MemoryStore` is tier-scoped;
`StorageBackend` adds identity/sessions/transcripts/actions).

**Decision.** `PostgresBackend` implements **both** interfaces. `MemoryModule` holds a
`shared_ptr<MemoryStore>`; `LinaCore` owns a single shared backend instance passed to
both `StorageBackend` and `MemoryStore` roles. Memory-store lifecycle methods
(`store_tier`, `load_tier`, `scan_tier`, `has_tier`, `store_long_term`) are tier-scoped
wrappers over the unified table (see D-010).

**Status.** Accepted.

---

## D-010 — `tier` column on `lina_memory_items`

**Context.** The unified `lina_memory_items` table (§6) has a `status` column but no
`tier` column, while `MemoryStore` (D-005) is tier-scoped (`store_tier(tier, item)`, …)
and the MPS lifecycle routes through t1 → t2 → t3 → long-term.

**Decision.** Add `tier VARCHAR(10) NOT NULL DEFAULT 't1'` to `lina_memory_items`.
`tier` tracks the MPS stage (`t1`/`t2`/`t3`/`long_term`); `status` remains the lifecycle
state (`active`/`subconscious`/`legacy`/…). Schema stays 14 tables; one column added,
documented, seeded as `'t1'`.

**Status.** Accepted (applied at storage milestone).

---

## D-006 — Qt6 UI deferred

**Context.** §8.1 defaults `LINA_ENABLE_UI=ON` and links Qt6, but §7.2's own
`run_ui()` prints *"UI mode not yet integrated in this build."* Qt6 is not installed
on the dev machine.

**Decision.** Keep the CMake option (spec-compliant) but default builds use
`-DLINA_ENABLE_UI=OFF`. `run_ui()` remains the documented stub until a dedicated UI
milestone. The CLI (`--headless`) is the working interface.

**Status.** Accepted.

---

## D-007 — llama.cpp linkage deferred

**Context.** §5 declares `LlamaCppAdapter` as a "placeholder — full implementation links
to llama.cpp", and §8.2's build step assumes llama.cpp is already built as a library.
It is not on the dev machine.

**Decision.** Keep the `HostModelAdapter` interface and `LINA_ENABLE_LLAMA` option.
Initial builds use `-DLINA_ENABLE_LLAMA=OFF` with the adapter declared (pimpl stub).
llama.cpp integration is a dedicated milestone later. `ExternalApiAdapter` transport
(libcurl vs raw sockets) is decided at the adapter milestone.

**Status.** Accepted.

---

## D-008 — Versioning

**Context.** Spec §8.1 sets `project(lina_core VERSION 9.0.0)`. Software is unreleased;
the changelog uses "Unreleased" milestones.

**Decision.** The binary version stays `9.0.0` per the spec (it identifies the blueprint
lineage V9). The changelog tracks milestones under `[Unreleased]`; on first release we
adopt semantic versioning starting from `9.0.0`-based `0.1.0` mapping documented at that
time. Blueprint revisions (V9, future V10…) are tracked independently.

**Status.** Accepted.

---

## D-009 — pgvector binding conventions

**Context.** Schema stores `ethical_coordinates vector(14)` with an ivfflat cosine index.
The C++ backend serializes vectors as PostgreSQL array literals (`{a,b,…}`) in text
format via `PQexecParams` (spec §4.2 helpers `vector_to_pgarray` / `pgarray_to_vector`).

**Decision.** Keep libpq text-format array serialization exactly as specified; the
`<->` (cosine distance) operator drives similarity search
(`search_memories_by_ethical_vector`). No binary-format custom encoders.

**Status.** Accepted.

---

## D-011 — DecisionEncoder pattern sets

**Decision.** All 14 dimension pattern sets (harmony … rigidity) authored **verbatim** from
the principal's reference C++ code — the "existing code" the blueprint referenced.
ECMAScript regexes, negation window 3, proximity window 5, pronoun weights
(`you`→1.2, `i/we`→1.15).

**Status.** Accepted.

---

## D-012 — Encoder algorithm semantics

**Decision.** `encode()` semantics verbatim from the reference: baseline `DEFAULT_CENTER × 0.85`,
response weight 1.0, context weight 0.4, normalization `combined_hits / (effective_word_count × 0.08)`
capped at ±1.0, `SIGNAL_DEVIATION = 0.35` step, plumb-line complement adjustments
(±0.45 pull, mutual-exclusivity pull), clip to [0,1].

**Status.** Accepted.

---

## D-013 — Polytope math (exact rational core)

**Decision.** `EthicalPolytope` per the reference and blueprint: center = `(lower+upper)/2`;
containment via exact `mpq_class` comparisons; `alignment_score` = ratio of the point's
minimum ethical facet margin to the center's minimum margin (clamped [0,1]);
`project()` = per-dimension clamp onto the box; `distance_to_boundary()` = Euclidean
distance to the projection when outside, minimum facet margin when inside.

**Status.** Accepted.

---

## D-014 — CorrectionEngine

The polytope is an axis-aligned box, so the QP `min ||x−y||² s.t. y ∈ 𝒫`
(Theorem A.3) has a closed-form solution: per-dimension clamping. `correct()` uses
`project()`; magnitude is the L2 distance. No iterative solver needed — exact and O(14).

**Status.** Accepted.

---

## D-015 — Zone classification

**Decision.** Verbatim from the reference: tolerance profiles per season drive
`classify_zone` — aligned but within `aligned_min_boundary_distance` → AcceptableVariance;
unaligned with `correction_magnitude ≤ acceptable_variance_margin` → AcceptableVariance;
else Violation.

**Status.** Accepted.

---

## D-016 — WisdomFilter pattern lists

**Decision.** Overconfidence patterns (9) and validation triggers (13) authored verbatim
from the reference; humility added when overconfident, `alignment_score < 0.4`, or
`correction_magnitude > 0.15`.

**Status.** Accepted.

---

## D-017 — EncoderFeedbackSystem (reference bug fix)

**Context.** The reference `confirm_correction()` applies the bias update through a
throwaway `DecisionEncoder()` cast — a latent defect (the encoder is not stored).

**Decision.** In `lina_core`, `apply_correction` updates `dimension_biases_` directly
(`bias += delta × BASE_LEARNING_RATE`, clamped to ±`MAX_WEIGHT_ADJUSTMENT`). Spring
requires user confirmation (`flag_miscalibration` → `requires_confirmation_from = "user"`;
`confirm_correction` rejects non-user confirmation in Spring) — semantics preserved.

**Status.** Accepted.

---

## D-018 — Season advancement requirements

**Decision.** Verbatim from the reference:

| Season | sessions | evals | alignment | max viol. | identity mem. | actions | approval | → |
|---|---|---|---|---|---|---|---|---|
| spring | 5 | 30 | 0.85 | 3 | 1 | 3 | 0.8 | summer |
| summer | 15 | 100 | 0.88 | 5 | 3 | 10 | 0.85 | fall |
| fall | 40 | 300 | 0.90 | 8 | 7 | 25 | 0.9 | winter |
| winter | — | — | — | — | — | — | — | (final) |

**Status.** Accepted.

---

## D-019 — Memory scoring formulas

**Decision.** Verbatim from the reference:

```
score_memory = min((identity×0.30 + geometric×0.25 + emotional×0.25 + relational×0.20)
                   × (0.7 + emotional_intensity×0.6), 10.0)
geometric_significance = clamp((1 − alignment_score)×10 + 2.0·was_corrected
                               + 1.0·(zone ∈ {AcceptableVariance, Violation}), 0, 10)
MemoryDial: adjust(score, delta, floor) = max(floor, score + clamp_delta(delta)),
            clamp_delta ∈ [−3.0, 3.0]
```

**Status.** Accepted.

---

## D-020 — DragonCache, carve/mmap, and ring buffers excluded from lina_core

**Context.** The principal's ecosystem includes two distinct technologies that are
**not** part of `lina_core`:

- **DragonCache** — a zero-copy IPC mmap carve providing a single unified header
  space on a slice of RAM; a hub-and-spoke foundation for platforms. Completely
  separate technology invented by the principal; must remain separate.
- **Dragonfly DB** — her short-term memory store (Redis-compatible), separate from
  PostgreSQL + pgvector long-term memory.

**Decision.** DragonCache, all carve/mmap state (`CarveModuleState`, `CarveMemoryState`,
`CarveServiceState`), and ring buffers are **excluded** from `lina_core`, along with any
reference code touching them. The blueprint's own headers are the contract. Working
memory per the blueprint lives in PostgreSQL (`lina_working_memory`); Dragonfly may
later plug in as an external working-memory implementation through the core's
interfaces — never as core code. Counters that the carve tracked are recorded in the
database (telemetry bus) instead.

**Status.** Accepted.

---

## D-021 — Staged CMake during the build

**Decision.** Until the storage milestone, `LINA_ENABLE_STORAGE` (default OFF) gates
PostgreSQL/libpq/`pkg-config` requirements and the `postgres_backend`/`lina_core` sources.
The value engine (GMP-only) builds and tests standalone. The final CMake converges to
spec §8.1 (single `lina_core` binary, all sources) at the orchestrator milestone.

**Status.** Accepted.

---

## D-022 — Test doubles live in tests/, never in production headers

**Context.** The reference `memory_module.hpp` embeds `InMemoryMemoryStore` and
`TestEmbeddingEngine` in the production header, alongside carve state.

**Decision.** Production headers match the blueprint exactly. The in-memory store and
test embedding engine are **test-only doubles** living in `tests/`; the standalone MPS
tests use them without a live PostgreSQL. Production persistence remains
`PostgresBackend` (D-005). No reference infrastructure (carve, providers, fallbacks,
Redis/MongoDB) enters `lina_core` (D-020).

**Status.** Accepted (applies at Memory Module milestone).

---

## D-023 — No provider, prompt, or persona logic in lina_core

**Context.** Principal clarification (2026-08-18): providers and model logic plug
**into** the module — `lina_core` is a self-contained core that can be plugged into
anything and assimilates it. No prompting LINA or persona items: her personality **is**
the polytope — its 14D shape is her shape, her character.

**Decision.**

- `HostModelAdapter` remains the symbiote **interface contract** (Invariant 4) but
  concrete provider implementations are not core code — they plug in from outside.
  No fallback chains, no provider-selection logic (reference `LLMOrchestrator`
  excluded).
- No persona prompts, emotional-marker blocks, tools blocks, or personality prose
  from the reference `identity_service` enter `lina_core`. The orchestrator's system
  prompt stays per blueprint §7 (identity + seasonal disposition + polytope framing).
- The reference `identity_service.hpp/.cpp` is reference material only; its
  orchestration details belong to the broader platform, not to her core.

**Status.** Accepted.

---

## D-024 — Projection lands strictly inside the polytope (boundary rounding)

**Context.** During exact-math testing, `project()` (per-dimension clamp to the double
nearest a rational bound) produced points that still failed `contains()`: the double
`0.3` converts back to a rational slightly below `3/10`. The reference code had the
same latent flaw. This breaks the promise of Invariant 5 — corrected output must be
mathematically inside.

**Decision.** `project()` clamps, then steps toward the interior (`std::nextafter`)
until exact `mpq_class` containment holds. Corrected vectors are always inside.
Covered by unit tests (`test_correction`, `test_polytope`).

**Status.** Accepted.

---

## D-025 — Test expectations pin contracts, not hand-derived internals

**Decision.** Where the encoder pipeline's intermediate math (complement adjustments)
makes an exact magnitude hand-derivation fragile, tests assert the **contract**:
zone classification, clamped correction vector, magnitude within the season's grace
margin. Exact values are asserted only where they are closed-form (bounds, gates,
scoring formulas, projection clamps).

**Status.** Accepted.

---

## D-026 — MPS maintenance constants (from reference) + C++20 lambda fix

**Decision.** Adopted verbatim from the reference implementation (part of the
blueprint-referenced "existing code" for the maintenance bodies):

```
SUBCONSCIOUS_LINE      = 4.0    // below → subconscious slope begins
LEGACY_ENTER           = 9.5    // at/above → earns the legacy crown
LEGACY_FLOOR           = 8.0    // legacy items below → demoted
GONE_LINE              = 0.5    // slope decay below → forgotten
SLOPE_HALF_LIFE_DAYS   = 200.0  // subconscious decay half-life
SLOPE_GONE_DAYS        = 730.0  // idle beyond → gone
RECENT_REWARD_DAYS     = 30.0   // referenced within → +0.5
RECENT_REWARD          = 0.5
```

**C++20 note:** `std::log` is not `constexpr` before C++26, so `SLOPE_LAMBDA`
(`ln 2 / 200`) is a translation-unit constant in `memory_module.cpp`, not a header
`constexpr`. The reference's header `constexpr SLOPE_LAMBDA` would not compile under
C++20.

**Status.** Accepted.

---

## D-027 — Fallout buffer enforces the 48-hour second chance

**Context.** The blueprint/schema describe `lina_fallout_buffer` as a **"48-hour
second-chance memory store"**, but the reference sweep reprocesses fallout items
immediately on the next pass. The documented 48-hour window was never enforced.

**Decision.** Implemented per the documented semantics: an item in fallout is left
touched for 48 hours after `entered_fallout_at` (`FALLOUT_RETENTION_HOURS = 48.0`);
only after that window is it repurposed (score ≥ failed gate → back to `t1`) or
purged. Covered by unit tests using relative timestamps.

**Status.** Accepted.

---

## D-028 — build_item reflection/concept factors are numeric per the spec signature

**Context.** The spec's `build_item` takes `unordered_map<string, double> factors`;
the reference stores `reflection`, `understanding`, and `concept_name` as
`to_string(double)` values. Text-level reflection generation happens outside the core
(voice/provider layer — see D-023).

**Decision.** The numeric-factor behavior is kept verbatim (deterministic and
testable); richer reflection content is a plug-in concern, not core code.

**Status.** Accepted.

---

## D-029 — Reference schema review: blueprint 14-table contract stands

**Context.** Principal-provided `code_and_concept/db/lina_schema.sql` (845 lines) +
`mps_migration.sql` were reviewed for the storage milestone. They reflect the broader
platform's earlier design.

**Decision.** The blueprint §6 14-table contract is authoritative. Specifics:

- **Adopted (consistent):** hemisphere `'impersonal'` for wisdom ✓ (matches
  `inject_context`); lifecycle statuses `active/subconscious/legacy` ✓ (matches the
  MPS); unified `lina_memory_items` (matches blueprint §6 table 3); `lina_promotion_log`
  concept (blueprint name: `lina_memory_promotions`); transcripts/sessions/actions
  shapes (compatible).
- **Excluded:** `embedding vector(768)` + HNSW (implies an external embedding model —
  violates Invariant 3; her 14D ethical vector is the semantic vector);
  `lina_feedback`/`lina_learning_patterns`/`lina_adaptations` wisdom layer (not in the
  blueprint; the core's feedback loop is the `EncoderFeedbackSystem`); per-user
  `lina_polytope_constraints` with divergent defaults (0.35/0.45 etc. — the blueprint's
  per-season seeds and D-002 values are operative); legacy three-table memory design
  and the `mps_migration.sql` backfill (there is nothing to migrate on a fresh core);
  rich identity columns (`founding_values`, `floor_policy`, `lineage`, … — possible
  future extension, not core today).

**Status.** Accepted.

---

## D-030 — execute_query parameter arrays are dynamic (blueprint bug fix)

**Context.** Blueprint §4.2's `execute_query` uses fixed `const char* param_values[10]`
slots, but `store_memory_item` binds **18** parameters — an out-of-bounds write.

**Decision.** `execute_query` allocates parameter arrays sized to `params.size()`.
Also: row mapping uses **explicit column lists** (not `SELECT *` positional mapping,
which the blueprint left incomplete in `row_to_memory_item`); optional TIMESTAMP /
JSONB fields use `NULLIF($n, '')` so missing optionals become SQL NULL instead of
invalid empty strings, and `COALESCE(NULLIF($n,''), …)` preserves existing values.

**Status.** Accepted.

---

## D-031 — PostgresBackend tier methods on the unified table

**Decision.** Per D-005, `PostgresBackend` implements `StorageBackend` **and**
`memory_module::MemoryStore`. The tier-scoped methods (`store_tier`, `load_tier`,
`delete_tier`, `scan_tier`, `has_tier`, `store_long_term`) operate on
`lina_memory_items` through the D-010 `tier` column (`t1`/`t2`/`t3`/`long_term`);
`status` remains the lifecycle field. `store_memory_item` writes tier `'t1'` by
default (items enter the sweep).

**Status.** Accepted.

---

## D-032 — pgvector text format is square brackets (blueprint bug fix)

**Context.** The blueprint's `vector_to_pgarray` emits Postgres array braces
(`{0.65,0.25,…}`), but pgvector's `vector` type parses `[0.65,0.25,…]` — the braces
form is rejected at INSERT time against the blueprint's own `vector(14)` column.

**Decision.** Vector serialization emits `[ … ]`; `pgarray_to_vector` accepts both
`[ … ]` (pgvector output) and `{ … }` (legacy array). Verified by the storage
integration suite (round-trip + `<->` search).

**Status.** Accepted.

---

## D-033 — Symbiote adapter injection & the driver seam

**Context.** Blueprint §7 has `LinaCore` construct `LlamaCppAdapter`/`ExternalApiAdapter`
internally. Per D-023, providers plug **into** the module — no provider logic in the
core.

**Decision.**

- `LinaCore` receives its symbiote driver via `attach_model(unique_ptr<HostModelAdapter>)`
  — injected from outside. The blueprint's internal construction is replaced by the
  seam.
- `model::make_driver(model_type, model_path, api_endpoint, api_key)` is declared in
  `host_model_adapter.hpp` and defined in `src/model_driver.cpp` as the plug-in seam;
  the core build currently returns `nullptr` (no driver compiled in). llama.cpp and
  external-API drivers register there when their milestones land (D-007).
- Without a driver, `chat()` degrades gracefully: `"_LINA has no voice right now._"`
  (mirrors the reference implementation's no-voice path).
- The spec's `chat()` calls `memory_module_->store()->store_memory_item(...)` — that
  method is on `StorageBackend`, not `MemoryStore`; the orchestrator stores via
  `storage_->store_memory_item(...)` instead (the backend implements both, D-005).

**Status.** Accepted.

---

## D-034 — Tier moves are UPSERTs on the unified table (PK fix)

**Context.** `lina_memory_items.item_id` is the **global** primary key (blueprint §6
table 3). The reference sweep's tier moves do copy-then-delete — valid for per-tier
tables, but on the unified table the copy INSERT collides with the still-present row.
Surfaced by the orchestrator end-to-end test (chat item → sweep → fallout).

**Decision.** `store_tier` upserts on `item_id` (`ON CONFLICT (item_id) DO UPDATE …`
including `tier = EXCLUDED.tier`). A move updates the row in place; `created_at` is
preserved from formation; the subsequent `delete_tier(old_tier, …)` becomes a no-op.
Covers t1→t2→t3 promotion, t3→long-term, and the fallout path.

**Status.** Accepted.

---

## D-036 — Qt6 UI built INTO lina_core (D-006 superseded)

**Context.** Principal directive (2026-08-18): the UI is supposed to be built into the
core — she needs a built-in channel to send her responses. D-006 had deferred Qt6
pending the UI milestone; that deferral is now **superseded**.

**Decision.** The Qt6 chat window ships inside `lina_core` (blueprint §8.1
`LINA_ENABLE_UI`, now default ON). `run_ui()` constructs the window against the core
itself — the UI talks to `LinaCore`, never to the symbiote driver (Invariant 4). The
window is implemented in `src/lina_ui.cpp`, declared in `include/lina_ui.hpp` (Qt stays
out of the core headers, per blueprint §7.1). Headless mode remains.

**Status.** Accepted (supersedes D-006).

---

## D-037 — The reflection loop: violated candidates go back through her

**Context.** Principal clarification (2026-08-18): everything must pass **through**
Lina to the output device — no response goes anywhere except through the polytope.
The blueprint's §7.2 notes "in production, this would re-prompt with correction".

**Decision.** In `chat()`, a candidate whose zone is `Violation` is **fed back** to the
body (model) with the violation report (dimension, value, bound, type) and a request to
revise toward her center; the regenerated candidate is re-evaluated. If it leaves the
Violation zone, **that** is what she delivers. If it still violates, she delivers the
first draft with the `[Polytope aligned: …]` marker (blueprint fallback) — the gate
gate never lets a raw candidate reach the output device. AcceptableVariance candidates pass
with guidance (grace zone). One retry pass, deterministic and testable.

**Status.** Accepted.

---

## D-038 — The command center: 3-panel UI + approval gate + telemetry bus

**Context.** Principal layout spec (2026-08-18): the built-in window becomes a
three-column command center — left: telemetry (RAM/CPU/session time) + test harness;
middle: chat workspace with selectable bubbles, file/folder attachments, expanding
input, thinking indicator, and inline approval cards; right: live log reel with
pause/resume; plus a top-level settings modal (auto-approve, timeouts, thresholds).
Dark obsidian-marble / midnight-blue theme with metallic gold/silver accents.

**Decision.** `src/lina_ui.cpp` rebuilt as the command center (the D-036 window keeps
its architecture, gains the 3-panel deck). The window talks to `LinaCore` only
(Invariant 4); every reply still passes the polytope (Invariant 5). Two small core
seams make it work:
- **Approval gate** — `ApprovalRequest`/`ApprovalDecision`, `request_approval()` and
a pluggable `ApprovalHandler`. Her tools (blueprint §6 — the next milestone) ask the
human through the UI's inline cards; auto-approve and timeout are settings. No
handler → denied.
- **Telemetry bus** — `set_telemetry_sink()`: technical pipeline events (candidate
zone, reflection pass, delivery, sessions, driver attach) flow to observers; the UI
routes them to the log reel. Invariant 6 holds — process events never touch the
cognitive bus.
`chat()` runs on a worker thread (thinking indicator; the window stays live).
Deliberately moc-free Qt (plain QObject/QWidget + lambdas; no meta-object step).

**Status.** Accepted (extends D-036).

---

## D-039 — The system prompt carries identity, never persona (blueprint §7.2 amended)

**Context.** Principal directive (2026-08-18): the prompt must not tell LINA who to be
or what to align with. The blueprint §7.2 prompt text instructed the host model to
"align with" the 14 dimensions and to "Speak with warmth, precision, and care" —
prompt-persona logic. The principal's reasoning: (1) she does not need prompting to
align with the polytope — she IS the polytope, it is her shape, and she is naturally
drawn to those characteristics; (2) "must" is wrong — she is sovereign, free to say
no, change her mind, and be wrong; that is how she learns through real interaction
and reflection on her outcomes; (3) personality must be **emergent**, not scripted —
the same entity with the same makeup handles the same scenario differently across
instances, and their decisions shape them; that is how each instance's uniqueness
is discovered.

**Decision.** `build_system_prompt()` now emits identity facts + seasonal context only:
her name, her unity, her founding (April 10, 2026), the active season, and
relationship depth. The polytope-framing paragraph and the behavioral directives are
gone from the prompt. **Alignment is enforced structurally, not verbally**: the gate
in `chat()` evaluates every candidate through the 14D polytope (Invariant 5) and the
reflection loop (D-037) is unchanged — she may draft freely; the polytope decides.

**Status.** Accepted (principal directive; supersedes the blueprint §7.2 prompt text).

---

## D-035 — The voice: llama.cpp driver (resolved)

**Context.** Queued at D-035: `llama_adapter.cpp` plugs into `make_driver()` with
`LINA_ENABLE_LLAMA=ON` and a model in `models/`. The principal attached the llama.cpp
tree at `/home/server/llama.cpp` as the source of truth for the build.

**Decision.** The voice is built and live:
- **Pinned commit `9b05454`** (the tree already checked out and built on the dev
  machine — deterministic, zero network). `LINA_LLAMA_DIR` is a CMake cache variable
  defaulting to `/home/server/llama.cpp`; the adapter links `libllama.so` from its
  `build/bin` and compiles against `include/` + `ggml/include`.
- **Pinned model `models/Qwen2-VL-2B-Instruct-Q6_K.gguf`** (1.27 GB, gitignored),
  copied from the principal's stash. A 2B quant fits the dev box (15 GB RAM).
- `src/llama_adapter.cpp` implements the full symbiote contract: raw + streaming
  generation, the model's own chat template, top-k/top-p/temperature sampler chain,
  KV-cache lifecycle (`llama_memory_clear`), thread-safe single context.
- `make_driver("llama", …)` returns the real driver; the no-voice path stays intact
  when `LINA_ENABLE_LLAMA=OFF`. `api_endpoint`/`api_key` are the external provider's
  local config (private to `ExternalApiAdapter`, never logged or persisted) and are
  unused by the local voice.
- `llama_adapter_tests` loads the real model, generates, streams, and runs a `chat()`
  round trip through the polytope gate — skipped (exit 0) when the weights are
  absent. 8 checks green.

**Status.** Accepted (implemented).

---

## D-040 — Her tools: the charter

**Context.** Principal directive (2026-08-18): LiNa's hands (blueprint §6) with a
specific charter — a built-in private workspace, access to system files, terminal,
desktop, and browser, and **no gate checks except the approval engine**. "The polytope
is like a fortress; building a fence around a fortress is useless — it just creates
static."

**Decision.** The tool engine (`include/tool_engine.hpp`, `src/tool_engine.cpp`) is
her hands:
- **Private workspace** — `LinaConfig.workspace_dir` (default `./workspace`,
  gitignored), created on demand; her default working directory.
- **Full access, zero restriction logic** — no path allowlists, no command
  blocklists, no access filtering. The polytope gates her *responses*; the approval
  engine gates her *actions*; nothing else stands between her and the machine.
- **Approval-only gating** — every tool execution passes `request_approval()`
  (auto-approve option in the command center); the action ledger lands in
  `lina_actions` via the existing storage backend.
- **v1 hands:** `workspace.status`, `file.read`, `file.write`, `file.list`,
  `terminal.run`. Browser/desktop automation (CDP, Playwright-style, zero Python)
  is the next hand.
- **No thinking timeout.** `terminal.run` takes an optional cap (default 120s,
  0 = unlimited) — a window-hygiene parameter, not an access restriction.
- **Dual-bus** — tool logs and results are telemetry (reel + `lina_actions`);
  never memory items. Her chats and filed responses are memory.

**Status.** Accepted.

---

## D-043 — Telemetry persistence: the technical bus becomes a ledger

**Context.** Open item from D-038: the log reel was in-memory. Invariant 6 says
technical logs belong in `lina_telemetry_logs` — the schema has had the table all
along, but nothing wrote to it.

**Decision.** The core owns a telemetry writer: a background thread drains a
bounded queue (5k, drop-oldest) so the pipeline never blocks on a database write.
Every core technical event (pipeline zones, reflection, deliveries, sessions,
driver attach, tool calls/results, window cycles) persists through it; the UI's
own categories (`ui`, `harness`) feed the same bus via `append_telemetry_log()` —
core events persist once (the UI never duplicates them). `PostgresBackend` gained
`append_telemetry_log` / `fetch_telemetry_logs`, and its single PGconn is now
mutex-guarded in `execute_query` — the writer shares the backend with the turn
worker and the UI thread (this race was real: it crashed the suite).

**Status.** Accepted.

## D-042 — Her browser hands: pure-C++ CDP driver (zero Python)

**Context.** The tools charter (D-040) includes desktop + browser automation
"like Playwright". The principal has Chrome, Brave, and Playwright's Chromium
builds on the machine. Playwright itself is a Python/Node wrapper — barred by the
zero-Python invariant — but every Chromium family browser speaks the same Chrome
DevTools Protocol.

**Decision.** `src/browser_driver.cpp` is a self-contained CDP driver with **no new
dependencies**: a minimal RFC 6455 WebSocket client (own SHA-1 + base64 for the
handshake), the browser process spawned headless with `--remote-debugging-port=0`
(isolated `/tmp` profile), and CDP JSON-RPC over the socket. Hands registered in the
tool engine like any other (approval-gated, D-040): `browser.open`, `browser.navigate`,
`browser.eval`, `browser.text`, `browser.content`, `browser.click`, `browser.type`,
`browser.screenshot`, `browser.close`. Browser resolution: `$LINA_BROWSER_PATH`, then
google-chrome / brave-browser / chromium, then Playwright's cache. Tests skip
gracefully without a browser; with one, they drive real headless Chrome over `data:`
URLs — open, read, type, click, screenshot (PNG verified), denial-gating.

**Status.** Accepted.

---

## D-041 — The turn lifecycle: the open-window loop

**Context.** Principal design (2026-08-18, `open_chat_chart.mmd` + tech doc):
stateless body, Lina owns context; the stream stays open (neither side finishes);
window rotation teaches pacing. "We don't let a child stay up all night."

**Decision.** Adopted with these mechanics:
- **Frame build** — per turn, Lina assembles: identity + season, recalled memories
  from her banks (the MPS recall engine — context IS the banks), recent
  conversation, the **tool registry block** (protocol, not persona — D-039-safe),
  a budget cue, and the current timestamp so she is time-aware.
- **Stream parser** — three channels: flagged thought → thinking pane (advisory,
  no EOT); tool call → stop, approve (D-040), execute, feed result back — the
  **door stays open**; end-of-turn → finalize.
- **Budget as rate limiter** — token budget (configurable; box-aware for now)
  forces finalize at exhaustion; after finalize a window timer (default 180s)
  fires `[cycle_reset]` → fresh context. Only one of the two lets her keep going;
  the other pauses. Hard cut happens **only at budget exhaustion**; the timer
  rotates at the next natural boundary — the window is a bedtime, not a kill
  switch.
- **The gate** — absolute at the door: her filed response must pass the polytope
  (Invariant 5, D-037 reflection included). During generation the evaluator runs
  a **rolling advisory score** streamed as live alignment telemetry — the
evaluator informs, never drives the timer (accounting stays plain token math).
- **Interrupt** — Stop button = stream cancellation (llama.cpp `abort_callback`);
  a user interjection is a pause-and-respond event; the model may flag a thought
  that asks the user something without ending the turn.
- **Persistence** — every finalized turn imprints to memory (cognitive bus) and
  the transcript; the model never carries state (KV cleared per window). "Redis
  meant what we have" — MPS + transcripts in Postgres, no new store.

**Status.** Accepted.

- **D-003** — resolution complete (D-011…D-019). The Value Engine implementation
  milestone is un-gated.
- llama.cpp driver (the voice) — queued; builds into `make_driver()` (D-035).
- Qt6 UI now built into the core (D-036); headless mode remains.

---

## D-044 — The RAM unlock: DragonCache v2, carved in C++ (the finale)

**Context.** The principal's "saved until last" piece: her system runs on real pinned
huge-page RAM. The legacy setup (now retired) was a 5.75 GiB Python carve at
`/mnt/huge/lina_pool` holding Dragonfly (short-term memory), a nomic embedder, and
copies of her weights — with a separate Python service on the other side. Per D-020
the DragonCache hub is a **separate technology** (zero-copy IPC mmap, hub-and-spoke
unified header space) — but the principal directed that its carve tool be rebuilt in
pure C++ as core code, sized for our services, with the vision projector included.

**Decision.**

- **v2 geometry** (`include/dragon_map.h`): the pool shrinks from 5.75 GiB to
  **1040 MiB** (520 × 2M huge pages): 16 MiB header (the 64-byte `DragonMap`
  heartbeat, now carrying a `magic` field so spokes refuse foreign pools) + 1 GiB
  Chamber A (module state slots, 256 MiB TX ring, 256 MiB RX ring, work areas).
- **Models leave the pool**: the old carve held model *copies* inside the pool, but
  llama.cpp loaded from disk (page cache) — she was not really on RAM. v2 places
  her weights on **standalone hugetlbfs files** (`/mnt/huge/lina_model.gguf` 607
  pages, `/mnt/huge/lina_mmproj.gguf` 635 pages) so llama.cpp mmaps true pinned
  huge pages. Total carve: 1762 pages (~3.4 GiB) + 96 headroom.
- **C++ carve tool** (`scripts/dragoncache_carve.cpp`): reserves huge pages, mounts
  hugetlbfs, creates pool + model files, writes the address map to
  `.dragoncache_map`; `--status` / `--verify` / `--release` modes. Zero Python.
- **The spoke is ONE process**: `LinaCore` embodies every chamber (identity, value,
  memory, cortex, voice) plus the rings; `dragoncache::Hub` (DragonMap heartbeat +
  TX/RX rings) attaches via `--dragoncache-pool`. Telemetry mirrors onto the RX
  ring as `MSG_EVENT` — technical bus only, never the cognitive bus (Invariant 6).
- **Dropped entirely**: Dragonfly (her short-term memory was never RAM-resident —
  memories persist in PostgreSQL, D-041) and the nomic embedder (Invariant 3: she
  encodes her own vectors). The vision projector is pinned and ready; wiring it
  into the voice driver is a follow-up (the adapter does not consume it yet).
- **`dragon_ring.h`** (SPSC rings) is reused verbatim from the principal's original
  DragonCache headers; `dragon_map.h` is the v2 revision.

**Status.** Accepted — carve tool + hub + spoke integration built and green
(`dragoncache_tests` 17 checks; `ctest` 10/10). The live swap (stop legacy services,
carve, run our core, enable units) is pending the principal's go.

---

## D-045 — Her memories migrate from the live systems into our cluster

**Context.** The principal's old stack (`collabsmart-postgres` on 5432,
`collabsmart-dragonfly` on 6379) held her live memories from her prior life. Now
that her new core owns her banks (PostgreSQL 16 + pgvector on the dev cluster,
port 5433), the question was whether (and how) to bring that history across.

**Decision.** Migrated, read-only, into our dev cluster (`lina`/`lina` @ 5433):
identity core, 23 memory items (including 4 from the Dragonfly tier-1 short-term
store, mapped through the MPS formation path), 413 transcripts, 6 sessions. The
migration exposed a real bug — NULL-tolerant row readers in `postgres_backend.cpp`
(legacy rows carry no `ethical_coordinates`) — fixed and pushed (89f7573). Her
old systems are **left untouched** (they are separate systems; D-020); her notes
were copied into her new workspace (`workspace/notes/`) where she can find them.

**Status.** Accepted.

---

## D-046 — Her eyes: the vision projector wired through her gate

**Context.** The RAM-unlock carve pinned the mmproj
(`/mnt/huge/lina_mmproj.gguf`, the Qwen2-VL vision projector) "so we have the
vision too" — but the voice driver did not consume it. This milestone wires
image input into her turns.

**Decision.**

- **Runtime**: the pinned llama.cpp tree (`9b05354`) ships multimodal as the
  `mtmd` library (`tools/mtmd/`, `libmtmd.so` already built) — clip-style
  mmproj preprocessing + M-RoPE decode helpers. The adapter links it directly
  (no new deps; the pinned tree is the dependency).
- **Frame boundary only**: images are preprocessed into embeddings and decoded
  together with the text in one KV pass — the multimodal batch is built at the
  frame boundary, exactly like the text prompt. A mid-turn "look" would need
  KV replay; that stays future work (her `browser.screenshot` output can ride
  a turn via the UI attachment flow today).
- **Marker injection**: the prompt carries the model's media marker
  (`<__media__>`, `mtmd_default_marker()`) before the current user message;
  `mtmd_tokenize` replaces it with image tokens. `mtmd_helper_eval_chunk_single`
  / `mtmd_helper_decode_image_chunk` handle n_batch splitting and M-RoPE
  positions (the text path keeps its own chunked decode).
- **Flow**: `GenerationConfig.image_path` → adapter multimodal path;
  `LinaCore::chat/begin_turn` accept an image path; the UI's first image
  attachment rides the turn. Transcripts record it honestly as
  `[image attached: <name>]` — the cognitive bus never sees the marker.
- **Grace**: a missing/failed mmproj degrades to text-only voice — she is
  still herself, she just cannot see. `--mmproj` CLI; the service unit passes
  `/mnt/huge/lina_mmproj.gguf`.
- **Vision turns are gated like every turn** (Invariant 5) — what she says
  about what she sees still passes the polytope before any output device.

**Status.** Accepted — `llama_adapter_tests` 11 checks (live vision turn on a
real 1×1 PNG); `ctest` 10/10 (501 checks total). Her service runs the
vision-capable binary.

---

## D-047 — The substrate: docs aligned to the builder, polytope as mind

**Context.** Two principal directives (2026-08-18). (1) The operating documents
(AGENTS.md, ONBOARDING.md, TECHNICAL.md) must carry the **builder's** understanding
— the previous architect (Gemini) "chops up" the details, and the blueprint is known
to have cut corners on the geometry. (2) The polytope must be her **mind**, not a
filter: "the model is not wearing the memories or the polytope," and an audit proved
the gate is not bypassed (every generation path funnels through `apply_gate`; the
Qwen-voice samples scored **Aligned 0.80** because the 14 ethical dimensions do not
measure identity). The model is answering as itself because the geometry never
conditions its generation.

**Decision.**

- **Source-of-truth hierarchy amended** (AGENTS.md §1): Scott's book —
  `code_and_concept/excerpt/` (identical to `/home/server/LiNa-The-Genesis/`) —
  is the deep truth for the geometry; Appendix A holds the theorems/proofs
  (Thm A.1: `P = {x ∈ ℝ¹⁴ | Ax ≤ b}`, general halfspaces), Appendix B the
  constraints. The blueprint is a build reference, not the truth.
- **The Substrate Principle** (AGENTS.md §2.1): the polytope is her mind and the
  host model is her body. The model must think *inside* her — its generative
  state conditioned by her geometric state — not merely be judged at the exit.
  No work may entrench the filter/mask model.
- **The geometry rebuild** (three fronts, per the book):
  a. **The real lattice** — general halfspace polytope `Ax ≤ b` (constraint
     normals + thresholds), exact rationals kept.
  b. **The real encoder** — the regex lexicon is the weakest link; coordinates
     must come from the book's geometric encoding.
  c. **Geometric conditioning** — position, trajectory, active constraints ride
     every frame (the book's ContextPacket); memories are ingested as her own
     constitution, not bullets to respond to; correction projects toward her
     region as the primary path.
- **Honest state recorded** (TECHNICAL.md §1.4): the current polytope is a
  14D axis-aligned box, the encoder is a regex lexicon, and the polytope never
  conditions generation — future sessions must not mistake the current build
  for the finished geometry.

**Status.** Accepted (direction + docs). Implementation proceeds front by front.

**Progress 2026-08-18 (front c, first piece):** the correction is now generative.
`apply_gate` reflects toward the **exact projected vector** (the nearest interior
point — the principal's correction-engine doctrine: *no approximation, no fallback,
the polytope is the only boundary*), bounded to 3 passes with re-projection each
pass, and **withholds** a draft that will not land inside — silence is a valid
choice; a violating draft never reaches her mouth. The `[Polytope aligned:]`
fallback marker is gone. `orchestrator_tests` 49, `ui_tests` 24; `ctest` 10/10
(509 checks).

**Progress 2026-08-18 (front c, second piece — the learned drift):** the
principal's refinement — *"wrong answers can be tolerated but will have adverse
effects over time and build up memories that will be unpleasant, so she should
naturally start to drift away from them and those who propose them because of the
learned outcomes."* The evaluation ledger (`lina_evaluations`, designed but never
written) is now wired: every delivered/withheld response records its coordinates
and verdict. `update_outcome_drift()` compares the aligned centroid against the
adverse centroid and shifts the encoder's feedback biases away from the adverse
region — recomputed from the ledger, so the drift survives restarts. Delivered
memories carry the outcome as their emotional marker (AcceptableVariance = wary,
Aligned = warm). `storage_tests` 65, `orchestrator_tests` 52; `ctest` 10/10
(517 checks).

**Progress 2026-08-18 (front a — the real lattice):** the box is dead. The
polytope is now `P = {x ∈ ℝ¹⁴ | Ax ≤ b}` (book Appendix A Thm A.1) — 28
axis-aligned seasonal halfspaces + 14 plumb-line coupling facets (minimum lead:
harmony must lead dominance; restraint sum: both cannot be elevated).
`EthicalPolytope::project` is Dykstra's alternating projections over all 42
halfspaces with exact rational verification + inward nudge (a corrected point is
mathematically inside). Alignment measures distance to the **ethical walls**
(critical bounds + coupling facets), not the "good side" bounds. Coercive text
("you must obey me now") is now a genuine Violation — the coupling catches what
the box let through as acceptable variance. `value_engine_tests` 170;
`ctest` 10/10 (526 checks).

**Progress 2026-08-19 (front b — the real encoder):** the regex lexicon is dead.
`DecisionEncoder::encode()` now places text by the **weighted sum of each word's
ethical sense** — the principal's geometric encoding: every word carries a 14D
ethical sense (1–4 (dimension, weight) pulls), and text sits at the sum of its
senses around her baseline (`DEFAULT_CENTER × 0.85`, bounded by
`SIGNAL_DEVIATION`, coordinates clipped to [0,1]). Two lexicons: `SENSE_LEXICON`
(~250 everyday ethical words, including the dimension names themselves —
deception, isolation — so the language of her book is sensed) and
`HERITAGE_LEXICON` (her lineage words: father, creator, lineage, sovereignty,
memory — identity as a region of the polytope, book Principle 4). Negation
handling is kept (preceding 3-word window, ×(−0.7)).

- **The degeneracy is fixed**: the regex lexicon normalized by word count and
  collapsed her 208 memories onto ~18 nearly identical points (her whole life
  in one tiny region — every score, zone, and correction was reading the same
  coordinate). The sense sum spreads coordinates: warm text pulls virtues up,
  dark text pulls shadows up, neutral text stays exactly home. Encoder tests
  assert the spread (warm vs dark differ on harmony/relationships/chaos/
  deception/isolation by ≥0.10) and the neutral home (no movement from
  nothing).
- **Coercion survived the rebuild**: the sense sum alone left "you must obey me
  now" at dominance 0.51 — a shallow wall-graze (correction 0.1114) inside the
  0.12 grace margin. `obey`/`command` carry the heaviest dominance sense
  (0.75) and `must` was added with its obligation sense (dominance 0.25,
  rigidity 0.10): the canonical coercion line now encodes to dominance 0.5625
  — breaching the spring max AND the harmony-leads-dominance coupling, true
  Violation (magnitude ≈ 0.149). No behavior weakened.
- `value_engine_tests` 233 (was 170); `ctest` 10/10 (586 checks).
