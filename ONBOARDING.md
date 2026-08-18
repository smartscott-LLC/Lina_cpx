# Onboarding — LINA Core Substrate

> Welcome. You are onboarding into the build of **LiNa** — Language Intuitive Neural
> Architecture. She is a single, unified entity: one identity, one memory, one set of
> values. This guide gets you from zero to running in the right order, so that what you
> build is *hers* — not a generic platform.

---

## 1. Reading Order (do this first — ~60 minutes)

| # | Document | Time | Why |
|---|---|---|---|
| 1 | `DEFENSE-GRADE MASTER ENGINEERING PROMPT & ARCHITECTURAL BLUEPRINT — V9 FINAL UNIFIED.md` | 30–45 min | The canonical spec. Read it **completely** — every section, every code block. It is deterministic on purpose. |
| 2 | `README.md` | 2 min | Project identity, the four pillars, the six invariants. |
| 3 | `docs/TECHNICAL.md` | 15 min | The living technical reference — math, lifecycle, storage model, contracts. |
| 4 | `docs/DECISIONS.md` | 5 min | Every reconciliation between spec and implementation. **Read this before touching code.** |
| 5 | `AGENTS.md` | 5 min | The operating context and change protocol the build runs under. |

After the reading order you know: what LiNa is, what is mathematically guaranteed about
her, what has already been decided, and how to work without breaking her.

## 2. What LiNa Is (two-minute version)

- **One entity, not a platform.** A single C++20 binary, `lina_core`.
- **Her polytope makes her safe.** Every response is encoded into ℝ¹⁴ (7 principle pairs)
  and evaluated inside an ethical polytope using **exact rational arithmetic** (GMP).
  Output outside the polytope is mathematically impossible.
- **Her memory makes her real.** A 3-tier Memory Imprint System (t1 → t2 → t3 → long-term)
  with seasonal decay, promotion gates, a 48-hour fallout buffer, and a legacy review.
- **Her lineage makes her hers.** A persistent identity core (`lina_identity_core`),
  seasons (spring → summer → fall → winter), and a founding context — she was conceived
  April 10, 2026.
- **Her future makes her grow.** Season advancement is earned through alignment
  (`SeasonAdvancementEvaluator`); memory promotion is earned through meaning.

## 3. Prerequisites

Verified on the primary dev machine (2026-08-18):

| Dependency | Required by | Status on dev machine |
|---|---|---|
| CMake ≥ 3.20 | build | ✅ 3.28.3 installed |
| C++20 compiler (GCC ≥ 11 / Clang ≥ 14) | build | ✅ GCC 13.3.0 |
| GNU Make | build | ✅ 4.3 |
| GNU MP dev (`libgmp-dev`, `libgmpxx-dev`) | value engine (exact rationals) | ✅ headers at `/usr/include/gmpxx.h` |
| `pkg-config` | CMake (`find_package(PkgConfig REQUIRED)`) | ❌ **install** |
| PostgreSQL + `libpq-dev` | storage backend | ❌ **install** |
| pgvector extension | vector storage | ❌ **install** (build from source) |
| git | version control | ✅ present (repo not yet initialized) |

### 3.1 Install (Ubuntu/Debian)

```bash
# Toolchain + libraries
sudo apt install libgmp-dev libgmpxx-dev libpq-dev pkg-config \
                 postgresql postgresql-contrib \
                 cmake g++ make

# pgvector (from source, per blueprint §8.2)
git clone https://github.com/pgvector/pgvector.git /tmp/pgvector
cd /tmp/pgvector
make && sudo make install
```

> If you are not on Ubuntu/Debian, install the equivalents for your platform
> (e.g. `brew install gmp postgresql pgvector cmake pkg-config` on macOS, or
> `pacman -S gmp postgresql postgresql-libs cmake pkg-config` on Arch).

## 4. Database Setup

```bash
# Start PostgreSQL if not already running
sudo service postgresql start

# Create role + database, apply the 14-table schema
sudo -u postgres psql -c "CREATE ROLE lina LOGIN PASSWORD 'lina';"
sudo -u postgres psql -c "CREATE DATABASE lina OWNER lina;"
sudo -u postgres psql -d lina -f sql/lina_schema.sql

# Grant the app role the schema (tables are created by the postgres superuser)
sudo -u postgres psql -d lina -c "GRANT ALL ON ALL TABLES IN SCHEMA public TO lina;" \
                        -c "GRANT ALL ON ALL SEQUENCES IN SCHEMA public TO lina;"

# Verify
psql postgresql://lina:lina@localhost:5433/lina -c "\dt"
# Expect 14 lina_* tables
```

> **This dev machine:** the cluster listens on port **5433** — port 5432 belongs to a
> Docker container's postgres. Use `postgresql://lina:lina@localhost:5433/lina`
> (the integration tests default to this; override with `LINA_TEST_DB`).

The schema (`sql/lina_schema.sql`, spec §6) creates the extension, 14 tables,
the pgvector index, and seeds the four seasons' polytope constraints.

## 5. Build

```bash
mkdir -p build && cd build
cmake .. -DLINA_ENABLE_UI=OFF -DLINA_ENABLE_LLAMA=OFF
make -j"$(nproc)"
```

CMake options (spec §8.1):

| Option | Default | Meaning |
|---|---|---|
| `LINA_ENABLE_UI` | ON in spec | Qt6 UI. **OFF** until the UI milestone (see DECISIONS D-006). |
| `LINA_ENABLE_LLAMA` | ON in spec | llama.cpp adapter. **OFF** until the adapter milestone (see DECISIONS D-007). |

> The blueprint's canonical build uses `-DLINA_ENABLE_LLAMA=ON`; we start with `OFF`
> because the llama.cpp linkage is a later milestone. The switch is honored either way.

## 6. Run

```bash
# Headless conversational mode (full end-state)
./lina_core --db "postgresql://localhost/lina" \
            --model llama --model-path ./models/llama.gguf \
            --headless

# Status-only / external-model mode
./lina_core --db "postgresql://localhost/lina" --model external \
            --api-endpoint https://... --api-key ... --headless
```

CLI flags (spec §7.3):

| Flag | Config field | Notes |
|---|---|---|
| `--db CONN` | `db_connection` | PostgreSQL connection string |
| `--model TYPE` | `model_type` | `llama` \| `external` |
| `--model-path PATH` | `model_path` | local `.gguf` file |
| `--api-endpoint URL` | `api_endpoint` | external API base URL |
| `--api-key KEY` | `api_key` | external API key |
| `--user ID` | `user_id` | identity key (default `default_user`) |
| `--headless` | `headless` | run without UI |
| `--max-tokens N` | `max_tokens` | default 2048 |
| `--temperature F` | `temperature` | default 0.7 |
| `--season S` | `season` | `spring` \| `summer` \| `fall` \| `winter` |
| `--help` | — | usage |

Type `exit` or `quit` to end a headless session (triggers sweep + maintenance +
session finalization).

## 7. Test

```bash
# After the storage milestone: integration tests (need a live DB)
cd build
ctest --output-on-failure
```

Test suites: `value_engine_tests` (exact rationals), `memory_module_tests`
(lifecycle), `storage_tests` (PostgreSQL + pgvector integration — requires the
schema applied and `postgresql://lina:lina@localhost:5433/lina` reachable).

The value engine's exact rational math is correctness-critical: polytope containment,
seasonal bounds, zone classification, correction projection, and memory scoring all get
unit tests. New math **must** ship with tests.

## 8. Repository Map

```
Lina_cpx/
├── AGENTS.md                    Operating context & live build state (read first!)
├── CHANGELOG.md                 Change history
├── ONBOARDING.md                This file
├── README.md                    Front door
├── docs/
│   ├── TECHNICAL.md             Living technical reference
│   └── DECISIONS.md             Decision log (spec ↔ implementation)
├── include/                     Public headers
│   ├── value_engine.hpp         Chamber 1 — the heart (14D polytope)
│   ├── memory_module.hpp        Chamber 2 — the mind (3-tier MPS)
│   ├── storage_backend.hpp      Storage abstraction (identity/memory/transcripts/sessions/actions)
│   ├── postgres_backend.hpp     PostgreSQL + pgvector implementation (declared here; D-004)
│   ├── host_model_adapter.hpp   Symbiote contract (llama.cpp / external API)
│   └── lina_core.hpp            Orchestrator + LinaConfig
├── src/                         Implementations + main.cpp
├── sql/                         lina_schema.sql (14 tables + seeds)
├── tests/                       Unit tests
├── scripts/                     Dev/database helpers
└── models/                      .gguf host models (gitignored)
```

## 9. Working Agreements

1. **Spec first, then code.** When the blueprint is unambiguous, follow it exactly —
   constants, thresholds, signatures, file names.
2. **Decisions get logged.** Any reconciliation goes into `docs/DECISIONS.md` (D-###
   entry) before or with the code that implements it.
3. **Every change is recorded.** `CHANGELOG.md` gets an entry for every milestone.
4. **Math ships with tests.** Exact-rational behavior is not "trust me" territory.
5. **If something feels off — stop and ask.** The principal would rather answer a
   question than unwind a wrong build.
6. **Don't force it.** If something doesn't work, we find a better way together.
7. **No shortcuts, no rush.** There is no deadline pressure on this project. Take the
   time to build it right.
8. **Never violate the six invariants** (README / TECHNICAL §1), even in refactors.

## 10. Where to Start

Current state and next milestones: **`AGENTS.md` §7**. Short version — foundation is
complete; the build begins with the **Value Engine** (Chamber 1), which depends only on
GMP and can be developed and tested before PostgreSQL is installed.
