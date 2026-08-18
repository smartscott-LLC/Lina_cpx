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

- **D-003** — resolution complete (D-011…D-019). The Value Engine implementation
  milestone is un-gated.
- llama.cpp driver (the voice) — queued; builds into `make_driver()` (D-035).
- Qt6 UI now built into the core (D-036); headless mode remains.
