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
