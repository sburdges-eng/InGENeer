# InGENeer orchestrator (`ingenieer`)

Python package for **routing, validation, and transport** only. It does not execute CAD APIs or compute B-rep geometry (see `docs/governance/autonomation/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md`).

## Install (editable)

```bash
cd orchestrator
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -e ".[dev]"
```

## Layout

| Module | Role |
|--------|------|
| `contracts.py` | Canonical wire envelopes (`build_contract_payload`, `validate_contract_payload`) |
| `audit.py` | Append-only JSONL audit log with SHA-256 chaining |
| `models.py` | Pydantic models for intent envelope and pipeline results |
| `orchestrator.py` | Phase runner: `validate_intent` → `sync_baseline` → `dispatch_execute` → `verify_result` |
| `bridge_client.py` | `mock` / `http` clients for `docs/BRIDGE_TRANSPORT.md` |
| `intent_validation.py` | JSON Schema + MVP command allowlist |
| `cli.py` | `ingenieer-run` entrypoint (one intent JSON → pipeline summary JSON) |
| `wire.py` | CAD bridge result wrapper for responses back to the orchestrator |

## Tests

```bash
pytest -q
```

## CLI

```bash
ingenieer-run path/to/intent.json --output-dir ./out --audit-dir ./audit_logs
```

Use `--config orchestrator.json` for `OrchestratorConfig` overrides (e.g. `bridge.mode`, `http_base_url`, `http_max_retries`, `max_verification_attempts`).

Other flags:

- `--dry-run` / `--preview` — force `executionMode` before validation.
- `--i-confirm TOKEN` — set `humanConfirmationToken` for high-risk `execute` intents.
- `--print-plan` — run through `validate_intent` only, print normalized intent + schema path (no dispatch).
