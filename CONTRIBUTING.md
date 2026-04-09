# Contributing

## Before you change code

1. Read **[AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md)** and **[AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md](AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md)**.
2. Follow **[docs/MODEL_LANGUAGE_ROUTING.md](docs/MODEL_LANGUAGE_ROUTING.md)** for Python vs C# vs schema work (air-gap orchestrator and host execution contexts when appropriate).

## Local checks

See **[docs/DEV_SETUP.md](docs/DEV_SETUP.md)**. At minimum before opening a PR:

- **Python:** from `orchestrator/`, `pip install -e ".[dev]"` then `python -m pytest -q`
- **.NET:** from repo root, `dotnet build icad-addin/InGENeer.IcadAddin.slnx`

## Pull requests

- Prefer **small, focused** PRs with a clear intent (one concern per PR when practical).
- If you change the **intent envelope** or **wire contract**, update **schema**, **catalog**, and **version constants** together; note the bump in the PR description.
- Do **not** remove error handling, rollback patterns, audit logging, or fingerprint checks to “clean up” unless explicitly agreed and replaced with equivalent guarantees.

## Licensing

By contributing, you agree your contributions are licensed under the same terms as this project ([LICENSE](LICENSE)).
