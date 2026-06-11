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

## Session Addendum — 2026-06-11 (Phases 2.2/2.3, 3.1–3.4, 4, 10 — implementation session)

Autonomous implementation session. **Changes are staged in the working tree, NOT committed**
(awaiting owner approval per repo policy). All gates run locally green.

**Phase 3.1 — audit_core storage spec (DONE):**
`docs/superpowers/specs/2026-06-11-audit-core-storage-schema-spec.md` — SQLite WAL + single-writer
(H-21); `event`/`entity`/`promotion` schema; frozen SHA-256 canonical record (ADR-0023 oracle
discipline); storage-layer authority guards (C-1.1/R-2.3); Certified Snapshot (R-2.4/2.5);
two-chains model (§2). Two human decisions flagged (licensed-identity boundary; SHA-256 freeze).

**Phase 2.2 — CMake superbuild (DONE):** root `CMakeLists.txt`, `CMakePresets.json`
(dev/hardened/asan-ubsan/tsan), `libs/CMakeLists.txt`. C-4.2 forbidden-flag guard at configure
time. `libs/` wired only for ready modules (no placeholder targets — C-5.4).

**Phase 2.3 — gates (DONE):** `.clang-format`, `.clang-tidy`, `tools/scripts/check_numeric_flags.sh`
(C-4.2 fast-math grep over compile_commands.json). `build/` + sqlite sidecars gitignored.

**Phase 3.2–3.6 — audit_core C++ implementation (DONE; evaluator sign-off pending):**
`libs/audit_core/` — self-contained SHA-256 (FIPS vectors pass; no OpenSSL/CommonCrypto, C-4.1),
canonical record, `Store` (append/create_entity/promote/verify_chain/certified_snapshot) over
SQLite with append-only triggers + `std::expected` errors (C-4.5). **6 CTest suites pass under
`dev` AND `asan-ubsan` (0 failures).** Guards proven by test: agent cannot APPROVE/CERTIFY
(AuthorityViolation); stale head_seq → ConcurrencyConflict; tamper → ChainBroken; AI_PROPOSED
excluded from snapshot.
- **3.5 agent-work chain (DONE):** `test_agent_chain.cpp` — same chaining code, separate db,
  no product authority, consolidation MVP (session→CONSOLIDATION→handoff), independent verify.
- **3.6 fuzzing (DONE):** `fuzz/fuzz_canonical.cpp` (`LLVMFuzzerTestOneInput`) + deterministic
  `standalone_main.cpp` driver (200k iterations, ASan/UBSan-clean). Real libFuzzer binary builds
  under `-DINGENEER_FUZZ=ON` (needs a libFuzzer runtime — Apple clang ships none; Linux CI / brew
  LLVM has it).
- **§6 cross-language parity (DONE):** C++ canonical record is now BYTE-IDENTICAL to Python
  `audit.py` (json.dumps sort_keys default separators); two Python-generated SHA-256 oracle
  vectors pinned in `test_canonical.cpp`. Remaining nicety: a full JSONL round-trip test (write in
  C++, verify in Python) — the hash oracle already proves the encoding parity.
- **Remaining for Phase 3 EXIT:** evaluator sign-off (generator/evaluator separation, plan §1.1)
  — a process gate, not code.

**Phase 4 — Swift↔C++ interop spike (DONE):** `tools/spikes/interop/` runnable spike
(`./run.sh`). Measured: **pure FFI dispatch ≈ 0.7 ns/call**, zero-copy buffer sharing confirmed,
generation-stamp invalidation (H-27) works. → **ADR-0025** (direct C ABI; no Obj-C++ shim; A-6
holds; R-11 mitigated for CPU path). Render-path Metal `bytesNoCopy` + realloc-under-render test
deferred to Phase 8.

**Phase 10 — specs (DONE, spec-first):**
`docs/superpowers/specs/2026-06-11-plugin-sdk-abi-spec.md` (hourglass C ABI; deny-by-default
capabilities; no certify primitive) and `2026-06-11-sync-conflict-model-spec.md` (command-log-replay
over A-9; authority-aware non-auto-merge; per-replica hash-DAG). Both list human sign-off items;
future ADRs proposed as 0026 (ABI) / 0027 (sync).

**ADR index:** added 0023/0024 rows (were missing) + 0025 (interop).

**Gates this session:** ruff ✓ · pytest 173 passed/1 skipped ✓ · ctest 4/4 (dev + asan-ubsan) ✓ ·
clang-format ✓ · check_numeric_flags ✓. No C# touched (dotnet unaffected). No schemas touched.

## Session Addendum — 2026-06-11 (debug & refactor: audit_core hardening)

PR #19 merged to main (CI green: dotnet/contract-sync/pytest/gitleaks). Then a debug +
refactor pass on `refactor/audit-core-hardening`: full diagnostic battery (tsan + hardened
presets, clang-tidy, independent adversarial code review) found and fixed **4 CRITICAL
defects** in the merged audit_core:

- **CRIT-3 ensure_ascii parity** (worst): Python `json.dumps` defaults to
  `ensure_ascii=True`; the C++ `json_escape` passed UTF-8 through raw → cross-language hash
  divergence on ANY non-ASCII payload. Fixed with a full UTF-8 → `\uXXXX`/surrogate-pair
  encoder (malformed bytes → U+FFFD deterministically); non-ASCII Python oracle vector
  (`Müller—测试` → `fd9ceeec…`) pinned in test_canonical. Spec §3.1 hash definition corrected
  (it wrongly said "compact separators").
- **CRIT-1 NULL-column UB**: `sqlite3_column_text` NULL fed to `std::string` in
  verify_chain/certified_snapshot → crash on corrupted/adversarial DB. Fixed via NULL-safe
  `col_text`; NULL now reports "corrupted database" distinct from hash tampering (forensics).
- **CRIT-2 ignored prepare rcs**: NULL stmt → UB + stuck transactions. Fixed via RAII `Stmt`
  (checked prepare, unconditional finalize).
- **CRIT-4 dangling transaction on failed COMMIT**: bricked the Store. Fixed via RAII `Txn`
  (rollback on scope exit; failed COMMIT also rolls back).
- HIGH: `sqlite3_close_v2` (no handle leak); `sqlite3_open_v2` explicit mode; **H-21 writer
  mutex now actually implemented** (was a doc claim only). API: `Store::open(path, chain_id)`
  — chain_id ("project_id") is store-owned; events no longer abuse entity_id as chain scope.
- MED-2 ruled intentional: agents MAY create/promote at REVIEWED (spec §3.3 binds humans to
  APPROVED+ only); now documented in the header.
- CMake: `-fstack-clash-protection` gated on real compiler support (Apple clang ignores it).
- **Real libFuzzer run** (brew LLVM, direct compile — CMake+brew-LLVM has an SDK header
  mismatch): **1,354,073 coverage-guided executions / 60s under ASan+UBSan, zero crashes.**

Verified: 6/6 CTest × all four presets (dev/asan-ubsan/tsan/hardened) = 24/24; clang-format
clean; numeric gate ✓; ruff ✓; pytest 173 ✓.

## Next Actions

1. **Owner review + commit** the staged work (specs, CMake, `libs/audit_core/`, interop spike,
   ADR-0025). Scoped commits per repo policy (`git commit -- <paths>`).
2. **Phase 3 finish** — 3.5 agent-work chain + consolidation MVP; 3.6 libFuzzer reader; pin the
   cross-language SHA-256 parity fixture (spec §6); evaluator sign-off for Phase 3 exit.
3. **Two human decisions** from the 3.1 spec §8: licensed-professional identity boundary; confirm
   SHA-256 as the frozen chain hash.
4. **Phase 8 prereq** — Metal `bytesNoCopy` + realloc-under-render test to fully close R-11.
5. **Promote Phase 10 specs to ADRs** (0026 ABI / 0027 sync) after owner sign-off on their open
   questions.
6. **Business** — Budget ODA Sustaining membership before paid DWG distribution.
7. Every session: C-5.2 drift check; ruff/pytest/ctest green before merge.
