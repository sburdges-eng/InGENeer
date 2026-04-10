# InGENeer

**Project path:** `~/Dev/InGENeer` (local Dev directory).

## Governance and local dev

- **[AGENTS.md](AGENTS.md)** — entrypoint for AI coding agents (read this first). Shorter pointers: [CLAUDE.md](CLAUDE.md), [GEMINI.md](GEMINI.md).
- **[InGENeer.code-workspace](InGENeer.code-workspace)** — open in Cursor/VS Code for a scoped multi-root workspace (orchestrator + `icad-addin` + schemas + docs).
- **[docs/DEV_SETUP.md](docs/DEV_SETUP.md)** — Python venv, `pytest`, `dotnet build`.
- **[docs/PARENT_DEV_MONOREPO.md](docs/PARENT_DEV_MONOREPO.md)** — if this repo lives under `~/Dev/` next to sibling projects.
- **[docs/WORKSPACE_STANDARDS.md](docs/WORKSPACE_STANDARDS.md)** — shared EditorConfig, Git attributes, .NET `Directory.Build.props`, Python ruff, and how to reuse standards in new repos.
- **[docs/REPOSITORY_SETUP.md](docs/REPOSITORY_SETUP.md)** — remote, branch protection, Dependabot, security toggles after you add a host.
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — PR expectations and architecture links.
- **[SECURITY.md](SECURITY.md)** — how to report issues.
- **[LICENSE](LICENSE)** — MIT (replace if you need a different license).

## Architecture rules (AutonomAtIon)

**[AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md)** — domain isolation, CAD threading, transactions, Cursor SOPs, naming (`InGENeer`, `AIrchetect`, `AutonomAtIon`).

**[AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md](AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md)** — layer checklists (L0–L6), pipeline phase map, versioning. Cursor auto-attaches **`.cursor/rules/autonomation-architecture.mdc`** when you work under `AutonomAtIon/`, `schemas/`, `docs/`, `orchestrator/`, or this README.

**[docs/AI_ASSISTANT_BEST_PRACTICES.md](docs/AI_ASSISTANT_BEST_PRACTICES.md)** — when to lean on **Claude Max**, **Gemini Ultra**, or **Codex Pro** for this repo (plus shared Cursor hygiene).  
**[docs/MODEL_LANGUAGE_ROUTING.md](docs/MODEL_LANGUAGE_ROUTING.md)** — model × **language/folder** rules, routing matrix, and handoffs (Python orchestrator vs C# `icad-addin/` vs schema/docs).

## Orchestrator (Python)

- **Package:** [`orchestrator/`](orchestrator/) — install with `pip install -e ".[dev]"` from that directory.  
- **Contracts + audit + phase runner:** `src/ingenieer/` (`contracts`, `audit`, `orchestrator`, `wire`, `models`).  
- **Intent JSON Schema:** [`schemas/cad_intent_envelope.schema.json`](schemas/cad_intent_envelope.schema.json).  
- **Command catalog:** [`docs/INTENT_COMMAND_CATALOG.md`](docs/INTENT_COMMAND_CATALOG.md).
- **Bridge transport (HTTP + mock):** [`docs/BRIDGE_TRANSPORT.md`](docs/BRIDGE_TRANSPORT.md).
- **CLI:** after `pip install -e ".[dev]"`, run `ingenieer-run --help` from the `orchestrator/` directory.

The **reference C# host spike** lives under [`icad-addin/`](icad-addin/) (wire DTOs and MVP command stubs). Python uses **`bridge.mode`** `mock` (default) or `http` against `GET/POST /v1/...` on loopback. Intent envelope is **`schemaVersion` `1.1.0`** (`executionMode`, optional human confirmation for high-risk commands); use [`scripts/copy_schema_handoff.sh`](scripts/copy_schema_handoff.sh) to bundle schema + samples for CAD-side workspaces.
