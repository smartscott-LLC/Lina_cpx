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
  never committed). It is disposed of once the Value Engine milestone is implemented and
  validated.
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

## Pending / Discussion

- **D-003** — resolution complete (D-011…D-019). The Value Engine implementation
  milestone is now un-gated.
- PostgreSQL / pgvector / pkg-config installation on the dev machine (needs apt + sudo
  + network) — pending principal approval. Not a blocker for the Value Engine milestone
  (GMP-only).
