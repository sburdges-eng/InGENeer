# InGENeer Project Skills & Tools — Design

**Date:** 2026-06-11
**Status:** Proposed (awaiting review)
**Author:** Claude Code session (feat/llm-intent-generator)

## Goal

Turn four recurring InGENeer workflows into durable, project-specific tooling — distinct from the
generic workspace/superpowers skills. Deterministic logic lives in committed scripts (CI-reusable);
skills wrap those scripts with the judgment and reporting layer.

## Context

InGENeer hit its V1 baseline (implementation authorized 2026-06-11). Existing project assets:

- Skill: `.claude/skills/intent-command-gen/SKILL.md` (slash-only, scaffolds intent commands)
- Agent: `.claude/agents/autonomation-governance.md` (read-only rule enforcement)
- Python harness: `tools/agentic/`
- Bash scripts: `tools/scripts/`

The repo carries **two independent contract version families** that must each stay internally
consistent — the #1 split-brain risk:

| Family | Schema file | Code constant | Allowlist / catalog |
|---|---|---|---|
| Intent envelope (`1.1.0`) | `schemas/cad_intent_envelope.schema.json` (`schemaVersion.const`) | `INTENT_SCHEMA_VERSION` — `orchestrator/src/ingenieer/models.py:12` | `ALLOWED_COMMANDS` / `COMMAND_RISK` — `orchestrator/src/ingenieer/intent_validation.py` ↔ `docs/INTENT_COMMAND_CATALOG.md` |
| Project/wire contract (`1.0.0`) | `schemas/project_contract.schema.json` | `SCHEMA_VERSION` — `orchestrator/src/ingenieer/contracts.py:16` (consumed by `wire.py:184`) | — |

## Deliverables

### 1. `tools/checks/check_contract_sync.py` (deterministic checker)

A dependency-light Python script (stdlib only: `json`, `re`, `pathlib`, `sys`). Reads all sources
for both families and reports drift. Exit code 0 = in sync, 1 = drift, 2 = usage/IO error.

Checks performed:

- **Intent envelope:** `schemaVersion.const` in the schema == `INTENT_SCHEMA_VERSION` in `models.py`.
- **Project/wire contract:** any version `const` in `project_contract.schema.json` == `SCHEMA_VERSION` in `contracts.py`.
- **Allowlist ↔ catalog:** every command in `ALLOWED_COMMANDS` has a row in `INTENT_COMMAND_CATALOG.md`, and every catalog command row maps to an allowlist entry (no orphans either direction).
- **Risk-tier coherence:** every command in `ALLOWED_COMMANDS` has a `COMMAND_RISK` entry, and the tier matches the catalog row's risk column.

Output: a human-readable table to stdout plus a `--json` mode for machine consumers (CI).
Parsing strategy: regex/AST extraction of the Python constants (no import of the package, so the
script runs without installing the orchestrator); JSON parse for schemas; markdown table parse for
the catalog. The script hardcodes repo-relative paths resolved from its own location, with a
`--repo-root` override.

### 2. `tools/scripts/verify_gate.sh` (full InGENeer gate)

One script that runs, in order, and reports a final pass/fail table:

1. `ruff check src tests` (in `orchestrator/`)
2. `python -m pytest -q` (in `orchestrator/`)
3. `dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release`
4. Domain-isolation greps: no geometry/B-rep symbols in `orchestrator/src/`; no LLM/AI symbols in `icad-addin/` (mirrors the autonomation-governance agent's Rule 1 detection).
5. `python tools/checks/check_contract_sync.py`

Flags: `--skip-dotnet` (for Python-only machines), `--quick` (skip dotnet + pytest, run ruff +
contract-sync only). Non-zero exit if any gate fails. Each step's status collected and printed as a
table at the end even when an early step fails (no `set -e` short-circuit on the reporting).

### 3. Skills (`.claude/skills/<name>/SKILL.md`)

| Skill | Invocation | Wraps | Purpose |
|---|---|---|---|
| `contract-sync` | model-invocable | `check_contract_sync.py` | Detect drift; when bumping a version, walk the coordinated change across schema → constant → catalog → tests together. |
| `verify-gate` | model-invocable | `tools/scripts/verify_gate.sh` | Run the full InGENeer pre-commit gate and report the table. |
| `audit-review` | slash-first (`disable-model-invocation: true`) | `AuditReader` | Summarize `audit_logs/*.jsonl`: command counts, validation rejections, high-risk confirmations, e2e outcomes. |
| `icad-api-research` | slash-first | (prose only) | No-hallucination API loop: search `CARLSON_API_RESEARCH.md` + `INTENT_COMMAND_API_REFERENCE.md` first, then external docs; emit `// TODO` + citation per Rule 4 — never invented method calls. |

`audit-review` uses the existing `AuditReader` via a short `python -c` / `python -m` invocation; no
new heavy module. If a tiny CLI shim is needed it goes in `tools/audit/summarize.py`, but the first
implementation prefers reusing the reader directly.

### 4. CI wiring (`.github/workflows/ci.yml`)

Add a `contracts` job mirroring the existing `python` job style:

```yaml
  contracts:
    name: Contract sync
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - run: python tools/checks/check_contract_sync.py
```

No extra deps (stdlib only), so no `pip install` step. Drift fails the build on push/PR to `main`.

## Non-goals (YAGNI)

- No pre-commit hook installation (CI step covers the gate; local devs use `verify-gate`).
- No auto-fixing of drift — checks report and the skill guides; humans/Claude apply the edit.
- No new audit storage format or schema changes.
- No changes to `intent-command-gen` or the governance agent (they stay as-is and are referenced).

## Boundaries / constraints

- Respect domain isolation: none of this introduces geometry into Python or LLM into C#.
- `check_contract_sync.py` must not import the orchestrator package (runs standalone in CI without install).
- Skills must reference only real symbols/paths verified in this design.

## Test / verification plan

- `check_contract_sync.py`: run against current tree → must exit 0 (tree is currently in sync, modulo
  any drift the script surfaces, which is itself a valid finding to report). Add a unit test in
  `orchestrator/tests/` that runs the checker as a subprocess and asserts exit 0 on the real tree,
  and exit 1 against a fixture with a deliberately mismatched version.
- `verify_gate.sh`: run `--quick` locally and confirm the table renders and exit codes propagate.
- Skills: invoke each once and confirm they call the right script and report correctly.
- CI: confirm YAML parses and the job appears (verified by `python -c yaml.safe_load` locally).

## File manifest

- `tools/checks/check_contract_sync.py` (new)
- `tools/checks/__init__.py` (new, if needed for test import — else omitted)
- `tools/scripts/verify_gate.sh` (new, `chmod +x`)
- `.claude/skills/contract-sync/SKILL.md` (new)
- `.claude/skills/verify-gate/SKILL.md` (new)
- `.claude/skills/audit-review/SKILL.md` (new)
- `.claude/skills/icad-api-research/SKILL.md` (new)
- `orchestrator/tests/test_contract_sync.py` (new)
- `.github/workflows/ci.yml` (modified — add `contracts` job)
