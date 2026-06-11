# ADR-0022 — Stage 1 Relocation Ruling (Option C)

**Status:** Accepted  
**Date:** 2026-06-11  
**Resolves:** [DRIFT-20260611-stage1-relocation-timing.md](../drift/DRIFT-20260611-stage1-relocation-timing.md)  
**Amends:** C-5.5 (exception table), MIGRATION_PLAN Stage 1, ADR-0019 consequences timing.

## Context

C-5.5 required exactly five top-level directories at Stage 1. MIGRATION_PLAN deferred relocation of `orchestrator/`, `icad-addin/`, and `schemas/` until Stages 2–3. Human selected **Option C (hybrid)** from the drift report.

## Decision

### Stage 1a (execute now — Phase 2.1)

1. Create skeleton trees: `apps/desktop/`, `libs/*` (nine engine placeholders), `research/{jepa,boundary_ai,legal_ai}/`.
2. `git mv docs/governance/autonomation/` → `docs/governance/autonomation/`.
3. `git mv scripts/` → `tools/scripts/` (alongside existing `tools/agentic/`).
4. Update CI, workspaces, agent entrypoints, and Cursor rule globs to new paths.

### Legacy exception list (until Stage 3 exit)

These **may remain at legacy top-level paths** during Stage 2 knowledge extraction and Stage 3 triage:

| Path | Relocate when | Proposed destination (Stage 3) |
|------|---------------|--------------------------------|
| `orchestrator/` | After Stage 2.5 + promotion decision | `tools/orchestrator/` (default) |
| `icad-addin/` | Host workspace batch update | `apps/icad-bridge/` (transitional) |
| `schemas/` | Contract-lane decision | `docs/contracts/schemas/` (default) |

### Stage 1b exit criterion

Top level is **strictly** `apps/ libs/ research/ docs/ tools/` plus dotfiles and gitignored artifacts only when the three legacy paths are empty or relocated and documented in handoff.

## Consequences

- C-5.5 is satisfied **in staged form**; agents must not add new top-level product directories.
- Phase 2.5 may proceed without orchestrator/schema path churn.
- `WORKSPACE_SCOPE_MAP.md` and `docs/workspaces/*` reference `docs/governance/autonomation/` and `tools/scripts/`.
