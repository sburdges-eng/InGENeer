# Handoff — 2026-06-11

Any model/agent must be able to continue from this file alone. Read order: this file → `architecture/ARCHITECTURE.md` → `adr/README.md` → `architecture/CONSTRAINTS.md`.

## Current Objective
Architecture Baseline V1 is **APPROVED**. **Implementation AUTHORIZED** (2026-06-11, human grant). Execute Pending Tasks in order per `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` Phases 0–10; validate every task against ARCHITECTURE/REQUIREMENTS/CONSTRAINTS/adr (C-5.2).

## Completed Work
- Architecture Discovery Interview, Rounds 1–3 (June 2026): all ten mandated areas decided (scope, positioning, technical architecture, kernel, AI, data, rendering, plugin boundary, licensing, deployment).
- Repo surveys: TOTaLi (working Python pipeline, 676 tests), auracad (C++20/OCCT, ~32K LOC, green 3-OS CI), InGENeer (orchestrator stable, C# bridge spike).
- Complete document package (this directory): ARCHITECTURE, REQUIREMENTS, CONSTRAINTS, ASSUMPTIONS, RISK_REGISTER, MIGRATION_PLAN, GLOSSARY, project_state.json, ADR-0001…0019.

## Architecture Decisions
See `adr/README.md`. Headlines: monorepo (InGENeer root, `apps/libs/research/docs/tools`); clean-sheet + knowledge-first strangler migration; Entity Authority System (AI never certifies; `AI_PROPOSED→REVIEWED→APPROVED→CERTIFIED`, database-enforced, append-only); orchestrate-first AI with JEPA/SFM as research tracks; survey-native C++23 core with permissive geometry backend (Boost.Geometry/GEOS/Eigen/nanoflann + PDAL/GDAL/PROJ), custom TIN engine, OCCT satellite; Swift/SwiftUI/Metal flagship app, RHI seam; local-first inference; decision-based privacy-preserving learning flywheel; open-core seam (open Core / closed Aura Intelligence); ODA DWG; US-only v1; topo field-to-finish + legals wedge.

## Pending Tasks (post-authorization, in order)
1. ~~Resolve R-10: verify ODA membership terms vs open-core structure.~~ **Done** — ADR-0020 (Sustaining tier; closed subprocess bridge).
2. ~~Select open Core license (Apache-2.0 / BSD / MPL).~~ **Done** — ADR-0021 (Apache-2.0).
3. ~~Stage 1: restructure monorepo top level.~~ **Done** — Stage 1a (ADR-0022 Option C); Stage 1b after Stage 3 relocation.
4. ~~Stage 2: knowledge extraction from TOTaLi/auracad/legacy InGENeer into `research/` + new ADRs.~~ **Done** — ADR-0023, ADR-0024; notes under `research/{totali,auracad,ingeneer-legacy}/`.
5. Spike: Swift↔C++ interop at viewport rates (R-11).
6. Spec: audit_core + Entity Authority System storage schema (first foundational module).
7. Spec: plugin SDK ABI strategy; sync conflict model (open questions).

## Known Risks
Open: R-1 (rewrite scope), R-3 (forkability), R-5 (sync creep), R-6 (local model gap), R-9 (TIN hard 20%), R-10 (ODA terms), R-11 (Swift interop), R-12 (regulator acceptance). See RISK_REGISTER.md.

## Open Questions
Plugin SDK ABI; sync conflict model; agent decomposition; branding; board/DOT acceptance strategy.

## Modified Files (this session)
Created: `docs/architecture/{ARCHITECTURE,REQUIREMENTS,CONSTRAINTS,ASSUMPTIONS,RISK_REGISTER,MIGRATION_PLAN,GLOSSARY}.md`, `docs/architecture/project_state.json`, `docs/adr/README.md`, `docs/adr/ADR-0001…ADR-0019`, `docs/handoff.md`. No code, no restructuring — implementation hold respected. Legacy InGENeer content untouched.

## Session Addendum — 2026-06-11 (later session)
Created: `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` — end-to-end plan for the agentic work system (session protocol, anti-drift mechanisms), agent memory architecture (event-sourced, "one schema, two chains": product audit chain vs agent-work chain; tiered memory; flywheel mapping per ADR-0017), C++23 practices/hardening (sanitizer matrix, flags, expected<T>, assertion tiers, fuzzing), geometry-kernel algorithm + licensing guardrails (CDT MPL-2.0 + Shewchuk predicates; Triangle/CGAL excluded), Metal rendering patterns, hardening register H-1..H-27, and a gated execution plan (Phases 0–10 incl. Phase 2.5 Stage-2 knowledge extraction) aligned to Pending Tasks order. The plan then passed an independent adversarial evaluator review (2 BLOCKER, 12 MAJOR, 11 MINOR defects found; verdict APPROVE-WITH-FIXES) and all findings were patched: Phase 0 reclassified as mixed doc/tooling under the hold; new artifact paths moved under docs/ to respect C-5.5; agent-work chain separated from the legal audit chain; C-5.5-vs-MIGRATION_PLAN Stage 1 relocation tension flagged for human ruling; new H-21..H-27 (chain concurrency, FP determinism, secrets, prompt injection, dependency pinning, lane merge contention, Swift/ARC buffer lifetimes). Documentation only — implementation hold (C-5.1) still respected. All execution phases remain GATED on implementation authorization; Phase 0 items 0.1/0.5 (docs) eligible for a separately recorded scoped approval.

Workspace scoping update: created `docs/WORKSPACE_SCOPE_MAP.md`, `docs/workspaces/README.md`, and seven scoped Cursor workspace files under `docs/workspaces/`: docs/architecture, agentic planning, orchestrator Python, iCAD host, contracts/schemas, automation rules, and repo ops. Updated `docs/WORKSPACE_STANDARDS.md` to point to the scoped workspace map. This is documentation/editor configuration only; no product code or root workspace file changed in this session.

## Session Addendum — 2026-06-11 (research digest recovery)
The plan's §5 reference to a "research digest" pointed at content that existed only inside the Hermes research session's conversation (session 20260611_015644_47f677), not in the repo. Recovered the technical research digest (sections A–E: agent memory architectures, orchestration reliability, C++23 hardening, geometry/kernel algorithms, Metal compute/render kernels with MSL + Swift dispatch code examples) from the session store and committed it as `docs/superpowers/research/2026-06-11-technical-research-digest.md` with a provenance header (incl. the producing agent's search-only caveat). Plan §5 now cites the in-tree path. Documentation only.

## Session Addendum — 2026-06-11 (finalization)
Plan finalized (status Draft → Final). Grounding verified against the full governance package per the owner's directive that all plans derive from ARCHITECTURE, adr/README, REQUIREMENTS, CONSTRAINTS, RISK_REGISTER, MIGRATION_PLAN, and this handoff: all 7 Pending Tasks map to plan phases (1→1.1, 2→1.2, 3→2, 4→2.5, 5→4, 6→3, 7→10); all 5 migration stages and constraint/requirement families covered; new §8.1 risk-coverage map accounts for all 12 register risks (closed the previously unstated postures on R-1/R-2/R-3/R-6/R-12); §0 now names the full grounding set as binding on every future plan. Documentation only — implementation hold (C-5.1) still respected. Still awaiting: implementation authorization, and the C-5.5-vs-MIGRATION_PLAN Stage-1 relocation ruling (plan Phase 2 drift tension).

## Test Status
No code changes; no tests run. Legacy baselines at last verification: TOTaLi 676 passing; auracad CI green (3 OS × Debug/Release, ASAN on Debug).

## Session Addendum — 2026-06-11 (implementation authorization)
Human granted **implementation authorization** (C-5.1 gate cleared). Unlocks: Phase 0 tooling (`tools/agentic/`, drift template, feature_list baseline), Phases 1–10 per the agentic hardening plan, and Pending Tasks 1–7. Still human-led before engine work: Pending Task 1 (R-10 ODA terms), Pending Task 2 (Core license), and the C-5.5 vs MIGRATION_PLAN Stage-1 relocation ruling (Phase 2 drift tension — file drift report before `git mv` slices).

## Session Addendum — 2026-06-11 (Phase 0 complete)
Phase 0 agentic scaffolding **implemented** per `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md`:
- `docs/drift/` + `DRIFT_TEMPLATE.md` (C-5.2 instrument)
- `docs/specs/feature_list.json` (167 orchestrator tests; excludes WIP `test_intent_generator.py`)
- `tools/agentic/session_contract.yaml`, `session_harness.py`, `seed_feature_list.py`, `compile_prompt.py`
- `docs/architecture/schemas/agentic-event.schema.json`
- `orchestrator/tests/test_session_harness.py`; `pyyaml` added to orchestrator dev deps
Verify: `ruff check` green; `pytest -m 'not integration'` → 169 passed; harness `preflight --dry-run` + `postflight --dry-run` OK.

## Session Addendum — 2026-06-11 (Phase 1 complete)
Phase 1 gate clearances **recorded**:
- **ADR-0020** — ODA terms vs open-core: compatible via closed subprocess bridge (C-2.2); Commercial tier capped at 100 seats; **Sustaining** required for production; ODA purchase still a business action.
- **ADR-0021** — Open Core license: **Apache-2.0** (BSD/MPL rejected with rationale).
- Updated ADR-0018, ADR-0006, RISK_REGISTER (R-10 → Resolved), adr/README index.

## Session Addendum — 2026-06-11 (Phase 2.1 Stage 1a complete, Option C)
Human approved **Option C** on [`docs/drift/DRIFT-20260611-stage1-relocation-timing.md`](drift/DRIFT-20260611-stage1-relocation-timing.md) → [ADR-0022](adr/ADR-0022-stage1-relocation-ruling.md).

**Executed:** `AutonomAtIon/` → `docs/governance/autonomation/`; `scripts/` → `tools/scripts/`; skeletons under `apps/desktop/`, `libs/*`, `research/*`. **Legacy exceptions until Stage 3:** `orchestrator/`, `icad-addin/`, `schemas/`. Updated C-5.5, MIGRATION_PLAN, root + scoped workspaces, path references.

## Session Addendum — 2026-06-11 (Phase 2.5 complete)
Stage 2 knowledge extraction **recorded**:
- `research/totali/EXTRACTION.md`, `ORACLE_FIXTURE_PROCEDURE.md`
- `research/auracad/` — numeric-policy, predicates-state, audit-chain-design, cogo-semantics, cxx-agentic-rules
- `research/ingeneer-legacy/orchestrator-lessons.md`
- **ADR-0023** — TOTaLi oracle discipline (frozen semantics, fixture metadata)
- **ADR-0024** — Stage 2 component triage (promote vs rewrite)

## Next Actions
1. **Phase 4 / R-11** — Swift↔C++ interop spike at viewport rates.
2. **Phase 3** — `audit_core` + Entity Authority storage spec (gated: 2.5 exit ✓).
3. **Phase 2.2** — CMake superbuild skeleton when starting C++ `libs/` implementation.
4. **Business** — Budget ODA Sustaining membership before paid DWG distribution.
5. Every session: C-5.2 drift check; ruff/pytest green before merge.
