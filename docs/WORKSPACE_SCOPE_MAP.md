# Workspace Scope Map

**Status:** Active workspace routing guide.  
**Purpose:** Tell humans and agents which Cursor workspace to open for each kind of InGENeer work, so context stays scoped and domain rules stay visible.

This supplements `InGENeer.code-workspace` at the repo root. The root workspace remains the default mixed overview. For focused work, prefer one of the scoped workspaces in `docs/workspaces/`.

## Non-negotiable routing rules

1. Never open `~/Dev` as the IDE workspace for InGENeer work.
2. Open the narrowest workspace that matches the task.
3. Keep Python orchestration and CAD host execution air-gapped unless the task is explicitly a schema/contract handoff.
4. Follow `docs/handoff.md`: implementation remains gated by C-5.1 unless the human grants authorization.
5. Before implementation tasks, apply C-5.2: validate against `ARCHITECTURE.md`, `REQUIREMENTS.md`, `CONSTRAINTS.md`, `handoff.md`, and `adr/`; on drift, stop and write a drift report.
6. Workspace files are stored under `docs/workspaces/` to avoid adding new top-level directories before the approved monorepo migration.

## Workspace chooser

| Open this workspace | Use for | Primary folders | Avoid doing here |
|---|---|---|---|
| `InGENeer.code-workspace` | Current broad repo overview; manual navigation; short cross-cutting checks | orchestrator, icad-addin, schemas, docs, apps, libs, research, tools | Long-running agent work; broad parent scans; unrelated sibling repos |
| `docs/workspaces/InGENeer.docs-and-architecture.code-workspace` | Architecture package review, ADR updates, handoff/doc-only planning | `docs/`, `docs/governance/autonomation/` | Code edits; host/API implementation |
| `docs/workspaces/InGENeer.agentic-planning.code-workspace` | Agentic plans/specs/research, C-5.2 drift reports, memory-work planning | `docs/superpowers/`, `docs/architecture/`, `docs/adr/`, `docs/workspaces/`, `docs/governance/autonomation/` | Production code changes; schema version bumps |
| `docs/workspaces/InGENeer.orchestrator-python.code-workspace` | Python orchestrator L1-L5 work: models, validation, wire contracts, audit, bridge client, tests | `orchestrator/`, `schemas/`, `docs/`, `docs/governance/autonomation/`, `tools/scripts/` | CAD host mutation logic; proprietary CAD API guessing |
| `docs/workspaces/InGENeer.icad-addin-host.code-workspace` | C# iCAD bridge / host execution work after API docs are available | `icad-addin/`, `schemas/`, `docs/`, `docs/governance/autonomation/` | LLM logic; Python orchestrator refactors; undocumented CAD API calls |
| `docs/workspaces/InGENeer.contracts-and-schemas.code-workspace` | Intent envelope, JSON schema, generated API reference, DTO handoff, version-bump reviews | `schemas/`, `orchestrator/src/ingenieer`, `orchestrator/tests`, `docs/`, `tools/scripts/` | Host execution code; broad architecture rewrites |
| `docs/workspaces/InGENeer.automation-rules.code-workspace` | Governance rule edits, Cursor rule review, routing/playbook updates | `docs/governance/autonomation/`, `docs/`, `.cursor/`, `tools/scripts/` | Product implementation; schema bumps without code/tests |
| `docs/workspaces/InGENeer.repo-ops.code-workspace` | Worktree index, CI/task metadata, editor/task settings, repo-ops documentation | `docs/roadmap/`, `docs/`, `tools/scripts/`, `.github/`, `.vscode/` | Engine/product code changes |

## Decision tree

- Need to read or update ADRs, architecture docs, risk registers, or handoff?  
  Open `docs/workspaces/InGENeer.docs-and-architecture.code-workspace`.

- Need to work on agent plans, specs, drift reports, or memory/hardening planning?  
  Open `docs/workspaces/InGENeer.agentic-planning.code-workspace`.

- Need to change Python orchestrator behavior or tests?  
  Open `docs/workspaces/InGENeer.orchestrator-python.code-workspace`.

- Need to change C# iCAD host bridge behavior?  
  Open `docs/workspaces/InGENeer.icad-addin-host.code-workspace`; require official API docs for real host calls.

- Need to change command schemas, catalog, wire contract constants, or DTO handoff?  
  Open `docs/workspaces/InGENeer.contracts-and-schemas.code-workspace`; update schema, catalog, and code constants together.

- Need to edit governance rules, Cursor rules, or model-routing docs?  
  Open `docs/workspaces/InGENeer.automation-rules.code-workspace`.

- Need to work on CI, worktree layout, or editor/task settings?  
  Open `docs/workspaces/InGENeer.repo-ops.code-workspace`.

## Air-gap handoff checklist

When moving from orchestrator/schema work to host execution work:

1. In the contracts workspace, update or verify `schemas/cad_intent_envelope.schema.json`, `schemas/params/*.schema.json`, `docs/INTENT_COMMAND_CATALOG.md`, and `docs/INTENT_COMMAND_API_REFERENCE.md`.
2. Run the orchestrator contract checks from the orchestrator workspace.
3. Use `tools/scripts/copy_schema_handoff.sh` to produce the schema + sample JSON handoff.
4. Open the host workspace in a separate Cursor window.
5. Generate or update deterministic DTO / execution code only from the handoff and official host API docs.

## Tracking table

| Workspace | Owner / lane | Last reviewed | Notes |
|---|---|---|---|
| Root overview | Human / coordinator | 2026-06-11 | Existing broad multi-root workspace. |
| Docs + architecture | Architecture/doc lane | 2026-06-11 | Safe for pre-implementation doc review. |
| Agentic planning | Agent-planning lane | 2026-06-11 | Use for hardening plans and drift reports. |
| Orchestrator Python | Python L1-L5 lane | 2026-06-11 | Includes schemas/docs for context; no host execution. |
| iCAD host | C# L6 lane | 2026-06-11 | Requires vendor API docs; no LLM logic. |
| Contracts + schemas | Contract/version lane | 2026-06-11 | Schema/catalog/constants move together. |
| Automation rules | Governance lane | 2026-06-11 | Rules/playbook/Cursor policy only. |
| Repo ops | Repo-maintenance lane | 2026-06-11 | CI/worktrees/editor settings. |

## Open questions / maintenance

- After the approved monorepo Stage 1 migration, revisit every `docs/workspaces/*.code-workspace` path and update this map.
- If `apps/`, `libs/`, `research/`, or `tools/` are created, add new scoped workspaces for app/desktop, libs/core, research, and tools rather than expanding the existing workspaces.
- **Resolved:** Stage 1a per [ADR-0022](adr/ADR-0022-stage1-relocation-ruling.md); legacy exceptions `orchestrator/`, `icad-addin/`, `schemas/` until Stage 3.
