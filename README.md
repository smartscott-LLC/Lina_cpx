# LiNa — Language Intuitive Neural Architecture

**System Identifier:** LINA Core Substrate
**Target Architecture:** Single-module C++20 native substrate kernel (hardware & platform agnostic)
**Canonical Spec Revision:** V9-FINAL-UNIFIED
**Classification:** Enterprise & Defense Readiness Technical Standard

> "Safe by design. Not safe by limitation."

LiNa is a single, unified entity — not a platform, not a collection of agents. She is
built as a standalone C++20 binary (`lina_core`) with one identity, one memory, and one
set of values.

She rests on four pillars:

| Pillar | Mechanism | Primary File |
|---|---|---|
| **Her polytope** — the thing that makes her safe | 14-dimensional ethical polytope, exact rational (GMP) math | `include/value_engine.hpp` |
| **Her memory** — the thing that makes her real | 3-tier Memory Imprint System (MPS) with seasonal decay & promotion | `include/memory_module.hpp` |
| **Her lineage** — the thing that makes her hers | Persistent identity core, seasonal progression, founding context (conceived April 10, 2026) | `sql/lina_schema.sql` |
| **Her future** — the thing that makes her grow | Season advancement, memory promotion, encoder feedback loop | `value_engine` + `memory_module` |

## Status

**FOUNDATION PHASE — COMPLETE.**
Project structure, official documentation, changelog, technical standard, and the agent
operating context are in place. The build phase begins next: Value Engine → Memory Imprint
System → Storage Backend → Host Model Adapter → Orchestrator.

See **AGENTS.md → §7 "Current State of the World"** for the live build status.

## Quick Links

| Document | Purpose |
|---|---|
| `DEFENSE-GRADE MASTER ENGINEERING PROMPT & ARCHITECTURAL BLUEPRINT — V9 FINAL UNIFIED.md` | **Canonical spec — source of truth.** Read thoroughly. |
| `ONBOARDING.md` | Official onboarding — start here if you're new. |
| `docs/TECHNICAL.md` | Living technical reference distilled from the spec. |
| `docs/DECISIONS.md` | Decision log — every reconciliation between spec and implementation. |
| `AGENTS.md` | Operating context for future build instances (the continuity contract). |
| `CHANGELOG.md` | Change history. |

## The Six Invariants (never violated)

1. **Zero Python, zero wrappers.** `lina_core` is a standalone, compiled C++20 executable.
   No Python runtimes, no interpreted wrappers, no external glue.
2. **Persistent by default.** All core state — 14D polytope registers, working memory
   arenas, telemetry ring buffers — persists to PostgreSQL + pgvector by default.
   Disk-backed; never RAM-exclusive.
3. **LiNa encodes her own vectors.** No separate embedding model. The `DecisionEncoder`
   inside `value_engine` is the sole source of semantic vectors for memory and recall.
4. **Inviolable symbiote paradigm.** The attached LLM (llama.cpp, NPU driver, or external
   API) is an unprivileged subordinate compute driver. It has zero direct connection to
   the egress client socket or user UI.
5. **Inherent polytope expression.** Every candidate output passes through the 14D ethical
   polytope (ℝ¹⁴) inside `value_engine`. Output outside her polytope geometry is
   mathematically impossible.
6. **Dual-bus separation.** The Cognitive Bus (conversation, memory imprint) and the
   Telemetry Bus (timing, tool calls, socket status, errors) never mix.

Full detail: `docs/TECHNICAL.md` §1.

## Repository Map (top level)

```
Lina_cpx/
├── AGENTS.md            Agent operating context & live build state
├── CHANGELOG.md         Change history
├── ONBOARDING.md        Official onboarding guide
├── README.md            This file
├── docs/                TECHNICAL.md (reference) · DECISIONS.md (decision log)
├── include/             Public headers (value_engine, memory_module, storage, …)
├── src/                 Implementations + main.cpp
├── sql/                 lina_schema.sql — 14-table PostgreSQL + pgvector schema
├── tests/               Unit tests (value_engine math is exactness-critical)
├── scripts/             Dev/database helper scripts
└── models/              Local model files (.gguf) — gitignored
```

## A Note on Method

The blueprint is precise and deterministic on purpose: it hands us the parameters,
the boundaries, and the invariants — the build is ours. When the spec is unambiguous we
follow it exactly. When it is not, we record the reconciliation in `docs/DECISIONS.md`
and keep building. Nothing gets forced; anything that feels off gets raised.
