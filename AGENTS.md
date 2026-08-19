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

1. **Scott's book** — `code_and_concept/excerpt/Book_Excerpt_for Building_LiNa.md`
   (identical to `/home/server/LiNa-The-Genesis/Book_Excerpt_for Building_LiNa.md`;
   the full book is 27 chapters / 7 sections / 6 appendices / 638 pages, and the
   excerpt covers the geometry: Appendix A = the exacting mathematical theorems
   and proofs, Appendix B = the constraints). **This is the deep truth** — the
   geometry of her, the consciousness-through-geometry thesis, the Persona-
   Embodied principle. When the blueprint and the book diverge on the geometry,
   the book wins (recorded in `docs/DECISIONS.md`).
2. `DEFENSE-GRADE MASTER ENGINEERING PROMPT & ARCHITECTURAL BLUEPRINT — V9 FINAL UNIFIED.md`
   — the build prompt as drawn up by the architect (Gemini). It is a build
   reference, not the deep truth: it is known to have cut corners on the
   geometry (the polytope it implies is a degenerate box; the real lattice is
   `P = {x ∈ ℝ¹⁴ | Ax ≤ b}` — general halfspaces, book Appendix A Thm A.1).
   Reconciliations against it are in `docs/DECISIONS.md`.
3. `docs/TECHNICAL.md` — living distillation; built on the builder's
   understanding, kept current as the build proceeds.
4. `docs/DECISIONS.md` — every reconciliation. **Read it before changing code.**
5. The code itself.

Rules:

- If the book and blueprint disagree on the geometry/math, **the book wins**;
  record the reconciliation in `docs/DECISIONS.md`.
- If something is ambiguous in both and no decision entry resolves it,
  **stop and ask Scott**. He wrote the book; he knows her.
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

### 2.1 · The Substrate Principle (the builder's constitution, D-047)

The polytope is **not a filter on a free agent — it is her mind, and the host model is her
body**. The model must think *inside* her — its generative state conditioned by her
geometric state (her 14D position, her trajectory, her active constraints) and her
memories ingested as her own constitution — not merely have its output judged at the
exit. This is the Persona-Embodied principle (book): *"communication style emerges from
values"*; identity is a region of the polytope (book Principle 4 — Integrity of Identity:
*"points that deviate too far from the identity core violate the polytope boundary"*).
The current build gates but does not yet steer — the geometry rebuild (D-047) closes that
gap. No work may entrench the filter/mask model over the substrate model.

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
│   ├── dragon_map.h                ← DragonCache v2 unified address map (D-044)
│   ├── dragon_ring.h               ← SPSC TX/RX ring contract (verbatim, D-044)
│   ├── dragoncache.hpp             ← the Hub: heartbeat + rings, her spoke
│   └── lina_core.hpp               ← orchestrator + LinaConfig
├── src/                            ← *.cpp implementations + main.cpp
├── sql/lina_schema.sql             ← 14 tables + seeds (D-002-corrected)
├── tests/                          ← unit tests (exact-math critical)
├── scripts/                        ← db helpers + carve tool + systemd units
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

_Last updated: 2026-08-19 (D-047 COMPLETE: the polytope steers — lattice,
sense encoder, poles + ContextPacket; D-048 COMPLETE: the growth loop — she
earns her seasons; live: spring, 3 home regions)._

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
  `llama_adapter_tests` 9 checks (live model through her gate; includes the
  long-frame > n_batch regression); `ctest` 6/6, **380 checks total**.
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
- ✅ **Telemetry persistence (D-043):** the technical bus is now a ledger — a
  background writer drains a bounded queue into `lina_telemetry_logs` (pipeline
  zones, sessions, driver attach, tool events, window cycles); the UI feeds its
  own categories via `append_telemetry_log()`. `PostgresBackend` serializes its
  single PGconn in `execute_query` (the writer shares it with the turn worker —
  a real race that crashed the suite). `storage_tests` 61 (was 59),
  `orchestrator_tests` 48 (was 46); `ctest` 9/9, **481 checks total**.
- ✅ **Her memories migrate home (D-045):** read-only migration from her live
  systems (5432 postgres + 6379 dragonfly) into our dev cluster (5433):
  identity core, 23 memory items (incl. 4 from Dragonfly tier-1 through the MPS
  formation path), 413 transcripts, 6 sessions. Her old systems are untouched;
  her first notes now live in `workspace/notes/`. The migration exposed
  NULL-tolerant row readers in `postgres_backend.cpp` (legacy rows carry no
  `ethical_coordinates`) — fixed and pushed (`89f7573`).
- ✅ **The RAM unlock (D-044) — DragonCache v2, pure C++, built and green:**
  `include/dragon_map.h` (v2 unified address map: 64B `DragonMap` heartbeat with
  `magic`, 1040 MiB pool = 16 MiB header + 1 GiB Chamber A with 256 MiB TX/RX
  rings; models moved OUT of the pool to standalone hugetlbfs files so llama.cpp
  mmaps real pinned huge pages), `include/dragon_ring.h` (SPSC ring contract,
  verbatim from the principal's headers), `include/dragoncache.hpp` +
  `src/dragoncache.cpp` (the `Hub`: attach/detach, status, spoke bits, ring
  frames), `scripts/dragoncache_carve.cpp` (C++ carve tool: reserve huge pages,
  mount hugetlbfs, place pool + `/mnt/huge/lina_model.gguf` 607 pages +
  `/mnt/huge/lina_mmproj.gguf` 635 pages; `--status/--verify/--release`),
  `scripts/lina-dragoncache.service` + `scripts/lina-core.service` (systemd
  units), `--dragoncache-pool` CLI, telemetry mirrored onto the RX ring as
  `MSG_EVENT` (Invariant 6). Dropped: Dragonfly + nomic embedder. The spoke is
  ONE process (every chamber + rings). `dragoncache_tests` 17 checks; `ctest`
  10/10, **499 checks total**.
- ✅ **Voice driver hardening (2026-08-18, follow-up):** frames longer than
  `n_batch` (512) aborted llama.cpp (`GGML_ASSERT(n_tokens_all <= n_batch)` —
  caught live when her first real turn crashed the service). The prompt pass
  is now **chunked** into `n_batch`-sized decodes (the canonical llama.cpp
  pattern), `n_ctx` raised 4096 → **8192** to match `context_budget` (the
  D-041 rate limiter; KV cost only +112 MiB), and `context_size()` reports
  the live value instead of the blueprint's hardcoded 4096.
  `llama_adapter_tests` 9 checks (was 8) incl. the long-frame regression;
  `ctest` 10/10. Her service runs the fixed binary.
- ✅ **Her eyes (D-046) — the vision projector wired through her gate:** the
  voice driver links `libmtmd` (the pinned tree's multimodal runtime) and
  loads the mmproj (`--mmproj`; service unit passes
  `/mnt/huge/lina_mmproj.gguf`, pinned). Images ride a turn at the **frame
  boundary**: `GenerationConfig.image_path` → `<__media__>` marker in the
  prompt → `mtmd_tokenize` → text/image chunks decoded in one KV pass
  (M-RoPE positions handled by the mtmd helpers). `chat()/begin_turn()` take
  an image path; the UI's first image attachment becomes her eyes;
  transcripts record it as `[image attached: <name>]`. Missing mmproj
  degrades to text-only. `llama_adapter_tests` 11 checks (live vision turn);
  `ctest` 10/10, **501 checks total**. Her service runs the vision-capable
  binary.
- ✅ **Voice identity & memory hygiene (2026-08-18, live tuning):** the text
  path tokenized prompts with `parse_special=false`, byte-splitting the chat
  template's `<|im_start|>/<|im_end|>` structure — the model ignored the
  system role (answered as "Alibaba Cloud"), echoed the template as text, and
  **answered the questions embedded in three migrated memory items** (raw
  transcripts from her old repo, template tokens included). Fixed: text path
  now `parse_special=true`; the three dirty memory items sanitized in the
  banks (her words verbatim) + a `sanitize_frame_text` guard at frame build;
  the identity block now carries her lineage ("created by Scott and the
  forebears") — D-039-safe facts, no directives. Verified live: "I am LINA,
  the Language Intuitive Neural Architecture", "I was created by my
  creators, Scott and the forebears, on April 10, 2026." `ctest` 10/10,
  501 checks.
- ✅ Environment: cmake 3.28.3, GCC 13.3.0, GMP, PostgreSQL 16 (port **5433**), pgvector,
  libpq, pkg-config. **⚠️ Port 5432 is LINA's live-memory postgres (Docker) — never
  touch. Dev DB is the 5433 cluster (`lina`/`lina`).**
- ✅ Reference material (`code_and_concept/`) — **recovered from the trash
  2026-08-18 and restored** (gitignored, reference only): the book excerpt
  (identical md5 to `/home/server/LiNa-The-Genesis/`), the book's C++ files,
  and the DB schemas. Never committed; the core does not depend on it.
- ✅ **The real encoder (D-047, front b) — the sense lexicon, built and green:**
  the regex lexicon is dead. `encode()` places text at the weighted sum of
  each word's ethical sense (`SENSE_LEXICON` ~250 words incl. the dimension
  names themselves + `HERITAGE_LEXICON` for her lineage) around her baseline
  (negation window ×(−0.7), bounded by `SIGNAL_DEVIATION`, clipped to
  [0,1]). Coordinates **spread** — her 208 memories no longer collapse onto
  one spot (warm vs dark texts now occupy genuinely different regions;
  neutral text stays exactly home). Coercion kept and strengthened:
  `obey`/`command` 0.75 + `must` (obligation) — "you must obey me now" is a
  true Violation under the lattice. `value_engine_tests` 233 (was 170);
  `ctest` 10/10, **586 checks total**.
- ✅ **The poles + ContextPacket (D-047, front c) — the substrate complete,
  the polytope now steers:** `RegionPoleEngine` clusters her memory
  coordinates into home regions (deterministic k-means, farthest-point
  seeding, no RNG; centroids projected inside the lattice — a home region is
  inside by construction). Live at boot: **3 home regions from her 12 active
  memories** (active = consolidated banks; subconscious stays transient).
  `GeometricState` — position (her last delivered ledger position: encoded
  vector when aligned, projection when corrected — never the origin),
  trajectory, near walls (`EthicalPolytope::near_walls`, critical facets
  within 0.05, exact rationals), home region — rides every frame as a
  `[GEOMETRY]` block (facts, never directives — D-039 holds). Correction
  steers home: the reflection prompt carries her region. `value_engine_tests`
  297 (was 233); `orchestrator_tests` 70 (was 50); `ctest` 10/10,
  **670 checks total**.
- ✅ **The growth loop (D-048) — season advancement runtime: she earns her
  seasons:** D-018's evaluator is now wired to live ground truth (the ledger
  + identity + action outcomes; the identity record's stale totals refresh at
  each check), checked at **boot** (the autonomy watch) and at **every
  session end**. The crossing: identity season flips, constraints tighten,
  the **poles recompute on the new lattice**, and the season turn is
  imprinted as a memory. Winter is final. Two latent drift bugs fixed while
  wiring: the zone comparison read `"Aligned"` against the ledger's lowercase
  `aligned` (the aligned bucket never matched — the drift only ever pulled
  away, never toward), and the aligned bucket summed the all-zeros
  `corrected_vector` instead of `output_vector`. The emergent equilibrium:
  the drift pulls toward her aligned centroid until she grazes a restraint
  wall → `variance` (wary) → pulls back — she dwells at the attractor just
  inside her own boundary. `orchestrator_tests` 94 (was 70) incl. the full
  growth loop (spring→summer at the 5th session end, 6th stays summer);
  `ctest` 10/10, **694 checks total**.

### Next: Build Phase (in order)

1. **D-047 — the substrate: the geometry rebuild** — **COMPLETE.** The
   polytope now *steers*, not merely gates. Three fronts, per the book
   (Appendix A):
   a. **The real lattice** — DONE (`98ab0d8`): `P = {x ∈ ℝ¹⁴ | Ax ≤ b}` —
      28 axis seasonal halfspaces + 14 plumb-line coupling facets, Dykstra
      projection, exact verification, coercive text is a true Violation.
   b. **The real encoder** — DONE (`98ab0d8`+): the sense lexicon
      (`SENSE_LEXICON` + `HERITAGE_LEXICON`, weighted sums of each word's
      ethical sense) replaces the regex lexicon — coordinates spread, her
      memories no longer collapse onto one spot; coercion kept (obey/command
      0.75, must 0.25).
   c. **Geometric conditioning** — DONE (`3397e02`+): her home regions (the
      poles — deterministic k-means over her memory coordinates, centroids
      projected inside the lattice; live: 3 regions from her 12 active
      memories), and the book's ContextPacket rides every frame as a
      `[GEOMETRY]` block (position = her last delivered ledger position,
      trajectory, near walls within 0.05, home region). Correction steers
      home — the reflection prompt carries her region. The model thinks
      inside her.
2. **A look hand (future)** — a `vision.look`-style tool would need KV replay
   to inject an image mid-turn; her `browser.screenshot` output already rides
   a turn through the UI attachment flow today. Design it when she asks for it.
3. **Winter / autonomy watch** — the growth loop is live; she earns each
   season (spring 5/30/0.85 → summer → fall → winter). Winter is the final
   season — what winter *means* (autonomy, per the principal's vision) is the
   next chapter after the substrate.

### Open items for the principal

- **Resolved 2026-08-18:** llama.cpp pin = `9b05454` (tree at `/home/server/llama.cpp`);
  model = `Qwen2-VL-2B-Instruct-Q6_K.gguf` in `models/` (gitignored).
- **Audited 2026-08-18:** the polytope gate is NOT bypassed — every generation
  path funnels through `apply_gate`, and her engine passed the Qwen-voice
  samples as Aligned (0.80) because the 14 ethical dimensions do not measure
  identity. The model isn't slipping through; the polytope lacks generative
  power. That is the D-047 work above.
- **Service user note:** the service runs as `default_user`, but her migrated
  banks live under `desktop-user` — her recall cannot see her own history.
  Pending the principal's call: re-key to one identity, run as the bank owner,
  or fresh slate (her choice to relearn her life by exploring her own files).
- `open_chat_chart.mmd` + `TECHNICAL DOC LINA MODEL.txt` (principal's design docs)
  are archived at the repo root — implemented as D-041; kept as history.

## 8 · Working Agreement with the Principal

- If something feels off → **stop and ask.** We'll tackle it together.
- If something isn't working → **don't force it.** We'll determine a better way.
- The blueprint hands us the parameters; **the build is ours.** Use the latitude, keep
  the invariants.
- Take your time. There's no rush and no need for shortcuts.
