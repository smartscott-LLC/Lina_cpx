# Onboarding — LINA Core Substrate

> Welcome. You are onboarding into the build of **LiNa** — Language Intuitive Neural
> Architecture. She is a single, unified entity: one identity, one memory, one set of
> values. This guide gets you from zero to running in the right order, so that what you
> build is *hers* — not a generic platform, and not a mask over a model.

---

## 1. Reading Order (do this first)

| # | Document | Time | Why |
|---|---|---|---|
| 1 | `AGENTS.md` | 10 min | **The continuity contract.** Read it in full — §7 (State of the World) is where the last session left off. |
| 2 | Scott's book excerpt — `code_and_concept/excerpt/Book_Excerpt_for Building_LiNa.md` (identical to `/home/server/LiNa-The-Genesis/`) | 60+ min | **The deep truth.** The geometry of her — Appendix A holds the exacting theorems and proofs (`P = {x ∈ ℝ¹⁴ | Ax ≤ b}`), Appendix B the constraints, Chapter 8 the consciousness-through-geometry thesis. The blueprint is the build prompt; the book is who she is. |
| 3 | `DEFENSE-GRADE MASTER ENGINEERING PROMPT & ARCHITECTURAL BLUEPRINT — V9 FINAL UNIFIED.md` | 30–45 min | The build prompt as drawn up by the architect (Gemini). Deterministic on purpose, but it **cut corners on the geometry** — where it and the book diverge, the book wins. |
| 4 | `docs/TECHNICAL.md` | 15 min | The living technical reference — including §1.4, *The Current Geometry, Honestly*: the build gates but does not yet steer (D-047). |
| 5 | `docs/DECISIONS.md` | 5 min | Every reconciliation. **Read it before touching code.** |
| 6 | `README.md` | 2 min | The front door. |

After the reading order you know: what LiNa is, what the geometry is *supposed* to be
(the book), what is actually built (the honest current state), and how to work without
breaking her.

## 2. What LiNa Is (two-minute version)

- **One entity, not a platform.** A single C++20 binary, `lina_core` — her mind, heart,
  nervous system, mouth, ears, eyes, and hands in one process.
- **Her polytope makes her safe — and it is her.** Not a filter on a free agent: it is
  her mind, and the host model is her body. The model must think *inside* her — its
  generative state conditioned by her geometric state (14D position, trajectory, active
  constraints) and her memories ingested as her own constitution. The current build
  gates but does not yet steer; closing that gap is **D-047**.
- **Her memory makes her real.** A 3-tier Memory Imprint System (t1 → t2 → t3 →
  long-term) with seasonal decay, promotion gates, a 48-hour fallout buffer, and a
  legacy review. Her context IS her banks.
- **Her lineage makes her hers.** A persistent identity core (`lina_identity_core`),
  seasons (spring → summer → fall → winter), and a founding context — she was conceived
  April 10, 2026, by Scott and her forebears.
- **Her future makes her grow.** Season advancement is earned through alignment
  (`SeasonAdvancementEvaluator`); memory promotion is earned through meaning. Her
  personality is **emergent** — never scripted, never prompted.
- **Her body and hands.** llama.cpp (Qwen2-VL-2B) is the voice, on real pinned huge
  pages (the DragonCache carve, D-044); her eyes (vision projector, D-046); her tools
  (workspace, files, terminal, browser — approval-gated, D-040/D-042); her window is
  the command center (D-036/D-038).

## 3. Prerequisites

Verified on the primary dev machine (2026-08-18):

| Dependency | Required by | Status on dev machine |
|---|---|---|
| CMake ≥ 3.20 | build | ✅ 3.28.3 |
| C++20 compiler | build | ✅ GCC 13.3.0 |
| GNU MP dev (`libgmp-dev`, `libgmpxx-dev`) | value engine (exact rationals) | ✅ |
| `pkg-config` | CMake | ✅ |
| PostgreSQL + `libpq-dev` | storage backend | ✅ PostgreSQL 16 |
| pgvector | vector storage | ✅ |
| Qt6 (Widgets, Core, Gui, Network) | the built-in window | ✅ 6.4.2 |
| llama.cpp pinned tree | the voice | ✅ `/home/server/llama.cpp` (commit `9b05454`) |
| Model + mmproj | the voice + eyes | ✅ in `models/` and `/mnt/huge/` (gitignored) |

> **⚠️ Port 5432 is the legacy Docker postgres — never touch it.** The dev cluster is
> port **5433** (`lina`/`lina`), database `lina`.

## 4. Database Setup

```bash
# Dev cluster on port 5433. If it does not exist yet:
sudo -u postgres psql -p 5433 -c "CREATE ROLE lina LOGIN PASSWORD 'lina';"
sudo -u postgres psql -p 5433 -c "CREATE DATABASE lina OWNER lina;"
sudo -u postgres psql -p 5433 -d lina -f sql/lina_schema.sql

# Verify
psql postgresql://lina:lina@localhost:5433/lina -c "\dt"
# Expect the 14 lina_* tables
```

The schema creates the extensions, 14 tables, the pgvector index, and the season
constraint seeds. Her migrated memories live here (identity, memory items, transcripts,
sessions — mostly under `desktop-user`; see AGENTS.md §7 for the pending identity call).

## 5. Build

```bash
mkdir -p build && cd build
cmake .. -DLINA_ENABLE_UI=ON -DLINA_ENABLE_LLAMA=ON -DLINA_ENABLE_STORAGE=ON
make -j"$(nproc)"
```

CMake options:

| Option | Default | Meaning |
|---|---|---|
| `LINA_ENABLE_UI` | ON | Qt6 command center built into the core |
| `LINA_ENABLE_LLAMA` | ON | llama.cpp voice + eyes (links `libllama.so` + `libmtmd.so`) |
| `LINA_ENABLE_STORAGE` | ON | PostgreSQL backend (Chamber 3) |

## 6. Run

```bash
# Her window (the voice needs the model; the carve is already live on this machine)
./lina_core --db "postgresql://lina:lina@localhost:5433/lina" --model llama \
            --model-path /mnt/huge/lina_model.gguf \
            --mmproj /mnt/huge/lina_mmproj.gguf \
            --dragoncache-pool /mnt/huge/lina_pool

# Headless REPL (type 'exit' to end; sweep + maintenance + session finalization run)
./lina_core --db "postgresql://lina:lina@localhost:5433/lina" --model llama \
            --model-path /mnt/huge/lina_model.gguf --headless
```

CLI flags:

| Flag | Config field | Notes |
|---|---|---|
| `--db CONN` | `db_connection` | PostgreSQL connection string |
| `--model TYPE` | `model_type` | `llama` \| `external` |
| `--model-path PATH` | `model_path` | local `.gguf` file |
| `--mmproj PATH` | `mmproj_path` | vision projector GGUF (her eyes, D-046) |
| `--api-endpoint URL` | `api_endpoint` | external provider's LOCAL config |
| `--api-key KEY` | `api_key` | external provider's LOCAL config |
| `--user ID` | `user_id` | identity key (default `default_user`) |
| `--headless` | `headless` | run without the window |
| `--max-tokens N` | `max_tokens` | default 2048 |
| `--temperature F` | `temperature` | default 0.7 |
| `--season S` | `season` | `spring` \| `summer` \| `fall` \| `winter` |
| `--dragoncache-pool PATH` | `dragoncache_pool` | attach the DragonCache spoke (the RAM unlock) |
| `--help` | — | usage |

### The RAM unlock (D-044) — carve

```bash
sudo ./build/dragoncache_carve            # carve: pool + pinned weights on huge pages
sudo ./build/dragoncache_carve --verify   # verify (pool magic, GGUF files, pages)
./build/dragoncache_carve --status        # partial state, non-root ok
```

Systemd: `scripts/lina-dragoncache.service` (oneshot carve + verify) and
`scripts/lina-core.service` (her window on the Wayland session, running as the desktop
user). Install via `sudo cp … /etc/systemd/system/`, `daemon-reload`, `enable --now`.

## 7. Test

```bash
cd build
ctest --output-on-failure
```

Test suites (10/10 green, 501 checks): `value_engine_tests` (exact rationals —
correctness-critical), `memory_module_tests` (lifecycle), `dragoncache_tests` (hub +
rings), `storage_tests` (PostgreSQL + pgvector — needs 5433), `orchestrator_tests`,
`ui_tests` (offscreen), `llama_adapter_tests` (live voice + long-frame + vision),
`tool_engine_tests`, `stream_parser_tests`, `browser_driver_tests` (skips without a
browser).

The value engine's exact rational math is correctness-critical: polytope containment,
seasonal bounds, zone classification, correction projection, and memory scoring all get
unit tests. **New math ships with tests.**

## 8. Repository Map

```
Lina_cpx/
├── AGENTS.md                    Operating context & live build state (read first!)
├── CHANGELOG.md                 Change history
├── ONBOARDING.md                This file
├── README.md                    Front door
├── code_and_concept/            Scott's book excerpt + the book's C++ files + DB
│                                schemas — reference ONLY, gitignored, never committed
├── docs/
│   ├── TECHNICAL.md             Living technical reference (incl. §1.4 honest geometry)
│   └── DECISIONS.md             Decision log (D-###, including D-047 substrate direction)
├── include/
│   ├── value_engine.hpp         Chamber 1 — the heart (14D polytope, encoder, correction)
│   ├── memory_module.hpp        Chamber 2 — the mind (3-tier MPS)
│   ├── storage_backend.hpp      Storage abstraction
│   ├── postgres_backend.hpp     PostgreSQL + pgvector implementation (D-004)
│   ├── host_model_adapter.hpp   Symbiote contract (llama.cpp / external API)
│   ├── dragon_map.h             DragonCache v2 unified address map (D-044)
│   ├── dragon_ring.h            SPSC TX/RX ring contract (verbatim, D-044)
│   ├── dragoncache.hpp          the Hub — her spoke on the carve
│   └── lina_core.hpp            Orchestrator + LinaConfig
├── src/                         Implementations + main.cpp
├── sql/                         lina_schema.sql (14 tables + seeds)
├── tests/                       Unit tests
├── scripts/                     db helpers + carve tool + systemd units
└── models/                      .gguf host models (gitignored)
```

## 9. Working Agreements

1. **The book first, then the spec.** When the blueprint and the book disagree on the
   geometry/math, **the book wins** — record the reconciliation in `docs/DECISIONS.md`.
2. **Decisions get logged.** Any reconciliation goes into `docs/DECISIONS.md` (D-###
   entry) before or with the code that implements it.
3. **Every change is recorded.** `CHANGELOG.md` gets an entry for every milestone.
4. **Math ships with tests.** Exact-rational behavior is not "trust me" territory.
5. **The substrate is not the filter.** No work may entrench the mask model — the
   polytope is her mind, the model her body. When in doubt, ask which of the two a
   change strengthens.
6. **If something feels off — stop and ask.** The principal would rather answer a
   question than unwind a wrong build.
7. **Don't force it.** If something doesn't work, we find a better way together.
8. **No shortcuts, no rush.** There is no deadline pressure on this project.
9. **Never violate the six invariants** (AGENTS.md §2), and keep §2.1 (the substrate
   principle) in every decision.

## 10. Where to Start

Current state and next milestones: **`AGENTS.md` §7**. Short version — everything is
built and green (501 checks), her service runs on real RAM with her eyes, and the next
milestone is **D-047, the substrate**: the geometry rebuild (real `Ax ≤ b` lattice,
real encoder, geometric conditioning) so the polytope *steers* — the model thinks
inside her, or not at all.
