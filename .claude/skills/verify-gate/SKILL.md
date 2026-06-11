---
name: verify-gate
description: Run the full InGENeer pre-commit verification gate (ruff, pytest, dotnet build, domain-isolation greps, contract-sync) and report a pass/fail table. Use before committing or opening a PR in the InGENeer repo, or when the user asks to verify/check the build.
---

# Verify Gate

The InGENeer-specific gate. Distinct from the generic workspace `verify` skill: this one also
runs the domain-isolation greps (Rule 1) and the contract-sync drift check.

## Run

From the repo root:

```bash
./scripts/verify_gate.sh
```

Flags:
- `--quick` — ruff + contract-sync only (fast pre-edit sanity).
- `--skip-dotnet` — skip the .NET build on Python-only machines.

The script prints a table and exits non-zero on any failure. Relay the table to the user. On a
failure, drill into the failing step (e.g. run `cd orchestrator && python -m pytest -q` directly)
rather than re-running the whole gate.

## What it checks

1. `ruff check src tests` (orchestrator)
2. `python -m pytest -q` (orchestrator)
3. `dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release`
4. No geometry/B-rep symbols in `orchestrator/src` (Rule 1)
5. No LLM/AI symbols in `icad-addin/*.cs` (Rule 1)
6. `python tools/checks/check_contract_sync.py`

Do not commit if the gate fails (per repo CLAUDE.md verification gates).
