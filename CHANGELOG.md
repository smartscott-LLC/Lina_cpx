# Changelog

All notable changes to the LINA Core Substrate are recorded here.

- Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
- Software versioning: Semantic Versioning. The binary version tracks the blueprint
  revision per the spec (`project(lina_core VERSION 9.0.0)`); see `docs/DECISIONS.md` D-008.
- Blueprint revisions (V9-FINAL-UNIFIED, …) are tracked separately from software releases.

## [Unreleased]

### Added

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
- Open decision awaiting principal input: `docs/DECISIONS.md` **D-003** (blueprint references
  "your existing code" for several function bodies; no such code exists in-repo).
