# Model × language routing (InGENeer)

Rules for **which assistant or tier to use** against **which languages and folders** in this repo, and how models should **hand off** work without breaking domain isolation.

**Companion:** [AI_ASSISTANT_BEST_PRACTICES.md](AI_ASSISTANT_BEST_PRACTICES.md) (when to prefer Claude Max vs Gemini Ultra vs Codex Pro for *task shape*). This document adds **language and path** constraints.

**Authority:** [AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](../AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md), [AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md](../AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md).

---

## Invariants (every model, every tier)

1. **Python (`orchestrator/`, repo-root `scripts/*.py`):** orchestration, validation, transport, audit only. **No B-rep geometry** and no proprietary CAD API calls.
2. **C# (`icad-addin/`):** deterministic bridge and host-side execution. **No LLM calls** in shipping add-in code. **No guessed** IntelliCAD/Carlson/Autodesk APIs—use official docs or `// TODO` with a snippet request.
3. **JSON Schema (`schemas/`):** must stay aligned with Pydantic/models and [INTENT_COMMAND_CATALOG.md](INTENT_COMMAND_CATALOG.md); bump `schemaVersion` on breaking envelope changes.
4. **Air-gap (SOP 2):** do not ask any model to “do orchestrator + real C# host execution” in one shot. Pass **schema + sample envelopes** across the boundary the same way production does ([scripts/copy_schema_handoff.sh](../scripts/copy_schema_handoff.sh)).
5. **Preserve guards:** do not remove rollback, logging, fingerprint checks, or transaction discipline to “simplify” diffs.

---

## Language and artifact map

| Area | Languages | Role |
|------|-----------|------|
| Orchestrator | Python | Phases, contracts, wire, bridge client, CLI, tests under `orchestrator/tests/` |
| Contracts at rest | JSON | `schemas/*.schema.json` |
| Narrative rules | Markdown | `AutonomAtIon/`, `docs/`, root `README.md` |
| Host reference | C#, `.csproj`, solution | `icad-addin/` (DTOs, loopback host, future iCAD add-in) |
| Glue | Shell | `scripts/*.sh` (handoff, local automation) |

---

## Routing matrix (primary owner per cell)

Legend: **Primary** = default choice for that kind of edit. **Secondary** = good with extra grounding. **Avoid** = likely to violate air-gap or hallucinate APIs without tight constraints.

| Language / focus | Claude Max | Gemini Ultra | Codex Pro | Cloud / IDE agent |
|------------------|------------|--------------|-----------|-------------------|
| Python orchestrator + tests | Secondary | Primary (large refactors across many files) | **Primary** (small, test-backed changes) | Secondary (follow a checklist) |
| JSON Schema + catalog sync | Primary | Primary | Secondary | Avoid unless given exact diff target |
| Markdown (architecture, playbook, intent docs) | **Primary** | Primary | Secondary | Secondary |
| C# `icad-addin/` (API-real code) | **Primary** (with @ doc) | Primary (with @ doc) | Avoid without pasted API | Avoid without pasted API |
| C# boilerplate (DTOs mirroring agreed JSON) | Secondary | Primary | Primary (given sample JSON + field list) | Secondary |
| Shell scripts | Secondary | Secondary | **Primary** (simple) | Secondary |

**Cloud / IDE agent** means the default **Cursor Agent**, **Composer**, or similar **multi-file cloud agents**—treat them as **strong for breadth**, **weak for proprietary C# APIs** unless you scope the workspace and paste vendor docs.

---

## By model: relationship to languages

### Claude Max

- **Best:** architecture, policy, contracts versioning, threading/transaction stories, **C# when vendor docs are attached**.
- **Python:** excellent for phase design and error semantics; pair with tests.
- **Markdown:** canonical wording for rules and catalogs.
- **Risk:** still must not invent CAD APIs—require doc citation for `icad-addin/`.

### Gemini Ultra

- **Best:** **cross-cutting consistency** (schema + `ingenieer` + tests + docs in one pass), comparison tables, long traces.
- **Python:** strong for repo-wide refactors when tests are the contract.
- **C#:** use only with **explicit doc grounding**; prefer narrowing context to `icad-addin/` alone for execution code.

### Codex Pro

- **Best:** **Python** implementation loops, `pytest`, typing, mechanical refactors in `orchestrator/src/ingenieer/`.
- **JSON / C# DTOs:** good when the **JSON shape is fixed** and the task is mirroring or validation glue.
- **Avoid:** designing new host API usage from memory; **avoid** mixing `orchestrator/` and `icad-addin/` execution logic in one prompt.

### Cloud / IDE agent (Cursor Agent, Composer, etc.)

- **Best:** scaffolding, multi-file edits **within one domain** (only Python *or* only docs *or* only C# stubs).
- **Practice:** open **separate windows** for orchestrator vs CAD plugin when possible; paste **file paths** and **acceptance commands** (`pytest -q`, `dotnet build`).
- **Risk:** context bleed across Python and C#—enforce air-gap manually.

---

## Handoffs between models (and humans)

1. **Contract change (JSON ↔ Python ↔ catalog):**  
   Owner: **Claude Max or Gemini** for shape/version story → **Codex** for implementation + tests → human review of `schemaVersion` / `SCHEMA_VERSION` bumps.

2. **New bridge or phase behavior (Python only):**  
   **Codex** with tests; escalate to **Claude Max** if rollback, audit, or phase ordering is unclear.

3. **Host-side behavior (C#):**  
   **Claude Max or Gemini** with vendor docs → minimal **Codex** pass only for mechanical edits **after** signatures are fixed.

4. **Doc-only clarification:** any tier; prefer **Gemini** for wide consistency, **Claude Max** for normative “must/must not” wording.

---

## Quick decision tree

1. Touching **`icad-addin/`** with real host APIs? → **Claude Max or Gemini + official docs**; not a blind Codex pass.
2. Only **`orchestrator/`** + tests, behavior specified? → **Codex Pro** (or fastest capable editor model).
3. Changing **schema + many files**? → **Gemini Ultra** (or Claude Max), then narrow follow-up for tests.
4. Unsure about **boundary** (orchestrator vs host)? → **Claude Max** first; implement later in a scoped thread.

---

## Document control

- **Owner:** project maintainer.
- **When to update:** new languages (e.g. FreeCAD worker), new packages under repo, or new default models in team tooling. Keep in sync with [AI_ASSISTANT_BEST_PRACTICES.md](AI_ASSISTANT_BEST_PRACTICES.md).
