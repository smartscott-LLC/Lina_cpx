# Changelog

All notable changes to the LINA Core Substrate are recorded here.

- Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
- Software versioning: Semantic Versioning. The binary version tracks the blueprint
  revision per the spec (`project(lina_core VERSION 9.0.0)`); see `docs/DECISIONS.md` D-008.
- Blueprint revisions (V9-FINAL-UNIFIED, …) are tracked separately from software releases.

## [Unreleased]

### Added

- **Value Engine milestone (Chamber 1) — complete**
  - `code_and_concept/` reference material fully read and extracted (D-011…D-019).
  - `include/value_engine.hpp` + `src/value_engine.cpp` authored (exact rational
    polytope, encoder, correction, wisdom filter, feedback, season evaluator).
  - Boundary-rounding fix (D-024): projection lands strictly inside the polytope,
    honoring Invariant 5. Reference bug in `confirm_correction` fixed (D-017).
  - Exact-math unit tests (159 checks) — `ctest` 100% green.
  - Clarifications recorded: DragonCache/Dragonfly are separate systems (D-020);
    no provider/prompt/persona logic in the core (D-023).
- **Memory Module milestone (Chamber 2) — complete**
  - `include/memory_module.hpp` + `src/memory_module.cpp` authored (3-tier MPS:
    formation, routing, sweep, 48h fallout grace, monthly maintenance, subconscious
    slope, legacy review, recall, context injection).
  - D-027 fix: fallout buffer now enforces the documented 48-hour second chance
    (the reference reprocessed immediately).
  - Test doubles live in `tests/` only (D-022); unit suite (107 checks) green.
- **Storage milestone (Chamber 3) — complete**
  - PostgreSQL 16 + pgvector + libpq + pkg-config installed (apt, dev machine).
  - `sql/lina_schema.sql` — blueprint's 14 tables + pgvector index, D-002
    corrected seeds, D-010 tier column — applied and verified (14 tables).
  - `storage_backend.hpp`, `postgres_backend.hpp`, `postgres_backend.cpp` —
    D-004 header, D-005/D-031 dual interface, D-030 fixes (dynamic params,
    explicit columns, NULLIF optionals), D-032 fix (pgvector `[…]` format).
  - Integration suite green (59 checks) — identity, memory round-trip, tier ops,
    `<->` vector search, transcripts, sessions, actions, promotion log,
    MemoryModule-over-Postgres end-to-end.
- **Host Model Adapter + Orchestrator milestones (Chambers 4–5) — complete**
  - `host_model_adapter.hpp` — blueprint §5 symbiote contract (interface + adapter
    declarations). Providers plug in via the `make_driver()` seam (D-033); the core
    ships no provider (D-023).
  - `lina_core.hpp/.cpp` — the orchestrator: identity → polytope → MPS → driver
    injection; chat pipeline gates every candidate through her polytope (Invariant 5).
  - `main.cpp` — blueprint §7.3 CLI; `model_driver.cpp` — the plug-in seam.
  - D-034 fix: tier moves are UPSERTs on the unified table (global `item_id` PK).
  - `lina_core` binary boots headless against the live stack; orchestrator suite
    green (15 checks). `ctest` 4/4 (325 checks total).
- **Project foundation**
  - `README.md` — project identity, pillars, invariants, quick links.
  - `ONBOARDING.md` — official onboarding guide (reading order, prerequisites, DB setup, build, run, working agreements).
  - `AGENTS.md` — operating context & continuity contract for future build instances, including live "Current State of the World".
  - `docs/TECHNICAL.md` — living technical reference distilled from the V9 blueprint (dimensions, polytope math, MPS lifecycle, storage model, symbiote contract, build reference).
  - `docs/DECISIONS.md` — decision log reconciling spec ambiguities with implementation choices.
  - `CHANGELOG.md` — this file.
  - Directory skeleton: `include/`, `src/`, `sql/`, `tests/`, `scripts/`, `models/`.
  - `.gitignore` — build artifacts, model files, environment secrets.

### Notes

- Foundation phase complete. Build phase (Value Engine first) begins next — see `AGENTS.md` §7.
- Git repository initialized by the principal; first commit pushed (`bfc9d1b`, `main`).
- `docs/DECISIONS.md` **D-003** resolution path chosen: principal-provided reference
  material (book proofs + original C++ code) with **math-only extraction**; the material
  lives in `code_and_concept/` (gitignored, disposable). Value Engine authorship in
  progress.
