#!/usr/bin/env python3
"""
Pre-flight and post-flight checks for agent sessions (Phase 0).

Usage:
  python tools/agentic/session_harness.py preflight [--dry-run] [--dirty-scope PATH ...]
  python tools/agentic/session_harness.py postflight [--dry-run] [--base-ref REF]
  python tools/agentic/session_harness.py verify-feature-list
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

import yaml


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def load_contract(root: Path) -> dict[str, Any]:
    path = root / "tools" / "agentic" / "session_contract.yaml"
    with path.open(encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def run_cmd(cmd: str, root: Path) -> tuple[int, str]:
    proc = subprocess.run(
        cmd,
        shell=True,
        cwd=root,
        capture_output=True,
        text=True,
    )
    output = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode, output


def git_status_porcelain(root: Path) -> list[str]:
    proc = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=root,
        capture_output=True,
        text=True,
        check=True,
    )
    return [line for line in proc.stdout.splitlines() if line.strip()]


def check_git_clean(root: Path, dirty_scope: list[str]) -> list[str]:
    errors: list[str] = []
    lines = git_status_porcelain(root)
    if not lines:
        return errors
    if dirty_scope:
        allowed = {p.rstrip("/") for p in dirty_scope}
        for line in lines:
            path = line[3:].strip()
            if not any(path == a or path.startswith(a + "/") for a in allowed):
                errors.append(f"unexpected dirty path: {path}")
        return errors
    errors.append(f"git working tree not clean ({len(lines)} entries); use --dirty-scope or commit/stash")
    return errors


def check_must_read(root: Path, paths: list[str]) -> list[str]:
    errors: list[str] = []
    for rel in paths:
        if not (root / rel).is_file():
            errors.append(f"must_read file missing: {rel}")
    return errors


def scan_forbidden_diffs(root: Path, base_ref: str, patterns: list[dict[str, str]]) -> list[str]:
    proc = subprocess.run(
        ["git", "diff", base_ref, "--", "."],
        cwd=root,
        capture_output=True,
        text=True,
        check=True,
    )
    errors: list[str] = []
    for line in proc.stdout.splitlines():
        if not line.startswith("-") or line.startswith("---"):
            continue
        removed = line[1:]
        for item in patterns:
            pat = item["pattern"]
            if re.search(re.escape(pat), removed, re.IGNORECASE):
                errors.append(f"forbidden removal ({item.get('description', pat)}): {removed.strip()[:120]}")
    return errors


def verify_feature_list(root: Path) -> list[str]:
    fl_path = root / "docs" / "specs" / "feature_list.json"
    if not fl_path.is_file():
        return ["feature_list.json missing; run tools/agentic/seed_feature_list.py"]
    data = json.loads(fl_path.read_text(encoding="utf-8"))
    features = data.get("features", [])
    passing = [f for f in features if f.get("status") == "passing"]
    if not passing:
        return ["feature_list.json has no passing features"]

    # Batch run: one pytest invocation matches CI; per-test cmds remain in the manifest for targeted reruns.
    cmd = (
        "cd orchestrator && python -m pytest -q "
        "--ignore=tests/test_intent_generator.py -m 'not integration'"
    )
    code, output = run_cmd(cmd, root)
    if code != 0:
        return [f"feature_list batch verify failed: exit {code}\n{output[-1200:]}"]
    return []


def cmd_preflight(args: argparse.Namespace) -> int:
    root = repo_root()
    contract = load_contract(root)
    pre = contract.get("preconditions", {})
    errors: list[str] = []

    if not args.dry_run and pre.get("git_clean", True):
        scope = list(pre.get("dirty_scope", [])) + list(args.dirty_scope or [])
        errors.extend(check_git_clean(root, scope))

    errors.extend(check_must_read(root, pre.get("must_read", [])))

    if errors:
        for err in errors:
            print(f"FAIL preflight: {err}", file=sys.stderr)
        return 1
    print("OK preflight")
    return 0


def cmd_postflight(args: argparse.Namespace) -> int:
    root = repo_root()
    contract = load_contract(root)
    errors: list[str] = []

    if args.dry_run:
        print("OK postflight (dry-run: invariants skipped)")
        return 0

    for inv in contract.get("invariants", []):
        if "cmd" in inv:
            when = inv.get("when_path_exists")
            if when and not (root / when).exists():
                continue
            code, output = run_cmd(inv["cmd"], root)
            if code != 0:
                errors.append(f"invariant failed ({inv['cmd']}): exit {code}\n{output[-800:]}")
        elif inv.get("contract_tests_previously_passing") == "must_still_pass":
            errors.extend(verify_feature_list(root))

    patterns = []
    for inv in contract.get("invariants", []):
        patterns.extend(inv.get("forbidden_diffs_mechanical", []))
    if patterns:
        errors.extend(scan_forbidden_diffs(root, args.base_ref, patterns))

    proc = subprocess.run(
        ["git", "diff", args.base_ref, "--", "docs/handoff.md"],
        cwd=root,
        capture_output=True,
        text=True,
        check=True,
    )
    if not proc.stdout.strip():
        errors.append("postcondition: docs/handoff.md not updated since base ref")

    if errors:
        for err in errors:
            print(f"FAIL postflight: {err}", file=sys.stderr)
        return 1
    print("OK postflight")
    return 0


def cmd_verify_feature_list(_: argparse.Namespace) -> int:
    errors = verify_feature_list(repo_root())
    if errors:
        for err in errors:
            print(f"FAIL feature-list: {err}", file=sys.stderr)
        return 1
    print("OK feature-list")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="InGENeer agent session harness")
    sub = parser.add_subparsers(dest="command", required=True)

    p_pre = sub.add_parser("preflight")
    p_pre.add_argument("--dry-run", action="store_true", help="skip git clean check")
    p_pre.add_argument("--dirty-scope", action="append", default=[])

    p_post = sub.add_parser("postflight")
    p_post.add_argument("--dry-run", action="store_true", help="skip handoff + forbidden-diff checks")
    p_post.add_argument("--base-ref", default="HEAD")

    sub.add_parser("verify-feature-list")

    args = parser.parse_args()
    if args.command == "preflight":
        return cmd_preflight(args)
    if args.command == "postflight":
        return cmd_postflight(args)
    if args.command == "verify-feature-list":
        return cmd_verify_feature_list(args)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
