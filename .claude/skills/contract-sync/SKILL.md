---
name: contract-sync
description: Detect and fix drift between InGENeer's contract version constants, schemas, the command allowlist, and the intent catalog. Use before committing changes to schemas/, models.py, contracts.py, intent_validation.py, or INTENT_COMMAND_CATALOG.md, and when bumping any schema version.
---

# Contract Sync

InGENeer carries two contract version families that must each stay internally consistent:

- **Intent envelope** (`1.1.0`): `schemas/cad_intent_envelope.schema.json` (`schemaVersion.const`) ↔ `INTENT_SCHEMA_VERSION` in `orchestrator/src/ingenieer/models.py` ↔ `ALLOWED_COMMANDS`/`COMMAND_RISK` in `orchestrator/src/ingenieer/intent_validation.py` ↔ command table in `docs/INTENT_COMMAND_CATALOG.md`.
- **Project/wire contract** (`1.0.0`): `SCHEMA_VERSION` in `orchestrator/src/ingenieer/contracts.py`, imported (never redefined) by `orchestrator/src/ingenieer/wire.py`.

## Detect drift

Run the deterministic checker from the repo root:

```bash
python tools/checks/check_contract_sync.py
```

Exit 0 = in sync; exit 1 = drift (a FAIL row); WARN rows are non-blocking. Use `--json` for machine output. Report the table to the user. Do NOT edit the checker to make it pass — fix the underlying drift.

## Bumping the intent envelope version (coordinated change)

When the envelope contract changes, update ALL of these together in one commit:

1. `schemas/cad_intent_envelope.schema.json` → `properties.schemaVersion.const`.
2. `orchestrator/src/ingenieer/models.py` → `INTENT_SCHEMA_VERSION`.
3. `docs/INTENT_COMMAND_CATALOG.md` → version note / changelog row.
4. Tests asserting the version (search: `grep -rn "1\.1\.0\|INTENT_SCHEMA_VERSION" orchestrator/tests`).

Then re-run the checker and `python -m pytest -q` in `orchestrator/`.

## Adding a command

Prefer the `/intent-command-gen` skill, which scaffolds catalog + allowlist + risk + C# stub + tests together. After it runs, confirm with the checker. The allowlist must be a subset of the catalog (FAIL otherwise); a catalog row without an allowlist entry is a WARN (planned/disabled command).
