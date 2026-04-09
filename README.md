# InGENeer

**Project path:** `~/Dev/InGENeer` (local Dev directory).

## Architecture rules (AutonomAtIon)

**[AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md)** — domain isolation, CAD threading, transactions, Cursor SOPs, naming (`InGENeer`, `AIrchetect`, `AutonomAtIon`).

**[AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md](AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md)** — layer checklists (L0–L6), pipeline phase map, versioning. Cursor auto-attaches **`.cursor/rules/autonomation-architecture.mdc`** when you work under `AutonomAtIon/`, `schemas/`, `docs/`, `orchestrator/`, or this README.

**[docs/AI_ASSISTANT_BEST_PRACTICES.md](docs/AI_ASSISTANT_BEST_PRACTICES.md)** — when to lean on **Claude Max**, **Gemini Ultra**, or **Codex Pro** for this repo (plus shared Cursor hygiene).

## Orchestrator (Python)

- **Package:** [`orchestrator/`](orchestrator/) — install with `pip install -e ".[dev]"` from that directory.  
- **Contracts + audit + phase runner:** `src/ingenieer/` (`contracts`, `audit`, `orchestrator`, `wire`, `models`).  
- **Intent JSON Schema:** [`schemas/cad_intent_envelope.schema.json`](schemas/cad_intent_envelope.schema.json).  
- **Command catalog:** [`docs/INTENT_COMMAND_CATALOG.md`](docs/INTENT_COMMAND_CATALOG.md).
- **Bridge transport (HTTP + mock):** [`docs/BRIDGE_TRANSPORT.md`](docs/BRIDGE_TRANSPORT.md).
- **CLI:** after `pip install -e ".[dev]"`, run `ingenieer-run --help` from the `orchestrator/` directory.

The **reference C# host spike** lives under [`icad-addin/`](icad-addin/) (wire DTOs and MVP command stubs). Python uses **`bridge.mode`** `mock` (default) or `http` against `GET/POST /v1/...` on loopback.
