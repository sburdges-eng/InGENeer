# Claude Code — InGENeer

You are in the **InGENeer** repo: Python orchestrator (`orchestrator/`), C# bridge spike (`icad-addin/`), JSON schemas (`schemas/`), and AutonomAtIon governance docs.

## Before coding

1. Read [AGENTS.md](AGENTS.md) for the full reading order and Cursor rules.
2. Respect **domain isolation**: no B-rep geometry or proprietary CAD APIs in Python; no LLM in C# execution paths; use `// TODO` + docs for uncertain host APIs.

## Quick checks (from repo root)

```bash
cd orchestrator && pip install -e ".[dev]" && ruff check src tests && python -m pytest -q
dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release
```

## Parent `~/Dev` layout

If this clone sits next to other projects under `~/Dev`, see [docs/PARENT_DEV_MONOREPO.md](docs/PARENT_DEV_MONOREPO.md). Do not assume sibling repos are present or stable.
