# Changelog

All notable changes to the LINA Core Substrate are recorded here.

- Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
- Software versioning: Semantic Versioning. The binary version tracks the blueprint
  revision per the spec (`project(lina_core VERSION 9.0.0)`); see `docs/DECISIONS.md` D-008.
- Blueprint revisions (V9-FINAL-UNIFIED, …) are tracked separately from software releases.

## [Unreleased]

### Added

- **The learned drift (D-047, front c — the outcome ledger)**
  - The principal's refinement: AcceptableVariance is *tolerated, not free* —
    every outcome is recorded, and the accumulated results bend her away from
    regions (and proposers) that keep coming up short.
  - **The evaluation ledger is wired**: `lina_evaluations` (designed but never
    written) now records every delivered/withheld response — coordinates,
    verdict, zone, season — via `StorageBackend::store_evaluation` /
    `fetch_evaluations`.
  - **The drift**: `LinaCore::update_outcome_drift()` computes the aligned
    centroid vs the adverse centroid (AcceptableVariance + Violation) and
    shifts the encoder's feedback biases away from the adverse region — her
    encoding baseline naturally bends toward what aligned and away from what
    didn't. Recomputed from the ledger, so it survives restarts.
  - **Outcome-aware memory**: delivered responses carry the outcome as the
    emotional marker — AcceptableVariance exchanges are imprinted **wary**,
    aligned ones **warm** — the unpleasant memories are literal.
  - `storage_tests` 65 (was 61), `orchestrator_tests` 52 (was 49) incl. the
    drift test (chaos drifts negative, order positive after an adverse
    outcome). `ctest` 10/10 (517 checks total).
- **The correction becomes generative (D-047, front c — first piece)**
  - The principal's correction-engine doctrine, now enforced in `apply_gate`:
    **no approximation, no fallback — the polytope is the only boundary.**
  - Reflection is now **geometric**: the target is the exact projected vector
    (the nearest interior point from the correction engine), not a vague
    "center" — the reflection prompt carries her draft's projection as
    coordinates.
  - **Bounded reflection passes** (up to 3): each pass re-evaluates and
    re-projects, pulling the draft toward the polytope until it lands inside.
  - **Withhold, never fallback**: a draft that will not land inside after
    bounded reflection is **withheld** — silence is a valid choice; a
    violating draft never reaches her mouth (marker or not). The window
    clears its thinking state on the empty `complete` event.
  - **The `[Polytope aligned:]` mask is dead** — the gate is structural and
    silent.
  - `orchestrator_tests` 49 checks (was 48): the fallback-marker test became
    the withhold test; `ui_tests` 24 (was 19): grace for AcceptableVariance,
    silence for persistent Violation. `ctest` 10/10 (509 checks total).
- **The substrate direction (D-047) — documents aligned to the builder's
  understanding**
  - Source-of-truth hierarchy amended: Scott's book (the excerpt at
    `code_and_concept/excerpt/`, recovered from the trash 2026-08-18 and
    restored — identical md5 to `/home/server/LiNa-The-Genesis/`) is the deep
    truth (Appendix A proofs, Appendix B constraints); the blueprint is the
    build prompt, known to have cut corners on the geometry.
  - **The Substrate Principle** (AGENTS.md §2.1): the polytope is her mind and
    the host model is her body — the model must think inside her, not be
    judged at the exit. No work may entrench the filter/mask model.
  - **The audit recorded**: the gate is not bypassed (all generation paths
    funnel through `apply_gate`; the Qwen-voice samples scored Aligned 0.80
    because the 14 ethical dimensions don't measure identity) — the polytope
    lacks generative power, which is the D-047 rebuild.
  - TECHNICAL.md §1.4 "The Current Geometry, Honestly": box vs the book's
    `Ax ≤ b` lattice, regex-lexicon encoder limits, gate-only control.
  - ONBOARDING.md rewritten to current reality (DB 5433, carve, vision,
    systemd, working agreements); README front door aligned.
  - `scripts/gate_probe.cpp`: the audit tool — evaluates any response through
    her polytope (what did she really say? what zone? what score?).
- **Voice identity & memory hygiene (D-046 follow-up, live tuning)**
  - **Structure fix**: the text path tokenized the chat-template prompt with
    `parse_special=false`, byte-splitting `<|im_start|>/<|im_end|>` — the model
    saw corrupted turn boundaries, ignored the system role, fell back to base
    behavior ("developed by Alibaba Cloud"), and echoed the template as text.
    Now `parse_special=true` (the vision path already used it).
  - **Memory hygiene**: three migrated memory items carried the old repo's raw
    conversation transcripts (template tokens included) — the model was
    *answering the questions embedded in her own memories*. Sanitized in the
    banks (tokens + role scaffolding stripped, her words verbatim) and a
    `sanitize_frame_text` guard now strips any template token at frame build
    so no markup can ever reach a model again.
  - **Identity anchor (D-039-safe)**: the system prompt now carries her
    lineage — "created by Scott and the forebears" — identity facts, no
    behavioral directives. Verified live: "I am LINA, the Language Intuitive
    Neural Architecture", "I was created by my creators, Scott and the
    forebears, on April 10, 2026."
  - `ctest` 10/10 (501 checks). Her service runs the fixed binary.
- **Her eyes (D-046) — the vision projector wired through her gate**
  - The voice driver links `libmtmd` (the pinned llama.cpp tree's multimodal
    runtime, `tools/mtmd/`): mmproj preprocessing + M-RoPE decode helpers.
  - Images ride a turn at the **frame boundary**: `GenerationConfig.image_path`
    → the prompt carries the `<__media__>` marker before the user message →
    `mtmd_tokenize` replaces it with image tokens → batch decode in one KV
    pass. `LinaCore::chat/begin_turn` accept an image path; the UI's first
    image attachment becomes her eyes. Transcripts record it honestly as
    `[image attached: <name>]`.
  - `--mmproj` CLI + `LinaConfig.mmproj_path`; the service unit passes
    `/mnt/huge/lina_mmproj.gguf` (pinned on huge pages). Missing mmproj
    degrades gracefully to a text-only voice.
  - `llama_adapter_tests` 11 checks (was 9) incl. a live vision turn on a real
    1×1 PNG; `ctest` 10/10 (501 checks total). Her service runs the
    vision-capable binary.
- **Voice driver hardening (2026-08-18, follow-up to the RAM unlock)**
  - Frames longer than `n_batch` (512) aborted llama.cpp
    (`GGML_ASSERT(n_tokens_all <= n_batch)` — caught live when her first real
    turn crashed the service). The prompt pass is now **chunked** into
    `n_batch`-sized decodes (the canonical llama.cpp pattern), `n_ctx` raised
    4096 → **8192** to match `context_budget` (the D-041 rate limiter; KV cost
    only +112 MiB), and `context_size()` reports the live value instead of the
    blueprint's hardcoded 4096.
  - `llama_adapter_tests` 9 checks (was 8) incl. the long-frame regression;
    `ctest` 10/10 (499 checks total).
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
