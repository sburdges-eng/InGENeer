# Drift Report — DRIFT-20260611-stage1-relocation-timing

**Status:** RESOLVED  
**Date:** 2026-06-11  
**Author:** implementation agent (Phase 2 gate)  
**Session / lane:** `feat/llm-intent-generator` / main coordinator

## Summary

Phase 2 (Stage 1 monorepo restructure) cannot start until we resolve a **direct timing conflict**: C-5.5 and ADR-0019 require the repository top level to contain **exactly** five directories at Stage 1, while MIGRATION_PLAN Stage 1 explicitly keeps `orchestrator/`, `icad-addin/`, and `schemas/` at the legacy top-level paths until **Stages 2–3**. The repo today has **eleven+** top-level product/governance trees plus dotfiles and artifacts. Work on `git mv` slices is **stopped** pending a human ruling on which document governs relocation timing.

## Grounding conflict

| Source | ID / section | Verbatim or summary |
|--------|--------------|---------------------|
| CONSTRAINTS | **C-5.5** | Monorepo top level is exactly: `apps/ libs/ research/ docs/ tools/` — **nothing else** (Stage 1). |
| ADR-0019 | Layout (Stage 1) | “nothing else at top level”; Stage 1 creates the five-dir layout. |
| ADR-0019 | Consequences | `orchestrator/`, `icad-addin/`, `schemas/` restructured into the layout during **Stage 2–3**. |
| MIGRATION_PLAN | Stage 1 | Top level becomes exactly five dirs. **Existing `orchestrator/`, `icad-addin/`, `schemas/` are relocated during Stages 2–3, not deleted.** |
| MIGRATION_PLAN | Stage 2–3 | Stage 2 = knowledge extraction; Stage 3 = promote proven code only. |
| Agentic plan | Phase 2 | “File a drift report and obtain a human ruling on the relocation timing before moving those three trees.” |
| handoff.md | Pending Task 3 | Stage 1: restructure monorepo top level. |
| WORKSPACE_SCOPE_MAP | Note | Scoped workspaces under `docs/workspaces/` avoid new top-level dirs **until** migration; must be updated after Stage 1. |

## Current top level (2026-06-11)

**Target five (partial):** `docs/` ✓ · `tools/` ✓ (contains `tools/agentic/` from Phase 0) · `apps/` ✗ · `libs/` ✗ · `research/` ✗

**Legacy / extra (non-exhaustive):**

| Path | Role | Natural target in five-dir layout |
|------|------|-----------------------------------|
| `orchestrator/` | Python L1–L5 orchestrator (shipping) | `tools/orchestrator/` (process tooling) **or** promote slice to `libs/ai_core/` interfaces only in Stage 3 |
| `icad-addin/` | C# iCAD / loopback host (ADR-0008 bridge) | `apps/icad-bridge/` (transitional host) **or** `tools/icad-addin-host/` |
| `schemas/` | Intent envelope + params JSON Schema | `docs/contracts/schemas/` (contract docs) **or** `libs/contracts/schemas/` (if treated as published API artifact) |
| `docs/governance/autonomation/` | Governance canon (rules, playbook) | `docs/governance/autonomation/` |
| `tools/scripts/` | Repo ops, handoff copy, worktrees | `tools/scripts/` |
| `audit_logs/`, `worktrees/`, research PDFs, etc. | Local / artifact / gitignored | Stay gitignored or move under `tools/` / delete from tree |

**CI / editor paths at risk on any move:** `.github/workflows/ci.yml` (`orchestrator/`, `icad-addin/`), `InGENeer.code-workspace`, `docs/workspaces/*.code-workspace`, `CONTRIBUTING.md` quick checks, `tools/scripts/copy_schema_handoff.sh`, `orchestrator/pyproject.toml`, dotnet solution paths.

## Observed drift

Executing Phase 2.1 (`git mv` toward five-dir layout) **as written in C-5.5** requires either:

1. **Immediate relocation** of `orchestrator/`, `icad-addin/`, `schemas/` (and likely `docs/governance/autonomation/`, `tools/scripts/`) in the same phase — **contradicts** MIGRATION_PLAN Stage 1 footnote (defer to Stages 2–3), **or**
2. **Creating only** `apps/`, `libs/`, `research/` skeletons while leaving legacy trees at top level — **violates** C-5.5 “nothing else” **now**, including the already-created `tools/agentic/` coexistence with legacy siblings.

This is a **documentation conflict**, not an implementation bug. Both statements were approved in the same baseline package.

## Proposed resolution options

### Option A — **Strict C-5.5 now** (relocate legacy in Phase 2.1)

**Align implementation to C-5.5; amend MIGRATION_PLAN timing footnote.**

| Move | Destination (proposed) |
|------|------------------------|
| `orchestrator/` | `tools/orchestrator/` |
| `icad-addin/` | `apps/icad-bridge/` |
| `schemas/` | `docs/contracts/schemas/` |
| `docs/governance/autonomation/` | `docs/governance/autonomation/` |
| `tools/scripts/` | `tools/scripts/` |
| (new) | `apps/desktop/` skeleton, `libs/*` skeleton dirs, `research/` skeleton |

- **Pros:** Top level compliant immediately; CI and workspaces updated once; matches ADR-0019 diagram literally.
- **Cons:** Large path churn before Stage 2 knowledge extraction; every agent/workspace doc must update in the same commit series; **does not** wait for Stage 3 triage on whether orchestrator code promotes to `libs/ai_core/`.

### Option B — **Deferred legacy hold** (amend C-5.5)

**Amend C-5.5** via ADR-0022 to define **Stage 1a / 1b**:

- **1a (now):** Create `apps/`, `libs/`, `research/` skeletons; consolidate `tools/scripts/` → `tools/scripts/`; move `docs/governance/autonomation/` → `docs/governance/autonomation/`.
- **1b (explicit exception list):** Until **end of Stage 3**, these may remain at legacy top-level paths: `orchestrator/`, `icad-addin/`, `schemas/`.
- **Exit criterion:** After Stage 3 triage, legacy paths empty or relocated; top level then strictly five dirs.

- **Pros:** Matches MIGRATION_PLAN footnote; minimizes churn during knowledge extraction (Phase 2.5); orchestrator/schemas paths stable for current CI and Cursor workspaces.
- **Cons:** C-5.5 is weakened temporarily; requires discipline so “temporary” does not become permanent (risk R-1 scope creep).

### Option C — **Hybrid (recommended)**

**Adopt Option A for governance + contracts; defer only shipping code trees.**

| Phase 2.1 slice (immediate) | Defer to Stage 3 triage |
|-----------------------------|-------------------------|
| Create `apps/`, `libs/`, `research/` skeletons | `orchestrator/` → relocate after Stage 2.5 + promotion decision |
| `docs/governance/autonomation/` → `docs/governance/autonomation/` | `icad-addin/` → relocate when host workspace paths are batch-updated |
| `tools/scripts/` → `tools/scripts/` | `schemas/` → relocate with contract-lane decision (`docs/contracts/` vs published `libs/contracts/`) |
| Amend C-5.5: allow **only** `orchestrator/`, `icad-addin/`, `schemas/` as named legacy exceptions until Stage 3 exit | |

Record ruling in **ADR-0022**; update MIGRATION_PLAN Stage 1 bullet to match; refresh `WORKSPACE_SCOPE_MAP.md` and `docs/workspaces/*.code-workspace` paths per slice.

- **Pros:** Removes most top-level clutter now; keeps stable paths for active Python/C#/schema CI during Phase 2.5 knowledge work; bounded exception list with exit gate.
- **Cons:** Still not “exactly five” until Stage 3; needs ADR amendment.

## Human decision required

> _Approver fills this section._

Choose **A**, **B**, or **C** (or specify a variant). If **C**, confirm the three-tree exception list and whether `schemas/` should land under `docs/contracts/schemas/` or elsewhere.

**Decision:** **Option C (hybrid)** — approved 2026-06-11.  
**Date:** 2026-06-11  
**Follow-up actions:**

- [x] Record ADR-0022 (Stage 1 relocation ruling)
- [x] Update `MIGRATION_PLAN.md` Stage 1 wording to match ruling
- [x] Update `CONSTRAINTS.md` C-5.5 (ADR-0022 exception table)
- [x] Execute Phase 2.1 Stage 1a `git mv` slices (governance, scripts, skeletons)
- [x] Refresh workspace files + doc path references

## References

- [ADR-0019](../adr/ADR-0019-monorepo-layout-migration.md)
- [MIGRATION_PLAN.md](../architecture/MIGRATION_PLAN.md)
- [CONSTRAINTS.md](../architecture/CONSTRAINTS.md) C-5.5
- [Agentic plan Phase 2](../superpowers/plans/2026-06-11-agentic-work-memory-hardening.md) §9
- [WORKSPACE_SCOPE_MAP.md](../WORKSPACE_SCOPE_MAP.md)
- Related: Phase 0 placed `tools/agentic/` at top level (pre-ruling)
