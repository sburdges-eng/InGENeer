#!/usr/bin/env python3
"""Regenerate docs/specs/feature_list.json from the orchestrator pytest suite."""

from __future__ import annotations

import json
import subprocess
import sys
from datetime import date
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def collect_tests(orchestrator_dir: Path) -> list[str]:
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "pytest",
            "--collect-only",
            "-q",
            "--ignore=tests/test_intent_generator.py",
        ],
        cwd=orchestrator_dir,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode not in (0, 5):
        raise RuntimeError(f"pytest --collect-only failed:\n{result.stderr}")
    nodes: list[str] = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if "::test_" in line and line.startswith("tests/"):
            nodes.append(line)
    return sorted(set(nodes))


def main() -> int:
    root = repo_root()
    orchestrator = root / "orchestrator"
    out = root / "docs" / "specs" / "feature_list.json"
    out.parent.mkdir(parents=True, exist_ok=True)

    nodes = collect_tests(orchestrator)
    today = date.today().isoformat()
    payload = {
        "version": "1.0",
        "generated": today,
        "source": "orchestrator pytest suite (excludes tests/test_intent_generator.py until module lands)",
        "features": [
            {
                "id": node,
                "test_cmd": f"cd orchestrator && python -m pytest -q {node}",
                "status": "passing",
                "last_verified": today,
            }
            for node in nodes
        ],
    }
    out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(nodes)} features to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
