# Workspace standards (cross-project)

This document distills **portable** conventions from the AutonomAtIon governance docs so they apply consistently across **Python orchestrator**, **C# bridge / host**, **schemas**, and **docs**—and so you can **reuse the same files** when spinning up sibling products (e.g. AIrchetect) or a separate CAD workspace.

**Authority for product rules:** [docs/governance/autonomation/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](../docs/governance/autonomation/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md), [docs/governance/autonomation/LAYERED_PRACTICE_PLAYBOOK.md](../docs/governance/autonomation/LAYERED_PRACTICE_PLAYBOOK.md).  
**Local dev versions:** [DEV_SETUP.md](DEV_SETUP.md) (Python 3.11+, .NET 10.x).

---

## Enforced in-repo (tooling)

| Artifact | Purpose |
|----------|---------|
| [`.editorconfig`](../.editorconfig) | UTF-8, LF, final newline, indentation (4 spaces code; 2 for JSON/YAML; Markdown preserves trailing spaces). |
| [`.gitattributes`](../.gitattributes) | Consistent `eol=lf` for source and docs across OSes. |
| [`icad-addin/Directory.Build.props`](../icad-addin/Directory.Build.props) | Shared `TargetFramework`, nullable, implicit usings, deterministic build for **all** projects under `icad-addin/`. |
| [`orchestrator/pyproject.toml`](../orchestrator/pyproject.toml) | Python `requires-python`, optional **ruff** in `[dev]`; `[tool.ruff]` runs **E/F/I/W** with **E501** ignored so existing long lines do not churn (wrap new code near 100 columns when practical). |
| [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) | Parity: gitleaks, `ruff check`, `pytest`, `dotnet build`; **workflow_dispatch**; NuGet cache on the .NET job. |
| [`.vscode/`](../.vscode/) | Recommended extensions (Python, C#, EditorConfig), tasks (ruff, pytest, dotnet build), settings aligned with `.editorconfig`. |
| [`InGENeer.code-workspace`](../InGENeer.code-workspace) | Multi-root workspace for orchestrator, `icad-addin`, schemas, docs, AutonomAtIon, scripts — limits IDE scope (see `~/Dev/DEV_OPS_RUNBOOK.md` when using a parent monorepo). |
| [`docs/WORKSPACE_SCOPE_MAP.md`](WORKSPACE_SCOPE_MAP.md) + [`docs/workspaces/`](workspaces/) | Scoped Cursor workspaces for architecture/docs, agentic planning, Python orchestrator, C# host, contracts/schemas, governance, and repo ops. Open the narrowest workspace matching the task; never open `~/Dev`. |
| [`AGENTS.md`](../AGENTS.md) | Single entrypoint for coding agents; links governance and worktrees. |

---

## Process and safety (from architecture rules — every contributor)

These are not single keys in a config file; they are **non-negotiable practices** referenced from [CONTRIBUTING.md](../CONTRIBUTING.md):

1. **Domain isolation** — Orchestrator (Python): validation, transport, audit; **no** B-rep geometry or proprietary CAD APIs. CAD execution (C# / future FreeCAD worker): deterministic only; **no** LLM in shipping execution code.
2. **Threading and transactions** — UI-thread marshaling for host mutations; native transactions with explicit rollback; never strip error handling to “clean up.”
3. **No API hallucinations** — `// TODO` + doc citation when vendor APIs are uncertain.
4. **Air-gapped workspaces (SOP 2)** — Separate Cursor/context for orchestrator vs host; hand off **schema + sample JSON** like production ([`tools/scripts/copy_schema_handoff.sh`](../tools/scripts/copy_schema_handoff.sh)).
5. **Cursor hygiene (SOPs 4–6)** — Right modality for the task; micro-diff audit; commit before big generations; reset after repeated failure.

**Model × language routing:** [MODEL_LANGUAGE_ROUTING.md](MODEL_LANGUAGE_ROUTING.md), [AI_ASSISTANT_BEST_PRACTICES.md](AI_ASSISTANT_BEST_PRACTICES.md).

---

## Applying to a **new** repo or worktree

1. Copy **`.editorconfig`** and **`.gitattributes`** to the new repository root (or merge into existing ones).
2. For a new **.NET** solution, add a **`Directory.Build.props`** beside the `.sln`/`.slnx` with the same `TargetFramework` / nullable / deterministic pattern as `icad-addin/`.
3. For **Python** packages, align `requires-python` with team standard (here: **3.11+**); add **ruff** (or equivalent) and a **CI** lint step.
4. Link or vendor the **architecture rules** and **playbook** (or a product-specific fork) so boundaries stay explicit.
5. Enable **secret scanning** in CI (this repo uses **gitleaks**); see [REPOSITORY_SETUP.md](REPOSITORY_SETUP.md).

---

## Versioning reminder (intent vs wire)

| Contract | Version field | Bump when |
|----------|---------------|-----------|
| Intent envelope | `schemaVersion` in JSON / `CadIntentEnvelope` | Envelope shape breaks |
| Outer wire payloads | `ingenieer.contracts.SCHEMA_VERSION` | Outer contract shape breaks |

Details: [LAYERED_PRACTICE_PLAYBOOK.md](../docs/governance/autonomation/LAYERED_PRACTICE_PLAYBOOK.md) versioning table, [INTENT_COMMAND_CATALOG.md](INTENT_COMMAND_CATALOG.md).
