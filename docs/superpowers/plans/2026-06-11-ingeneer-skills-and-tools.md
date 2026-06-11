# InGENeer Project Skills & Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add four project-specific Claude skills plus two committed deterministic checks (contract-sync + verify-gate) and a CI job, so InGENeer's recurring guardrail workflows are durable and reusable outside Claude.

**Architecture:** Deterministic logic lives in committed scripts (`tools/checks/check_contract_sync.py`, `tools/scripts/verify_gate.sh`) so CI and humans can run them; skills under `.claude/skills/` wrap them with judgment and reporting. `check_contract_sync.py` is stdlib-only and never imports the orchestrator package, so it runs in CI without `pip install`.

**Tech Stack:** Python 3.11 (stdlib only for the checker), Bash, GitHub Actions, Markdown skills, pytest.

---

## File Structure

- `tools/checks/check_contract_sync.py` (new) — stdlib drift checker, one responsibility: detect contract/allowlist/catalog drift, exit non-zero on drift.
- `tools/scripts/verify_gate.sh` (new) — orchestrates the full local gate, collects per-step status, prints a table.
- `.claude/skills/contract-sync/SKILL.md` (new) — wraps the checker; guides coordinated version bumps.
- `.claude/skills/verify-gate/SKILL.md` (new) — wraps `verify_gate.sh`.
- `.claude/skills/audit-review/SKILL.md` (new) — summarizes `audit_logs/*.jsonl` via `AuditReader`.
- `.claude/skills/icad-api-research/SKILL.md` (new) — no-hallucination API research loop.
- `orchestrator/tests/test_contract_sync.py` (new) — subprocess tests for the checker.
- `.github/workflows/ci.yml` (modify) — add `contracts` job.

### Verified ground-truth facts (do not re-derive)

- Intent envelope schema const: `schemas/cad_intent_envelope.schema.json` → `properties.schemaVersion.const` == `"1.1.0"`.
- `orchestrator/src/ingenieer/models.py:12` → `INTENT_SCHEMA_VERSION = "1.1.0"`.
- `orchestrator/src/ingenieer/intent_validation.py` → `ALLOWED_COMMANDS: frozenset[str] = frozenset({ ... })` and `COMMAND_RISK: dict[str, str] = { ... }` (19 commands, tiers `low`/`high`).
- `docs/INTENT_COMMAND_CATALOG.md` → main command table header `| Command | Risk | Parameters (example) | Notes |`; commands wrapped in backticks; risk is `low`/`high`. Per-command **detail** tables use header `| Field | Value |` with rows like `| **Risk** | \`high\` |` — the parser must IGNORE these and only read the main command table.
- `orchestrator/src/ingenieer/contracts.py:16` → `SCHEMA_VERSION = "1.0.0"`.
- `orchestrator/src/ingenieer/wire.py:9` → `from ingenieer.contracts import SCHEMA_VERSION, build_contract_payload`; `wire.py:184` uses it as a default. wire.py does NOT define its own `SCHEMA_VERSION = ...` literal.
- `schemas/project_contract.schema.json` → `version` is `{ "type": "string" }` with NO `const`.
- `AuditReader` (`orchestrator/src/ingenieer/audit_reader.py`): `__init__(log_dir)`, `query(project_id=None, command=None, after=None, before=None) -> list[dict]`, `commands_summary(project_id=None) -> dict[str,int]`. Each entry has keys `seq`, `timestamp`, `project_id`, `event`, `data` (dict, may hold `command`/`phase`/`step`), `prev_hash`, `hash`.
- Existing CI: `.github/workflows/ci.yml` has jobs `gitleaks`, `python` (working-directory `orchestrator`, python 3.11), `dotnet`.

---

## Task 1: Contract-sync checker (TDD)

**Files:**
- Create: `tools/checks/check_contract_sync.py`
- Test: `orchestrator/tests/test_contract_sync.py`

- [ ] **Step 1: Write the failing test**

Create `orchestrator/tests/test_contract_sync.py`:

```python
"""Subprocess tests for tools/checks/check_contract_sync.py."""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPO_ROOT / "tools" / "checks" / "check_contract_sync.py"

# Files the checker reads, relative to repo root.
TRACKED = [
    "schemas/cad_intent_envelope.schema.json",
    "schemas/project_contract.schema.json",
    "orchestrator/src/ingenieer/models.py",
    "orchestrator/src/ingenieer/contracts.py",
    "orchestrator/src/ingenieer/intent_validation.py",
    "orchestrator/src/ingenieer/wire.py",
    "docs/INTENT_COMMAND_CATALOG.md",
]


def _run(repo_root: Path | None = None, json_mode: bool = False) -> subprocess.CompletedProcess[str]:
    cmd = [sys.executable, str(CHECKER)]
    if repo_root is not None:
        cmd += ["--repo-root", str(repo_root)]
    if json_mode:
        cmd += ["--json"]
    return subprocess.run(cmd, capture_output=True, text=True)


def _materialize_fixture(dst: Path) -> None:
    for rel in TRACKED:
        target = dst / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(REPO_ROOT / rel, target)


def test_real_tree_in_sync() -> None:
    result = _run()
    assert result.returncode == 0, f"drift in real tree:\n{result.stdout}\n{result.stderr}"


def test_json_mode_emits_object() -> None:
    result = _run(json_mode=True)
    payload = json.loads(result.stdout)
    assert payload["ok"] is True
    assert isinstance(payload["checks"], list)


def test_intent_version_drift_detected(tmp_path: Path) -> None:
    _materialize_fixture(tmp_path)
    models = tmp_path / "orchestrator/src/ingenieer/models.py"
    text = models.read_text(encoding="utf-8")
    text = text.replace('INTENT_SCHEMA_VERSION = "1.1.0"', 'INTENT_SCHEMA_VERSION = "9.9.9"')
    models.write_text(text, encoding="utf-8")

    result = _run(repo_root=tmp_path)
    assert result.returncode == 1
    assert "INTENT_SCHEMA_VERSION" in result.stdout or "envelope" in result.stdout.lower()


def test_missing_allowlist_command_in_catalog(tmp_path: Path) -> None:
    _materialize_fixture(tmp_path)
    iv = tmp_path / "orchestrator/src/ingenieer/intent_validation.py"
    text = iv.read_text(encoding="utf-8")
    # Inject a fake command into the allowlist that is absent from the catalog.
    text = text.replace('"NoOp",', '"NoOp",\n        "TotallyFakeCommand",', 1)
    iv.write_text(text, encoding="utf-8")

    result = _run(repo_root=tmp_path)
    assert result.returncode == 1
    assert "TotallyFakeCommand" in result.stdout
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd orchestrator && python -m pytest tests/test_contract_sync.py -v`
Expected: FAIL — `CHECKER` file does not exist, subprocess returns non-zero with `can't open file ... check_contract_sync.py`.

- [ ] **Step 3: Write the checker**

Create `tools/checks/check_contract_sync.py`:

```python
#!/usr/bin/env python3
"""Cross-file contract version + allowlist drift checker for InGENeer.

Stdlib only. Does NOT import the orchestrator package, so it runs in CI without install.

Checks:
  1. Intent envelope: schema `schemaVersion.const` == models.INTENT_SCHEMA_VERSION.
  2. Project/wire contract: contracts.SCHEMA_VERSION is valid semver, and wire.py
     imports it (never redefines a literal).
  3. Allowlist subset of catalog: every ALLOWED_COMMANDS entry has a catalog row (ERROR if not).
     Catalog command not in allowlist => WARNING (documented but not yet enabled).
  4. Risk coherence: every ALLOWED_COMMANDS entry has a COMMAND_RISK tier, and that tier
     matches the catalog row's Risk column.

Exit codes: 0 = in sync, 1 = drift (any ERROR), 2 = usage/IO error.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ""
    severity: str = "error"  # "error" | "warning"


@dataclass
class Report:
    checks: list[Check] = field(default_factory=list)

    def add(self, name: str, ok: bool, detail: str = "", severity: str = "error") -> None:
        self.checks.append(Check(name, ok, detail, severity))

    @property
    def ok(self) -> bool:
        return all(c.ok for c in self.checks if c.severity == "error")


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_schema_const(schema_path: Path) -> str | None:
    data = json.loads(_read(schema_path))
    return data.get("properties", {}).get("schemaVersion", {}).get("const")


def extract_py_string_constant(py_path: Path, name: str) -> str | None:
    text = _read(py_path)
    m = re.search(rf'^{re.escape(name)}\s*(?::[^=]+)?=\s*["\']([^"\']+)["\']', text, re.MULTILINE)
    return m.group(1) if m else None


def extract_allowlist(py_path: Path) -> set[str]:
    text = _read(py_path)
    m = re.search(r"ALLOWED_COMMANDS[^{]*\{(.*?)\}", text, re.DOTALL)
    if not m:
        return set()
    return set(re.findall(r'"([^"]+)"', m.group(1)))


def extract_command_risk(py_path: Path) -> dict[str, str]:
    text = _read(py_path)
    m = re.search(r"COMMAND_RISK[^{]*\{(.*?)\}", text, re.DOTALL)
    if not m:
        return {}
    return dict(re.findall(r'"([^"]+)"\s*:\s*"([^"]+)"', m.group(1)))


def extract_catalog(md_path: Path) -> dict[str, str]:
    """Return {command: risk} from the main command table only.

    The main table header begins with `| Command | Risk |`. Per-command detail tables
    (header `| Field | Value |`) are ignored.
    """
    result: dict[str, str] = {}
    in_table = False
    for line in _read(md_path).splitlines():
        stripped = line.strip()
        if re.match(r"^\|\s*Command\s*\|\s*Risk\s*\|", stripped):
            in_table = True
            continue
        if in_table:
            if not stripped.startswith("|"):
                break
            if re.match(r"^\|[\s\-|:]+\|$", stripped):
                continue  # separator row
            cells = [c.strip() for c in stripped.strip("|").split("|")]
            if len(cells) < 2:
                continue
            command = cells[0].strip("`").strip()
            risk = cells[1].strip("`").strip()
            if command:
                result[command] = risk
    return result


def wire_imports_schema_version(wire_path: Path) -> bool:
    text = _read(wire_path)
    imports = bool(re.search(r"from\s+ingenieer\.contracts\s+import[^\n]*\bSCHEMA_VERSION\b", text))
    redefines = bool(re.search(r"^SCHEMA_VERSION\s*=", text, re.MULTILINE))
    return imports and not redefines


def run_checks(root: Path) -> Report:
    report = Report()

    envelope_schema = root / "schemas/cad_intent_envelope.schema.json"
    models = root / "orchestrator/src/ingenieer/models.py"
    contracts = root / "orchestrator/src/ingenieer/contracts.py"
    intent_validation = root / "orchestrator/src/ingenieer/intent_validation.py"
    wire = root / "orchestrator/src/ingenieer/wire.py"
    catalog = root / "docs/INTENT_COMMAND_CATALOG.md"

    # Check 1: intent envelope version.
    schema_const = extract_schema_const(envelope_schema)
    code_const = extract_py_string_constant(models, "INTENT_SCHEMA_VERSION")
    report.add(
        "intent-envelope-version",
        schema_const is not None and schema_const == code_const,
        f"schema const={schema_const!r} vs models.INTENT_SCHEMA_VERSION={code_const!r}",
    )

    # Check 2: project/wire contract version sanity.
    schema_ver = extract_py_string_constant(contracts, "SCHEMA_VERSION")
    report.add(
        "project-contract-semver",
        bool(schema_ver) and bool(SEMVER_RE.match(schema_ver or "")),
        f"contracts.SCHEMA_VERSION={schema_ver!r}",
    )
    report.add(
        "wire-imports-schema-version",
        wire_imports_schema_version(wire),
        "wire.py must import SCHEMA_VERSION from ingenieer.contracts and not redefine it",
    )

    # Check 3 & 4: allowlist <-> catalog and risk coherence.
    allowlist = extract_allowlist(intent_validation)
    risk_map = extract_command_risk(intent_validation)
    catalog_map = extract_catalog(catalog)

    missing_in_catalog = sorted(allowlist - set(catalog_map))
    report.add(
        "allowlist-documented",
        not missing_in_catalog,
        f"allowed commands missing a catalog row: {missing_in_catalog}",
    )

    undocumented_extra = sorted(set(catalog_map) - allowlist)
    report.add(
        "catalog-extra-commands",
        not undocumented_extra,
        f"catalog rows not in allowlist (planned/disabled?): {undocumented_extra}",
        severity="warning",
    )

    missing_risk = sorted(allowlist - set(risk_map))
    report.add(
        "risk-tier-present",
        not missing_risk,
        f"allowed commands missing a COMMAND_RISK tier: {missing_risk}",
    )

    mismatched = sorted(
        cmd
        for cmd in allowlist & set(risk_map) & set(catalog_map)
        if risk_map[cmd] != catalog_map[cmd]
    )
    report.add(
        "risk-tier-matches-catalog",
        not mismatched,
        "tier mismatch (code vs catalog): "
        + ", ".join(f"{c}: {risk_map[c]}!={catalog_map[c]}" for c in mismatched),
    )

    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Check InGENeer contract/allowlist drift.")
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Repository root (defaults to two levels above this script).",
    )
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON.")
    args = parser.parse_args(argv)

    try:
        report = run_checks(args.repo_root)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"contract-sync: IO/parse error: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(
            json.dumps(
                {
                    "ok": report.ok,
                    "checks": [
                        {"name": c.name, "ok": c.ok, "severity": c.severity, "detail": c.detail}
                        for c in report.checks
                    ],
                },
                indent=2,
            )
        )
    else:
        print("InGENeer contract-sync\n" + "=" * 40)
        for c in report.checks:
            if c.ok:
                mark = "PASS"
            elif c.severity == "warning":
                mark = "WARN"
            else:
                mark = "FAIL"
            print(f"[{mark}] {c.name}")
            if not c.ok:
                print(f"       {c.detail}")
        print("=" * 40)
        print("RESULT:", "IN SYNC" if report.ok else "DRIFT DETECTED")

    return 0 if report.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Make the checker executable and run it against the real tree**

Run:
```bash
chmod +x tools/checks/check_contract_sync.py
python tools/checks/check_contract_sync.py
```
Expected: prints the table and `RESULT: IN SYNC`, exit 0. If it reports real DRIFT, that is a genuine finding — STOP and report it to the user before continuing (do not paper over it by editing the checker).

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd orchestrator && python -m pytest tests/test_contract_sync.py -v`
Expected: all four tests PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/checks/check_contract_sync.py orchestrator/tests/test_contract_sync.py
git commit -m "feat: add contract-sync drift checker with tests"
```

---

## Task 2: Wire contract-sync into CI

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add the `contracts` job**

Append this job under `jobs:` in `.github/workflows/ci.yml` (after the `python` job, before `dotnet`):

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

(No `pip install` — the checker is stdlib-only.)

- [ ] **Step 2: Validate the YAML parses**

Run:
```bash
python -c "import yaml, sys; yaml.safe_load(open('.github/workflows/ci.yml')); print('ci.yml OK')"
```
Expected: `ci.yml OK` (if PyYAML is unavailable, instead run `python -c "import json,subprocess"` skip — confirm by eye that indentation matches the `python` job).

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: fail build on contract-sync drift"
```

---

## Task 3: verify-gate script

**Files:**
- Create: `tools/scripts/verify_gate.sh`

- [ ] **Step 1: Write the script**

Create `tools/scripts/verify_gate.sh`:

```bash
#!/usr/bin/env bash
# InGENeer full local verification gate.
# Runs ruff + pytest + dotnet build + domain-isolation greps + contract-sync,
# collecting per-step status and printing a table at the end.
#
# Flags:
#   --skip-dotnet   Skip the .NET build (e.g. on a Python-only machine).
#   --quick         Run ruff + contract-sync only (skip pytest and dotnet).
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

SKIP_DOTNET=0
QUICK=0
for arg in "$@"; do
  case "$arg" in
    --skip-dotnet) SKIP_DOTNET=1 ;;
    --quick) QUICK=1 ;;
    *) echo "Unknown flag: $arg" >&2; exit 2 ;;
  esac
done

declare -a NAMES=()
declare -a RESULTS=()
OVERALL=0

record() {
  # record <name> <exit_code>
  NAMES+=("$1")
  if [ "$2" -eq 0 ]; then
    RESULTS+=("PASS")
  else
    RESULTS+=("FAIL")
    OVERALL=1
  fi
}

run_step() {
  # run_step <name> <command...>
  local name="$1"; shift
  echo ">>> $name"
  if "$@"; then record "$name" 0; else record "$name" $?; fi
}

# 1. ruff
run_step "ruff" bash -c "cd orchestrator && ruff check src tests"

# 2. pytest (unless --quick)
if [ "$QUICK" -eq 0 ]; then
  run_step "pytest" bash -c "cd orchestrator && python -m pytest -q"
fi

# 3. dotnet build (unless skipped or --quick)
if [ "$QUICK" -eq 0 ] && [ "$SKIP_DOTNET" -eq 0 ]; then
  run_step "dotnet-build" dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release
fi

# 4. domain isolation: no geometry in orchestrator Python.
echo ">>> domain-isolation:python"
if grep -rniE "b_rep|brep|tessellat|nurbs" orchestrator/src/ingenieer/ >/dev/null 2>&1; then
  echo "    found geometry symbols in orchestrator/src (Rule 1 violation)"
  record "domain-isolation:python" 1
else
  record "domain-isolation:python" 0
fi

# 5. domain isolation: no LLM/AI in C# execution layer.
echo ">>> domain-isolation:csharp"
if grep -rniE "openai|anthropic|\bllm\b|transformers" icad-addin/ --include="*.cs" >/dev/null 2>&1; then
  echo "    found LLM/AI symbols in icad-addin (Rule 1 violation)"
  record "domain-isolation:csharp" 1
else
  record "domain-isolation:csharp" 0
fi

# 6. contract sync
run_step "contract-sync" python tools/checks/check_contract_sync.py

echo ""
echo "==================== VERIFY GATE ===================="
for i in "${!NAMES[@]}"; do
  printf "  [%s] %s\n" "${RESULTS[$i]}" "${NAMES[$i]}"
done
echo "====================================================="
if [ "$OVERALL" -eq 0 ]; then
  echo "RESULT: ALL GATES PASS"
else
  echo "RESULT: GATE FAILURES"
fi
exit "$OVERALL"
```

- [ ] **Step 2: Make executable and smoke-test**

Run:
```bash
chmod +x tools/scripts/verify_gate.sh
./tools/scripts/verify_gate.sh --quick
```
Expected: prints the gate table with `ruff`, `domain-isolation:python`, `domain-isolation:csharp`, `contract-sync`, and a final `RESULT:` line. Exit 0 if all pass.

- [ ] **Step 3: Commit**

```bash
git add tools/scripts/verify_gate.sh
git commit -m "feat: add verify_gate.sh full local gate"
```

---

## Task 4: contract-sync skill

**Files:**
- Create: `.claude/skills/contract-sync/SKILL.md`

- [ ] **Step 1: Write the skill**

Create `.claude/skills/contract-sync/SKILL.md`:

```markdown
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
4. Tests asserting the version (search: `grep -rn "1\\.1\\.0\\|INTENT_SCHEMA_VERSION" orchestrator/tests`).

Then re-run the checker and `python -m pytest -q` in `orchestrator/`.

## Adding a command

Prefer the `/intent-command-gen` skill, which scaffolds catalog + allowlist + risk + C# stub + tests together. After it runs, confirm with the checker. The allowlist must be a subset of the catalog (FAIL otherwise); a catalog row without an allowlist entry is a WARN (planned/disabled command).
```

- [ ] **Step 2: Verify the skill loads**

Run: `python -c "import yaml; f=open('.claude/skills/contract-sync/SKILL.md'); h=f.read().split('---')[1]; yaml.safe_load(h); print('frontmatter OK')"`
Expected: `frontmatter OK`.

- [ ] **Step 3: Commit**

```bash
git add .claude/skills/contract-sync/SKILL.md
git commit -m "feat: add contract-sync skill"
```

---

## Task 5: verify-gate skill

**Files:**
- Create: `.claude/skills/verify-gate/SKILL.md`

- [ ] **Step 1: Write the skill**

Create `.claude/skills/verify-gate/SKILL.md`:

```markdown
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
./tools/scripts/verify_gate.sh
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
```

- [ ] **Step 2: Verify frontmatter**

Run: `python -c "import yaml; f=open('.claude/skills/verify-gate/SKILL.md'); h=f.read().split('---')[1]; yaml.safe_load(h); print('frontmatter OK')"`
Expected: `frontmatter OK`.

- [ ] **Step 3: Commit**

```bash
git add .claude/skills/verify-gate/SKILL.md
git commit -m "feat: add verify-gate skill"
```

---

## Task 6: audit-review skill

**Files:**
- Create: `.claude/skills/audit-review/SKILL.md`

- [ ] **Step 1: Write the skill**

Create `.claude/skills/audit-review/SKILL.md`:

```markdown
---
name: audit-review
description: "Summarize InGENeer audit logs (audit_logs/*.jsonl) — command counts, event/phase breakdown, validation rejections, high-risk confirmations, and per-project activity. Usage: /audit-review [project_id]"
disable-model-invocation: true
args: project_id
---

# Audit Review

Summarize the hash-chained audit logs in `audit_logs/*.jsonl` using the existing `AuditReader`
(`orchestrator/src/ingenieer/audit_reader.py`). Read-only.

## Steps

### 1. Run the summary

From `orchestrator/` (so the package is importable), run this inline script. Pass the optional
`project_id` argument to scope the report; omit it for all projects.

```bash
cd orchestrator && python - "$@" <<'PY'
import sys
from collections import Counter
from ingenieer.audit_reader import AuditReader

project = sys.argv[1] if len(sys.argv) > 1 else None
reader = AuditReader("../audit_logs")
entries = reader.query(project_id=project)

print(f"entries: {len(entries)}  project: {project or 'ALL'}")

events = Counter(e.get("event") for e in entries)
print("\nevents:")
for name, n in events.most_common():
    print(f"  {n:5d}  {name}")

print("\ncommands:")
for cmd, n in sorted(reader.commands_summary(project_id=project).items(), key=lambda kv: -kv[1]):
    print(f"  {n:5d}  {cmd}")

# Surface notable events without hardcoding an event taxonomy: anything whose name or data
# suggests rejection/failure/confirmation.
notable = [
    e for e in entries
    if any(k in (e.get("event") or "").lower() for k in ("reject", "fail", "error", "confirm", "deny"))
]
print(f"\nnotable events (reject/fail/error/confirm): {len(notable)}")
for e in notable[:20]:
    print(f"  seq={e.get('seq')} {e.get('event')} data={e.get('data')}")
PY
```

### 2. Report

Summarize for the user: total entries, the event breakdown, command frequency, and any notable
rejection/failure/confirmation events. If the user asked about a specific run, filter by the
matching `project_id` (e.g. the `e2e` logs are `audit_logs/e2e_*.jsonl` → `project_id="e2e"`).

Do not modify or delete any audit log — these are an integrity record (hash-chained via `prev_hash`/`hash`).
```

- [ ] **Step 2: Verify frontmatter**

Run: `python -c "import yaml; f=open('.claude/skills/audit-review/SKILL.md'); h=f.read().split('---')[1]; yaml.safe_load(h); print('frontmatter OK')"`
Expected: `frontmatter OK`.

- [ ] **Step 3: Smoke-test the inline summary against real logs**

Run:
```bash
cd orchestrator && python - <<'PY'
from collections import Counter
from ingenieer.audit_reader import AuditReader
reader = AuditReader("../audit_logs")
entries = reader.query()
print("entries:", len(entries))
print("commands:", reader.commands_summary())
PY
```
Expected: prints a non-zero entry count and a commands dict (the repo has `audit_logs/e2e_*.jsonl` and `unknown_*.jsonl`). Confirms the import path and reader work before relying on the skill.

- [ ] **Step 4: Commit**

```bash
git add .claude/skills/audit-review/SKILL.md
git commit -m "feat: add audit-review skill"
```

---

## Task 7: icad-api-research skill

**Files:**
- Create: `.claude/skills/icad-api-research/SKILL.md`

- [ ] **Step 1: Write the skill**

Create `.claude/skills/icad-api-research/SKILL.md`:

```markdown
---
name: icad-api-research
description: "Research the correct Carlson / IntelliCAD (iCAD) API for a command before writing C#, to satisfy Rule 4 (no API hallucinations). Produces a cited // TODO block, never invented method calls. Usage: /icad-api-research <CommandName or capability>"
disable-model-invocation: true
args: capability
---

# iCAD / Carlson API Research

Rule 4 of the AutonomAtIon architecture: **never guess proprietary CAD API methods** (Autodesk,
Bentley, Siemens, Carlson, ITC/IntelliCAD). Uncertain APIs must use `// TODO` with a doc citation
request. This skill runs the research loop that produces that citation.

## Steps

### 1. Search the repo's own research first

```bash
cd /Users/seanburdges/Dev/InGENeer
grep -ni "<capability>" docs/CARLSON_API_RESEARCH.md docs/INTENT_COMMAND_API_REFERENCE.md
```

If a documented API + citation already exists, use it directly and skip to step 4.

### 2. Search external documentation

If not found in-repo, search the official sources (Carlson SDK docs, IntelliCAD/ITC ARX/.NET API
references, ODA docs). Use WebSearch/WebFetch. Capture: exact namespace, class, method signature,
and the source URL + access date.

### 3. Record the finding

Append a row to `docs/CARLSON_API_RESEARCH.md` under the relevant section:

| Capability | API (namespace.Class.Method) | Source (URL, accessed YYYY-MM-DD) | Confidence |

If you could NOT find an authoritative source, say so explicitly — do not fabricate a signature.

### 4. Emit the C# stub block

Produce the dispatcher block for the user to paste (do NOT invent calls if unconfirmed):

```csharp
case "<CommandName>":
    // TODO(Rule 4): Implement <CommandName> via <confirmed API or "API UNCONFIRMED">.
    // Source: <URL accessed YYYY-MM-DD | "needs doc citation">
    // Parameters: <parameters>
    return BridgeExecutionResult.Stub("<CommandName>", intent);
```

### 5. Report

Tell the user: what was found (with citation), whether confidence is high/low, and whether the
stub is ready to implement or still blocked on an authoritative source. Never present an unconfirmed
API as confirmed.
```

- [ ] **Step 2: Verify frontmatter**

Run: `python -c "import yaml; f=open('.claude/skills/icad-api-research/SKILL.md'); h=f.read().split('---')[1]; yaml.safe_load(h); print('frontmatter OK')"`
Expected: `frontmatter OK`.

- [ ] **Step 3: Confirm referenced docs exist**

Run: `ls docs/CARLSON_API_RESEARCH.md docs/INTENT_COMMAND_API_REFERENCE.md`
Expected: both paths listed (they exist in the repo).

- [ ] **Step 4: Commit**

```bash
git add .claude/skills/icad-api-research/SKILL.md
git commit -m "feat: add icad-api-research skill"
```

---

## Final verification

- [ ] **Run the full gate** to confirm nothing regressed:

```bash
./tools/scripts/verify_gate.sh
```
Expected: all gates PASS (or only the documented WARN rows from contract-sync).

- [ ] **List the new skills** to confirm Claude can discover them:

```bash
ls .claude/skills/
```
Expected: `audit-review`, `contract-sync`, `icad-api-research`, `intent-command-gen`, `verify-gate`.

---

## Notes / spec deviations

- The spec proposed comparing a `const` in `project_contract.schema.json` against `SCHEMA_VERSION`. Verification showed that schema declares `version` as a free string with NO `const`, and `wire.py` imports `SCHEMA_VERSION` rather than copying it. The checker therefore guards the *real* risk instead: `SCHEMA_VERSION` is valid semver and `wire.py` does not redefine it. Documented in Task 1.
- `tools/checks/__init__.py` is NOT needed — the checker runs as a standalone script (subprocess), never imported as a module.
```
