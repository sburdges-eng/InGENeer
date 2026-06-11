# Agent instructions (InGENeer)

This file is the **entrypoint for coding agents** (Cursor, Claude Code, Codex, etc.) working in this repository.

## Dev-root boundary

This file is project-local. `~/Dev` is only the workspace container; once a session is inside this repo, use this file plus `CLAUDE.md` and project docs as the active rules.

- Scope searches, build commands, tests, and contract checks to this repository unless a sibling repo is explicitly named.
- Load project-specific code intelligence, schema checks, orchestrator tooling, and CI checks from this repo's docs only.
- If work touches another folder inside `~/Dev`, switch to that folder's own `AGENTS.md` before editing there.
- Do not refresh or rely on a `~/Dev`-wide index as the source of truth for this project.

## Read first

1. [docs/governance/autonomation/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](docs/governance/autonomation/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md) — domain isolation, CAD threading, transactions, SOPs.
2. [docs/governance/autonomation/LAYERED_PRACTICE_PLAYBOOK.md](docs/governance/autonomation/LAYERED_PRACTICE_PLAYBOOK.md) — layers L0–L6, phases, versioning.
3. [docs/WORKSPACE_STANDARDS.md](docs/WORKSPACE_STANDARDS.md) — EditorConfig, Git attributes, ruff, .NET defaults, reuse checklist.
4. [docs/MODEL_LANGUAGE_ROUTING.md](docs/MODEL_LANGUAGE_ROUTING.md) — Python vs C# vs schema; air-gap handoffs.

## Cursor rules

Auto-attached under `docs/governance/autonomation/`, `schemas/`, `docs/`, `orchestrator/`, and root `README.md`:

- [.cursor/rules/autonomation-architecture.mdc](.cursor/rules/autonomation-architecture.mdc)
- [.cursor/rules/preferred-desktop-tooling.mdc](.cursor/rules/preferred-desktop-tooling.mdc)

## Tool-specific stubs

- [CLAUDE.md](CLAUDE.md) — Claude Code.
- [GEMINI.md](GEMINI.md) — Gemini / Google AI surfaces.

## Human workflow

- [CONTRIBUTING.md](CONTRIBUTING.md) — PR expectations, local checks (`ruff`, `pytest`, `dotnet build`).
- [docs/DEV_SETUP.md](docs/DEV_SETUP.md) — venv and toolchain versions.

## IDE scope

- Prefer opening **[InGENeer.code-workspace](InGENeer.code-workspace)** (or the `orchestrator/` / `icad-addin/` folder alone) instead of a huge parent directory. See [docs/PARENT_DEV_MONOREPO.md](docs/PARENT_DEV_MONOREPO.md) if this clone lives under `~/Dev/`.

## Worktrees (parallel lanes)

- [docs/roadmap/WORKTREE_INDEX.md](docs/roadmap/WORKTREE_INDEX.md) — branch naming and `tools/scripts/bootstrap_worktrees.sh`.

## Contract bumps

Intent envelope vs wire contract versions: see playbook **Versioning** table and [docs/INTENT_COMMAND_CATALOG.md](docs/INTENT_COMMAND_CATALOG.md). Change schema, catalog, and code constants together.
