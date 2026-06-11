# Handoff — 2026-06-11

Any model/agent must be able to continue from this file alone. Read order: this file → `architecture/ARCHITECTURE.md` → `adr/README.md` → `architecture/CONSTRAINTS.md`.

## Current Objective
Architecture Baseline V1 is **APPROVED**. Implementation is **ON HOLD**. Next gate: human review of this document package, then explicit implementation authorization.

## Completed Work
- Architecture Discovery Interview, Rounds 1–3 (June 2026): all ten mandated areas decided (scope, positioning, technical architecture, kernel, AI, data, rendering, plugin boundary, licensing, deployment).
- Repo surveys: TOTaLi (working Python pipeline, 676 tests), auracad (C++20/OCCT, ~32K LOC, green 3-OS CI), InGENeer (orchestrator stable, C# bridge spike).
- Complete document package (this directory): ARCHITECTURE, REQUIREMENTS, CONSTRAINTS, ASSUMPTIONS, RISK_REGISTER, MIGRATION_PLAN, GLOSSARY, project_state.json, ADR-0001…0019.

## Architecture Decisions
See `adr/README.md`. Headlines: monorepo (InGENeer root, `apps/libs/research/docs/tools`); clean-sheet + knowledge-first strangler migration; Entity Authority System (AI never certifies; `AI_PROPOSED→REVIEWED→APPROVED→CERTIFIED`, database-enforced, append-only); orchestrate-first AI with JEPA/SFM as research tracks; survey-native C++23 core with permissive geometry backend (Boost.Geometry/GEOS/Eigen/nanoflann + PDAL/GDAL/PROJ), custom TIN engine, OCCT satellite; Swift/SwiftUI/Metal flagship app, RHI seam; local-first inference; decision-based privacy-preserving learning flywheel; open-core seam (open Core / closed Aura Intelligence); ODA DWG; US-only v1; topo field-to-finish + legals wedge.

## Pending Tasks (post-authorization, in order)
1. Resolve R-10: verify ODA membership terms vs open-core structure.
2. Select open Core license (Apache-2.0 / BSD / MPL).
3. Stage 1: restructure monorepo top level.
4. Stage 2: knowledge extraction from TOTaLi/auracad/legacy InGENeer into `research/` + new ADRs.
5. Spike: Swift↔C++ interop at viewport rates (R-11).
6. Spec: audit_core + Entity Authority System storage schema (first foundational module).
7. Spec: plugin SDK ABI strategy; sync conflict model (open questions).

## Known Risks
Open: R-1 (rewrite scope), R-3 (forkability), R-5 (sync creep), R-6 (local model gap), R-9 (TIN hard 20%), R-10 (ODA terms), R-11 (Swift interop), R-12 (regulator acceptance). See RISK_REGISTER.md.

## Open Questions
Plugin SDK ABI; sync conflict model; agent decomposition; branding; board/DOT acceptance strategy; ODA terms; Core license.

## Modified Files (this session)
Created: `docs/architecture/{ARCHITECTURE,REQUIREMENTS,CONSTRAINTS,ASSUMPTIONS,RISK_REGISTER,MIGRATION_PLAN,GLOSSARY}.md`, `docs/architecture/project_state.json`, `docs/adr/README.md`, `docs/adr/ADR-0001…ADR-0019`, `docs/handoff.md`. No code, no restructuring — implementation hold respected. Legacy InGENeer content untouched.

## Session Addendum — 2026-06-11 (later session)
Created: `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` — end-to-end plan for the agentic work system (session protocol, anti-drift mechanisms), agent memory architecture (event-sourced, "one schema, two chains": product audit chain vs agent-work chain; tiered memory; flywheel mapping per ADR-0017), C++23 practices/hardening (sanitizer matrix, flags, expected<T>, assertion tiers, fuzzing), geometry-kernel algorithm + licensing guardrails (CDT MPL-2.0 + Shewchuk predicates; Triangle/CGAL excluded), Metal rendering patterns, hardening register H-1..H-27, and a gated execution plan (Phases 0–10 incl. Phase 2.5 Stage-2 knowledge extraction) aligned to Pending Tasks order. The plan then passed an independent adversarial evaluator review (2 BLOCKER, 12 MAJOR, 11 MINOR defects found; verdict APPROVE-WITH-FIXES) and all findings were patched: Phase 0 reclassified as mixed doc/tooling under the hold; new artifact paths moved under docs/ to respect C-5.5; agent-work chain separated from the legal audit chain; C-5.5-vs-MIGRATION_PLAN Stage 1 relocation tension flagged for human ruling; new H-21..H-27 (chain concurrency, FP determinism, secrets, prompt injection, dependency pinning, lane merge contention, Swift/ARC buffer lifetimes). Documentation only — implementation hold (C-5.1) still respected. All execution phases remain GATED on implementation authorization; Phase 0 items 0.1/0.5 (docs) eligible for a separately recorded scoped approval.

## Test Status
No code changes; no tests run. Legacy baselines at last verification: TOTaLi 676 passing; auracad CI green (3 OS × Debug/Release, ASAN on Debug).

## Next Actions
1. Human: review package → grant/withhold implementation authorization.
2. On authorization: execute Pending Tasks order above; every implementation task validates against ARCHITECTURE/REQUIREMENTS/CONSTRAINTS/adr first (drift rule C-5.2); update this handoff every session.
