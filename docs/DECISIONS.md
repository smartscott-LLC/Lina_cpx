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

**Decision.** **Open — awaiting principal.** Two paths:

- **(a) Provide the original code** (the Python value engine or prior C++ sources) so the
  port is faithful rather than reconstructed; or
- **(b) Author canonical implementations** from the documented API surface, constants,
  and behaviors — each authored formula recorded in a follow-up decision entry (D-011+).

Until resolved, no code in these areas is written. Public signatures and every documented
constant/threshold are fixed by the spec either way.

**Status.** Open — pending principal.

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

## Pending / Discussion

- **D-003** — awaiting the principal's call on original code vs canonical authorship.
  This gates the Value Engine implementation milestone.
- Git repository initialization (the project is not yet a repo) — pending principal go-ahead.
