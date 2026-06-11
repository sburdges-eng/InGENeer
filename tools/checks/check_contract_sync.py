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
    # Assumes the literal has no nested braces (true for the current set/dict literals).
    m = re.search(r"ALLOWED_COMMANDS[^{]*\{(.*?)\}", text, re.DOTALL)
    if not m:
        return set()
    return set(re.findall(r'"([^"]+)"', m.group(1)))


def extract_command_risk(py_path: Path) -> dict[str, str]:
    text = _read(py_path)
    # Assumes the literal has no nested braces (true for the current set/dict literals).
    m = re.search(r"COMMAND_RISK[^{]*\{(.*?)\}", text, re.DOTALL)
    if not m:
        return {}
    return dict(re.findall(r'"([^"]+)"\s*:\s*"([^"]+)"', m.group(1)))


def extract_catalog(md_path: Path) -> dict[str, str]:
    """Return {command: risk} documented in the catalog.

    Commands are documented in two forms, both harvested here:
      1. The main command table (header `| Command | Risk |`).
      2. Per-command `### CommandName` sections whose detail table has a
         `| **Risk** | `tier` |` row.
    Generic detail tables with header `| Field | Value |` are otherwise ignored.
    """
    text = _read(md_path)
    result: dict[str, str] = {}

    # Form 1: the main `| Command | Risk |` table.
    # Assumes a single main command table; a second would merge into the same map.
    in_table = False
    for line in text.splitlines():
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

    # Form 2: per-command `### CommandName` sections with a **Risk** detail row.
    lines = text.splitlines()
    for i, line in enumerate(lines):
        heading = re.match(r"^###\s+([A-Za-z][A-Za-z0-9]*)\s*$", line.strip())
        if not heading:
            continue
        command = heading.group(1)
        for follow in lines[i + 1:]:
            fstr = follow.strip()
            if fstr.startswith("### ") or fstr.startswith("## "):
                break  # next section, no Risk row found
            risk_row = re.match(r"^\|\s*\*\*Risk\*\*\s*\|\s*`?([A-Za-z]+)`?\s*\|", fstr)
            if risk_row:
                result.setdefault(command, risk_row.group(1).strip())
                break
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
