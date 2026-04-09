# Contributing

## Before you change code

1. Read **[AGENTS.md](AGENTS.md)** (agent entrypoint) and **[AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md)** / **[AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md](AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md)**.
2. Follow **[docs/MODEL_LANGUAGE_ROUTING.md](docs/MODEL_LANGUAGE_ROUTING.md)** for Python vs C# vs schema work (air-gap orchestrator and host execution contexts when appropriate).
3. Skim **[docs/WORKSPACE_STANDARDS.md](docs/WORKSPACE_STANDARDS.md)** for repo-wide EditorConfig, Git attributes, shared .NET defaults, and how process rules map to tooling.

## Local checks

See **[docs/DEV_SETUP.md](docs/DEV_SETUP.md)**. At minimum before opening a PR:

- **Python:** from `orchestrator/`, `pip install -e ".[dev]"` then `ruff check src tests` and `python -m pytest -q`
- **.NET:** from repo root, `dotnet build icad-addin/InGENeer.IcadAddin.slnx`

Editors should pick up **[`.editorconfig`](.editorconfig)** automatically; **[`.gitattributes`](.gitattributes)** keeps LF endings consistent for CI and reviews.

## Pull requests

- Prefer **small, focused** PRs with a clear intent (one concern per PR when practical).
- If you change the **intent envelope** or **wire contract**, update **schema**, **catalog**, and **version constants** together; note the bump in the PR description.
- Do **not** remove error handling, rollback patterns, audit logging, or fingerprint checks to “clean up” unless explicitly agreed and replaced with equivalent guarantees.

## Licensing

By contributing, you agree your contributions are licensed under the same terms as this project ([LICENSE](LICENSE)).
