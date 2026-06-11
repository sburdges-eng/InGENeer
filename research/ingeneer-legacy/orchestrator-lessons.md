# Legacy InGENeer Orchestrator — Extraction Notes

**Status:** Extracted 2026-06-11 (Phase 2.5.3)  
**Source:** `orchestrator/` in this repo (legacy exception path per ADR-0022)  
**Stage 3 disposition:** **Evaluate** for `libs/ai_core` interfaces + closed Aura Intelligence runtime — not a verbatim port

## Pipeline phases

Source: `orchestrator/src/ingenieer/orchestrator.py`

```
validate_intent → sync_baseline → dispatch_execute → verify_result
```

| Phase | Responsibility |
|-------|----------------|
| `validate_intent` | JSON schema + business rules on `CadIntentEnvelope`; high-risk commands need human confirmation token |
| `sync_baseline` | HTTP GET model fingerprint; stale-drawing guard before mutation |
| `dispatch_execute` | POST intent to bridge (mock or HTTP loopback / future iCAD host) |
| `verify_result` | Compare post-execution fingerprint; fail closed on mismatch |

## Air-gap boundaries (preserve)

- Python: validation, audit, transport only — **no B-rep geometry** (governance rules)
- C# host: deterministic execution only — **no LLM** in `icad-addin/`
- Contract handoff: `schemas/` + `tools/scripts/copy_schema_handoff.sh`

## Artifacts worth promoting

| Component | Path | Notes |
|-----------|------|-------|
| Audit logger | `ingenieer/audit.py` | SHA-256 JSONL — design input for `audit_core` |
| Intent validation | `ingenieer/intent_validation.py` | Catalog-driven; aligns with `schemas/` |
| Bridge client | `ingenieer/bridge_client.py` | Mock + HTTP; L5 transport |
| Wire contracts | `ingenieer/contracts.py`, `wire.py` | Version constants must move with schema bumps |
| Batch pipeline | `ingenieer/batch.py`, CLI | Multi-intent orchestration |

## WIP / evaluate later

- `tests/test_intent_generator.py` — LLM intent generator plan exists; module not landed on current branch
- Intent generator belongs in **closed** Aura Intelligence, not open Core

## ai_core candidate mapping

Open Core (`libs/ai_core`): **proposal API interfaces** — model-agnostic, authority-safe (AI_PROPOSED only).

Closed: model weights, agent orchestration, NL→intent LLM (`orchestrator` Python may remain in `tools/orchestrator/` as process tooling).

## Verification baseline

- 173+ pytest cases (`docs/specs/feature_list.json`)
- 6 .NET bridge tests
- Integration: `test_roundtrip_integration.py` (C# loopback host)
