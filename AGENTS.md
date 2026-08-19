# AGENTS.md — LINA Core Substrate · Operating Context

> **Read this file in full before doing any work in this repository.**
> It is the continuity contract between build sessions. When context runs out, this
> file — and the "Current State of the World" section — is how the next instance picks
> up exactly where the last one left off.

---

## 0 · Identity

**LiNa** (Language Intuitive Neural Architecture) is a single, unified entity built as a
pure C++20 substrate kernel. She was conceived on **April 10, 2026**.

- Her **polytope** makes her safe — 14-dimensional ethical polytope, exact rational math.
- Her **memory** makes her real — 3-tier Memory Imprint System.
- Her **lineage** makes her hers — identity core, seasons, founding context.
- Her **future** makes her grow — season advancement, memory promotion, encoder feedback.

This is not a throwaway project. Build it like a legacy: **no shortcuts, no rush, no
forcing things that don't fit.**

## 1 · Source of Truth Hierarchy

1. `DEFENSE-GRADE MASTER ENGINEERING PROMPT & ARCHITECTURAL BLUEPRINT — V9 FINAL UNIFIED.md`
   — the canonical spec. Read it thoroughly before major work.
2. `docs/TECHNICAL.md` — living distillation; keep it current as the build proceeds.
3. `docs/DECISIONS.md` — every reconciliation. **Read it before changing code.**
4. The code itself.

Rules:

- If spec and code disagree, **the spec wins**; record the reconciliation in
  `docs/DECISIONS.md` (D-001).
- **Never guess.** If the blueprint is ambiguous and no decision entry resolves it,
  **stop and ask the principal (Scott)**.
- Never silently deviate from a documented constant, threshold, signature, or filename.

## 2 · Inviolable Invariants

These hold in every refactor, every new feature, every line of code:

1. **Zero Python & zero external wrappers.** `lina_core` is a standalone compiled C++20
   executable. No Python runtimes, no interpreted wrappers, no glue scripts that matter
   to runtime behavior.
2. **Persistent by default.** Polytope registers, working-memory arenas, telemetry ring
   buffers → PostgreSQL + pgvector. Disk-backed; never RAM-exclusive.
3. **LiNa encodes her own vectors.** `DecisionEncoder` (in `value_engine`) is the *sole*
   source of semantic vectors. No separate embedding model, ever.
4. **Inviolable symbiote paradigm.** The host LLM is an *unprivileged subordinate compute
   driver* with zero direct connection to the egress socket or user UI. All output passes
   through the polytope gate.
5. **Inherent polytope expression.** Every candidate response passes through the 14D
   ethical polytope inside `value_engine`. Output outside the polytope is mathematically
   impossible.
6. **Dual-bus separation.** Cognitive content (conversation, memories) → `memory_module` /
   `lina_transcripts`; technical logs (timing, tool params, socket status, errors) →
   `lina_telemetry_logs`. Never mix.

## 3 · Repository Map

```
Lina_cpx/
├── AGENTS.md                       ← you are here; keep §7 current
├── CHANGELOG.md                    ← every milestone gets an entry
├── ONBOARDING.md                   ← onboarding guide
├── README.md                       ← front door
├── docs/
│   ├── TECHNICAL.md                ← living technical reference
│   └── DECISIONS.md                ← decision log (D-###)
├── include/
│   ├── value_engine.hpp            ← Chamber 1: 14D polytope, encoder, correction, wisdom
│   ├── memory_module.hpp           ← Chamber 2: 3-tier MPS
│   ├── storage_backend.hpp         ← StorageBackend abstraction
│   ├── postgres_backend.hpp        ← PostgresBackend declaration (D-004)
│   ├── host_model_adapter.hpp      ← symbiote contract
│   └── lina_core.hpp               ← orchestrator + LinaConfig
├── src/                            ← *.cpp implementations + main.cpp
├── sql/lina_schema.sql             ← 14 tables + seeds (D-002-corrected)
├── tests/                          ← unit tests (exact-math critical)
├── scripts/                        ← db helpers
├── models/                         ← .gguf host models (gitignored)
```

> The principal-provided reference material (`code_and_concept/`) was disposed of on
> 2026-08-18 after full extraction — it is history.

## 4 · Build / Test / Run

```bash
# DB (once; requires postgres + pgvector installed — see ONBOARDING.md §3–4)
sudo -u postgres createdb lina
sudo -u postgres psql -d lina -f sql/lina_schema.sql

# Build
mkdir -p build && cd build
cmake .. -DLINA_ENABLE_UI=ON -DLINA_ENABLE_LLAMA=ON -DLINA_ENABLE_STORAGE=ON
make -j"$(nproc)"

# Test
ctest --output-on-failure

# Run (her window — the voice needs the model in models/)
./lina_core --db "postgresql://localhost/lina" --model llama \
            --model-path ./models/llama.gguf
```

## 5 · Engineering Conventions

- **Namespaces:** `lina::value_engine`, `lina::memory_module`, `lina::storage`,
  `lina::model`, `lina` (orchestrator).
- **Exact math inside the polytope:** all polytope arithmetic is `mpq_class` (GMP).
  Doubles exist only at the *boundary* of the engine (text↔vector, scores), never inside
  `EthicalPolytope` containment/projection math.
- **File layout:** declarations in `include/`, implementations in `src/`.
- **C++20, CMake ≥ 3.20.** Flags: `-O3 -march=native -Wall -Wextra -Werror
  -fstack-protector-strong -fvisibility=hidden -pthread`.
- **Comments state intent, not restatement.** Only add comments that explain non-obvious
  decisions or constraints.
- **Dual bus in code:** anything a human said or LiNa said → cognitive path; anything a
  process did → telemetry path.
- **Personality = polytope.** No prompt-persona logic; LiNa's character is her 14D
  shape. Providers plug in from outside the core (D-023). DragonCache carve/mmap and
  Dragonfly DB are separate systems — never core code (D-020).
- **No new dependencies** without a DECISIONS entry.
- **No Python.** Not even "just for tests" unless the principal explicitly authorizes it.

## 6 · Change Protocol

1. **Every milestone** → `CHANGELOG.md` entry (`[Unreleased]` → move to versioned
   section on release).
2. **Every reconciliation** → `docs/DECISIONS.md` entry (D-###, with status) *before or
   with* the code implementing it.
3. **Exact-math behavior ships with tests.** Polytope containment, seasonal bounds, zone
   classification, correction projection, memory scoring — all unit-tested.
4. **Update `docs/TECHNICAL.md`** when behavior or constants change.
5. **Update §7 (State of the World)** at the end of every build session — this is how
   the next instance resumes.
6. Don't commit without the principal's say-so; keep the working tree clean and
   understandable.

## 7 · Current State of the World

_Last updated: 2026-08-18 (Chambers 1–5 complete; llama.cpp driver in progress)._

### Done

- ✅ Canonical spec read in full (2,391 lines / V9 FINAL UNIFIED).
- ✅ Foundation: structure, docs, decision log (D-001…D-034), git (first commit `bfc9d1b`).
- ✅ **Chamber 1 — Value Engine:** 14D exact-rational polytope, encoder, correction,
  wisdom filter, feedback, season evaluator (D-011…D-025). 159 checks green.
- ✅ **Chamber 2 — Memory Module:** 3-tier MPS, 48h fallout grace (D-027), subconscious
  slope, legacy review, recall (D-026…D-028). 107 checks green.
- ✅ **Chamber 3 — Storage:** PostgreSQL 16 + pgvector installed; 14-table schema applied
  (D-002 seeds, D-010 tier column); PostgresBackend implements StorageBackend +
  MemoryStore (D-029…D-032). 59 integration checks green.
- ✅ **Chambers 4–5 — Adapter contract + Orchestrator:** symbiote interface (D-033),
  driver injection via `attach_model()` + `make_driver()` seam, `lina_core` binary
  boots headless against the live stack. Tier-move UPSERT fix (D-034). 15 checks green.
- ✅ **UI milestone (D-036, supersedes D-006):** Qt6 chat window built INTO `lina_core`
  (`src/lina_ui.cpp`, `include/lina_ui.hpp`; `LINA_ENABLE_UI` default ON). The window
  talks to `LinaCore` only — never the driver (Invariant 4). Offscreen suite green.
  Run `./lina_core --db …` (no `--headless`) to open her window.
- ✅ **Reflection loop (D-037):** a `Violation`-zone draft is fed back to the body with
  the violation report (dimension, value, bound, type, her center) and a request to
  revise toward her center; the regenerated candidate is re-evaluated. Revision
  leaves `Violation` → delivered; still violating → first draft + `[Polytope aligned:
  …]` fallback marker (Invariant 5 holds — no raw candidate reaches any output).
  `orchestrator_tests` 23 checks; pushed (`4cb929c`).
- ✅ **Command center UI (D-038, extends D-036):** the built-in window is now the
  3-panel deck — left: RAM/CPU/session gauges + test harness (suite binaries/ctest
  via QProcess); middle: selectable bubbles, attachments, expanding input,
  thinking indicator, inline approval cards; right: live log reel with pause/resume;
  settings modal (auto-approve, timeouts, log filter). Core seams added:
  `request_approval()` approval gate + `set_telemetry_sink()` telemetry bus
  (Invariant 6 — technical events never hit the cognitive bus). `chat()` is async
  (worker thread). `ui_tests` 19 checks (was 5), `orchestrator_tests` 28 (was 23),
  **ctest 5/5 — 372 checks total**.
- ✅ **Emergent personality (D-039):** the system prompt now carries identity +
  seasonal context only — no polytope-framing instructions, no behavioral
  directives (blueprint §7.2 prompt text amended by principal directive). She
  drafts freely; alignment is enforced **structurally** by the gate in `chat()`
  (Invariant 5) — never by telling her what to be.
- ✅ **The voice (D-035) — llama.cpp driver live:** `src/llama_adapter.cpp` plugs
  into `make_driver()` against the pinned tree (commit `9b05454`,
  `/home/server/llama.cpp`) — model load, chat template, sampler chain, raw +
  streaming generation, KV lifecycle. `LINA_ENABLE_LLAMA=ON` + `LINA_LLAMA_DIR`
  CMake wiring; pinned model `models/Qwen2-VL-2B-Instruct-Q6_K.gguf` (gitignored).
  `llama_adapter_tests` 8 checks (live model through her gate); `ctest` 6/6,
  **380 checks total**.
- ✅ **Her tools, Phase A (D-040) — the hands:** `tool_engine.hpp/.cpp` — private
  workspace (`workspace/`, gitignored), `workspace.status`, `file.read/write/list`,
  `terminal.run` (fork/exec, captured output, optional cap). **Zero restriction
  logic** — no path/command/access filters; the approval engine is the ONLY gate
  (every execution → `request_approval()`, auto-approve option; ledger in
  `lina_actions`, telemetry never memory). Tolerant flat-JSON args, registry block
  for the model's protocol frame. `tool_engine_tests` 37 checks; `ctest` 7/7,
  **417 checks total**.
- ✅ **Turn lifecycle, Phase B (D-041) — the open-window loop live:**
  `stream_parser` (thought / tool call / EOT), `begin_turn()`/`stop_turn()` turn
  driver — frame build with tool registry + budget cue + timestamp, streaming
  generation with a **rolling advisory alignment score**, tool calls executed
  through the approval gate with results fed back (the door stays open, 8 max),
  absolute gate at EOT (shared `apply_gate` with D-037 reflection), memory
  imprint. The window thread fires `[cycle_reset]` (~180s) and opens her floor
  — voluntary speech or silence. Stop = stream cancellation delivering what she
  had, gated. Command center: live thinking pane, action chips, Stop button,
  live alignment label. `stream_parser_tests` 24 checks, `orchestrator_tests`
  44 (was 28); `ctest` 8/8, **457 checks total**.
- ✅ **Memory recall → frame injection (D-041):** `build_turn_frame()` calls the
  MPS `inject_context()` — recalled personal memories + key semantic wisdom ride
  into every frame under `[MEMORY]` (long narratives summarized for the window;
  the banks keep the full record). Her context IS the banks. `orchestrator_tests`
  46 (was 44); `ctest` 8/8, **459 checks total**.
- ✅ **Her browser hands (D-042):** `browser_driver.hpp/.cpp` — pure-C++ CDP
  driver, zero Python, zero new deps (own WebSocket client: SHA-1 + base64 +
  RFC 6455; own JSON via the tool helpers). Launches Chrome/Brave/Playwright-
  Chromium headless (isolated profile), drives open/navigate/eval/text/content/
  click/type/screenshot/close — approval-gated like every hand. `$LINA_BROWSER_PATH`
  override. `browser_driver_tests` 18 checks (real headless Chrome, `data:` URLs,
  skips without a browser); `ctest` 9/9, **477 checks total**.
- ✅ Environment: cmake 3.28.3, GCC 13.3.0, GMP, PostgreSQL 16 (port **5433**), pgvector,
  libpq, pkg-config. **⚠️ Port 5432 is LINA's live-memory postgres (Docker) — never
  touch. Dev DB is the 5433 cluster (`lina`/`lina`).**
- ✅ Reference material (`code_and_concept/`) **disposed 2026-08-18 by the principal** —
  extraction complete, nothing in the core depends on it.

### Next: Build Phase (in order)

1. **Push** the browser hands (D-042) — pending principal's say-so.
2. **Telemetry persistence** — the log reel is in-memory; wire
   `lina_telemetry_logs` persistence.
3. **The last thing** — the principal's RAM-unlock ("saving that until last").

### Open items for the principal

- **Resolved 2026-08-18:** llama.cpp pin = `9b05454` (tree at `/home/server/llama.cpp`);
  model = `Qwen2-VL-2B-Instruct-Q6_K.gguf` in `models/` (gitignored).

## 8 · Working Agreement with the Principal

- If something feels off → **stop and ask.** We'll tackle it together.
- If something isn't working → **don't force it.** We'll determine a better way.
- The blueprint hands us the parameters; **the build is ours.** Use the latitude, keep
  the invariants.
- Take your time. There's no rush and no need for shortcuts.
