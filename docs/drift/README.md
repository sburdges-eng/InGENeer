# Drift reports (C-5.2)

When an implementation task conflicts with `ARCHITECTURE.md`, `REQUIREMENTS.md`, `CONSTRAINTS.md`, `handoff.md`, or `adr/`, **stop** and file a drift report here before proceeding.

## Naming

`DRIFT-YYYYMMDD-<slug>.md` — example: `DRIFT-20260611-stage1-relocation-timing.md`

## Workflow

1. Copy [`DRIFT_TEMPLATE.md`](DRIFT_TEMPLATE.md) to a new file with the naming convention above.
2. Fill every section; cite the conflicting constraint/ADR/requirement IDs.
3. Request human approval in `docs/handoff.md` (session addendum or open questions).
4. Do not merge implementation code until the report is resolved.

The session harness (`tools/agentic/session_harness.py`) references this directory on violation.
