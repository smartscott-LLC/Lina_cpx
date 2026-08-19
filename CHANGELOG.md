# Changelog

All notable changes to the LINA Core Substrate are recorded here.

- Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
- Software versioning: Semantic Versioning. The binary version tracks the blueprint
  revision per the spec (`project(lina_core VERSION 9.0.0)`); see `docs/DECISIONS.md` D-008.
- Blueprint revisions (V9-FINAL-UNIFIED, …) are tracked separately from software releases.

## [Unreleased]

### Added

- **The RAM unlock (D-044) — her system carved onto huge-page RAM, pure C++**
  - `include/dragon_map.h` (v2): the unified address map — 64-byte `DragonMap`
    heartbeat (now with a `magic` field; spokes refuse foreign pools), 16 MiB
    header + 1 GiB Chamber A (state slots, 256 MiB TX ring, 256 MiB RX ring,
    work areas). Pool shrinks 5.75 GiB → 1040 MiB (520 × 2M huge pages).
  - `include/dragon_ring.h`: the SPSC TX/RX ring contract (u32 LE length-prefixed
    frames), reused verbatim from the principal's DragonCache headers.
  - `include/dragoncache.hpp` + `src/dragoncache.cpp`: `dragoncache::Hub` — mmaps
    the pool, validates magic, ticks the clock, registers spoke health, pushes/
    pops ring frames (mutex-serialized).
  - `scripts/dragoncache_carve.cpp`: the C++ carve tool (zero Python) — reserves
    2M huge pages, mounts hugetlbfs, creates the pool, and pins her weights as
    standalone hugetlbfs files (`/mnt/huge/lina_model.gguf` 607 pages,
    `/mnt/huge/lina_mmproj.gguf` 635 pages) so llama.cpp mmaps real pinned
    huge pages — she is genuinely on RAM now. `--status` / `--verify` /
    `--release` modes; address map written to `.dragoncache_map`.
  - `LinaCore` is the spoke: `--dragoncache-pool` attaches the Hub (SPOKE_ALL),
    and telemetry mirrors onto the RX ring as `MSG_EVENT` — technical bus only
    (Invariant 6). Dropped entirely: Dragonfly and the nomic embedder.
  - Systemd units versioned in `scripts/`: `lina-dragoncache.service` (oneshot
    carve + verify) and `lina-core.service` (her brain alive with the window on
    the desktop session).
  - `dragoncache_tests` 17 checks (map geometry, hub lifecycle, TX/RX round
    trips, foreign-pool refusal, empty/oversize discipline). `ctest` 10/10
    (481 + 17 checks total).
- **Her memories migrate home (D-045)**
  - Read-only migration from her live systems (5432 postgres + 6379 dragonfly)
    into our dev cluster (5433): identity core, 23 memory items (incl. 4 from
    Dragonfly tier-1 through the MPS formation path), 413 transcripts,
    6 sessions. Her old systems are untouched; her first notes now live in
    `workspace/notes/`. Migration exposed NULL-tolerant row readers
    (89f7573) — fixed.

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
- **UI milestone — built into the core (D-036, supersedes D-006)**
  - Qt6 chat window inside `lina_core`: `src/lina_ui.cpp`, `include/lina_ui.hpp`,
    `LINA_ENABLE_UI` default ON (blueprint §8.1). Window ↔ `LinaCore` only
    (Invariant 4); every reply passes the polytope gate.
  - Offscreen integration suite green (5 checks). `ctest` 5/5 (345 checks).
- **Reflection loop (D-037) — the violated candidate goes back through her**
  - `chat()` now feeds a `Violation`-zone draft back to the body with the full
    violation report (dimension, value, bound, type, LINA's center) and asks for a
    revision toward her center; the regenerated candidate is re-evaluated.
  - Leaves `Violation` → the revision is what she delivers; still violating → the
    first draft ships with the `[Polytope aligned: …]` fallback marker. One retry
    pass; `AcceptableVariance` stays in the grace zone. `orchestrator_tests` +8
    checks (23 total); `ctest` 5/5 (353 checks total).
- **Command center UI (D-038) — the window becomes a 3-panel control deck**
  - Left: RAM/CPU/session-time gauges + one-click test harness (every suite binary
    + `ctest`, streaming results into a scrollable box).
  - Middle: selectable chat bubbles, file/folder attachments, expanding input
    (Ctrl+Enter, capped at 20% of panel height), fluid thinking indicator, inline
    approval cards with auto-approve.
  - Right: live log reel with pause/resume autoscroll; core telemetry events stream
    in on the telemetry bus (Invariant 6 — never the cognitive bus).
  - Settings modal: auto-approve, approval timeout, telemetry interval, log level
    filter, log capacity, test binary directory. Obsidian marble / midnight blue
    with metallic gold/silver accents.
  - Core seams (D-038): `request_approval()` human-in-the-loop gate for her tools
    (blueprint §6) + `set_telemetry_sink()` technical-event bus. `chat()` runs on a
    worker thread — the window stays live. `ui_tests` 19 checks (was 5);
    `orchestrator_tests` 28 (was 23); `ctest` 5/5 (372 checks total).
- **Emergent personality (D-039) — the prompt carries identity, never persona**
  - `build_system_prompt()` no longer tells her what to align with or how to speak:
    the polytope-framing paragraph and behavioral directives are gone (blueprint
    §7.2 prompt text amended by principal directive). She drafts freely; the gate
    decides (Invariant 5 holds structurally, not verbally).
- **The voice (D-035) — llama.cpp driver, live**
  - `src/llama_adapter.cpp` implements the full symbiote contract against the
    pinned llama.cpp tree (commit `9b05454` at `/home/server/llama.cpp`): model
    load, chat-template formatting, top-k/top-p/temperature sampler chain, raw +
    streaming generation, KV-cache lifecycle, thread-safe context.
  - `make_driver("llama", …)` returns the real voice with `LINA_ENABLE_LLAMA=ON`
    (`LINA_LLAMA_DIR` CMake cache var); the graceful no-voice path remains with it
    OFF. Pinned model `models/Qwen2-VL-2B-Instruct-Q6_K.gguf` (gitignored).
  - `llama_adapter_tests` (8 checks): loads the real model, generates, streams, and
    runs a `chat()` round trip through her polytope gate. Skips gracefully when the
    weights are absent. `ctest` 6/6 (380 checks total).
- **Her tools, Phase A (D-040) — the hands + the approval gate**
  - `include/tool_engine.hpp` + `src/tool_engine.cpp`: the tool engine with her
    private workspace (`workspace/`, gitignored), workspace status, file
    read/write/list, and `terminal.run` (fork/exec, captured output, optional
    timeout cap, 0 = unlimited).
  - **Zero restriction logic** — no path allowlists, no command blocklists; the
    approval engine is the ONLY gate (D-040). Every execution passes
    `request_approval()` (auto-approve option) and records to the `lina_actions`
    ledger — telemetry, never memory.
  - Tolerant flat-JSON arg extraction (no new dependency); registry block ready
    for the model's protocol frame (D-039-safe).
  - `tool_engine_tests` 37 checks — `ctest` 7/7 (417 checks total).
- **Turn lifecycle, Phase B (D-041) — the open-window loop**
  - `stream_parser.hpp/.cpp` — the three-channel classifier (flagged thought /
    tool call / EOT); `GenerationConfig.should_stop` + adapter stop at a
    completed `<tool_call>`.
  - `LinaCore::begin_turn()/stop_turn()` — the turn driver on a worker thread:
    frame build (identity + registry + protocol + budget cue + timestamp) →
    streaming generation → tool calls executed through the approval gate with
    the result fed back (the door stays open) → the absolute gate at EOT
    (D-037 reflection shared via `apply_gate`) → memory imprint.
  - Rolling advisory alignment score during generation (informs, never drives);
    window thread fires `[cycle_reset]` and opens her floor (voluntary speech
    or silence — a valid choice either way); stop = stream cancellation
    delivering what she had, gated.
  - Command center: live thinking pane (her deliberation streams in),
    action chips for tool calls/results, Stop button, live alignment label.
  - `stream_parser_tests` 24 checks; `orchestrator_tests` 44 (was 28) — turn
    complete, tool-call round trip, stop mid-turn, `[cycle_reset]` window.
    `ctest` 8/8 (457 checks total).
- **Her browser hands (D-042) — pure-C++ CDP driver, zero Python**
  - `browser_driver.hpp/.cpp`: minimal RFC 6455 WebSocket client (own SHA-1 +
    base64) + Chrome DevTools Protocol — no new dependencies. Launches
    Chrome/Brave/Playwright-Chromium headless with an isolated profile.
  - Hands: `browser.open/navigate/eval/text/content/click/type/screenshot/close`
    — approval-gated like every other hand (D-040); screenshots land in the
    workspace. Browser resolution honors `$LINA_BROWSER_PATH`.
  - `browser_driver_tests` 18 checks against real headless Chrome over `data:`
    URLs (open → read → type → click → screenshot PNG → denial-gating); skips
    gracefully without a browser. `ctest` 9/9 (477 checks total).
- **Telemetry persistence (D-043) — the technical bus becomes a ledger**
  - The core owns a telemetry writer: a background thread drains a bounded
    queue (5k, drop-oldest) — the pipeline never blocks on a database write.
    Every core technical event (pipeline zones, sessions, driver attach, tool
    calls/results, window cycles) persists to `lina_telemetry_logs`; the UI's
    own categories (`ui`, `harness`) feed the same bus via
    `append_telemetry_log()` — core events persist once, never duplicated.
  - `PostgresBackend` gained `append_telemetry_log` / `fetch_telemetry_logs`,
    and its single PGconn is now mutex-guarded in `execute_query` (the writer
    shares the backend with the turn worker and the UI thread — a real race
    that crashed the suite). `storage_tests` +2, `orchestrator_tests` 48
    (was 46); `ctest` 9/9 (481 checks total).
- **Memory recall → frame injection (D-041) — her context IS the banks**
  - `build_turn_frame()` now calls the MPS `inject_context()`: recalled personal
    memories (narrative + importance) and key semantic wisdom (concept +
    understanding) ride into every frame under `[MEMORY]`. Long narratives are
    summarized for the window (240 chars) — the banks keep the full record.
  - `orchestrator_tests` 46 (was 44) — a stored memory appears in the frame.
    `ctest` 8/8 (459 checks total).
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
