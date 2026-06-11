"""Smoke tests for tools/agentic session harness (Phase 0)."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def test_session_harness_preflight_dry_run() -> None:
    root = repo_root()
    proc = subprocess.run(
        [sys.executable, "tools/agentic/session_harness.py", "preflight", "--dry-run"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    assert proc.returncode == 0, proc.stderr
    assert "OK preflight" in proc.stdout


def test_session_harness_postflight_dry_run() -> None:
    root = repo_root()
    proc = subprocess.run(
        [sys.executable, "tools/agentic/session_harness.py", "postflight", "--dry-run"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    assert proc.returncode == 0, proc.stderr + proc.stdout
    assert "OK postflight" in proc.stdout


def test_compile_prompt_writes_non_empty() -> None:
    root = repo_root()
    out = root / "docs" / "specs" / "_compile_prompt_test.md"
    try:
        proc = subprocess.run(
            [
                sys.executable,
                "tools/agentic/compile_prompt.py",
                "-o",
                str(out),
            ],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        assert proc.returncode == 0, proc.stderr
        assert out.is_file()
        assert len(out.read_text(encoding="utf-8")) > 500
    finally:
        if out.exists():
            out.unlink()
