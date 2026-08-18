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
├── reference/                      ← principal-provided material lives in `code_and_concept/`
│                                      (gitignored, disposable — removed after Value Engine milestone)
```

## 4 · Build / Test / Run

```bash
# DB (once; requires postgres + pgvector installed — see ONBOARDING.md §3–4)
sudo -u postgres createdb lina
sudo -u postgres psql -d lina -f sql/lina_schema.sql

# Build
mkdir -p build && cd build
cmake .. -DLINA_ENABLE_UI=OFF -DLINA_ENABLE_LLAMA=OFF
make -j"$(nproc)"

# Test
ctest --output-on-failure

# Run (headless)
./lina_core --db "postgresql://localhost/lina" --model llama \
            --model-path ./models/llama.gguf --headless
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

_Last updated: 2026-08-18 (foundation phase)._

### Done

- ✅ Canonical spec read in full (2,391 lines / V9 FINAL UNIFIED).
- ✅ Project structure: `include/ src/ sql/ tests/ scripts/ models/ docs/`.
- ✅ `README.md`, `ONBOARDING.md`, `CHANGELOG.md`.
- ✅ `docs/TECHNICAL.md` (living reference), `docs/DECISIONS.md` (D-001…D-010).
- ✅ `AGENTS.md` (this file).
- ✅ Environment audited: cmake 3.28.3, GCC 13.3.0, GMP headers present.
  ❌ Not yet installed: PostgreSQL, libpq-dev, pgvector, pkg-config.
- ✅ Git repo initialized, first commit pushed (`bfc9d1b`, branch `main`).
- ✅ Season bounds reconciled (D-002): fall `order_min 3.2`, `chaos_max 3.8`.
- ✅ D-003 resolution path chosen: principal-provided reference material (book proofs +
  original C++ code) → **math-only extraction**; `code_and_concept/` gitignored,
  disposable (delivered 2026-08-18).

### Next: Build Phase (in order)

1. **Value Engine** (`value_engine.hpp/.cpp`) — ✅ **COMPLETE.** Header + implementation
   authored (D-011…D-025); exact-math suite green (159 checks, `ctest` 100%).
   Includes the D-024 projection fix: corrected vectors always land strictly inside
   the polytope (Invariant 5).
2. **Memory Module** (`memory_module.hpp/.cpp`) — ✅ **COMPLETE.** Header + implementation
   authored (D-019, D-026…D-028); exact-math suite green (107 checks, `ctest` 100%).
   Includes the D-027 fix: the fallout buffer enforces its documented 48-hour grace.
3. **Storage** — ✅ **COMPLETE.** `sql/lina_schema.sql` applied (14 tables + seeds +
   pgvector); `storage_backend.hpp` (blueprint §4.1); `postgres_backend.hpp/.cpp`
   (D-004/D-005/D-031; D-030/D-032 fixes). PostgreSQL 16 + pgvector installed;
   integration suite green (59 checks, `ctest` 100%). Reference schema reviewed
   (D-029). **Dev machine notes:** cluster on port **5433** (5432 is a Docker
   container's postgres); role/db `lina`/`lina`; schema grants applied.
   ⚠️ **Port 5432 is LINA's live memory postgres (her existing memories) —
   never touch, never migrate, never stop the container.** The core's dev
   database is the 5433 cluster only.
4. **Host Model Adapter** (`host_model_adapter.hpp`) — ✅ **COMPLETE (contract).**
   Blueprint §5 interface + adapter declarations; providers plug in via the
   `make_driver()` seam (D-033) — no provider logic in the core (D-023).
5. **Orchestrator** — ✅ **COMPLETE.** `lina_core.hpp/.cpp`, `main.cpp`, CMake
   wiring; driver injected via `attach_model` (D-033); tier-move UPSERT fix
   (D-034). Integration suite green (15 checks); `lina_core` binary boots
   headless against the live stack.
6. **Tests** — exact-math suites + integration; `ctest` 4/4 green (325 checks).
   **Remaining:** concrete drivers (llama.cpp / external API) plug into
   `make_driver` when their milestone lands (D-007).

### Open items for the principal

- **D-003 resolved:** reference material (`code_and_concept/`) fully read and extracted
  (2026-08-18); formulas/patterns recorded in D-011…D-019; carve/mmap infrastructure
  excluded per D-020. Value Engine implementation is un-gated.
- **Storage milestone** unblocked: PostgreSQL 16 + pgvector installed and running
  (port 5433), schema applied, integration tests green. Next milestone: Host Model
  Adapter (interface + D-023 provider plug-ins).

## 8 · Working Agreement with the Principal

- If something feels off → **stop and ask.** We'll tackle it together.
- If something isn't working → **don't force it.** We'll determine a better way.
- The blueprint hands us the parameters; **the build is ours.** Use the latitude, keep
  the invariants.
- Take your time. There's no rush and no need for shortcuts.
