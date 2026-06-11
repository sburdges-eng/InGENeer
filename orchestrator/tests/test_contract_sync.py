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
    original = models.read_text(encoding="utf-8")
    text = original.replace('INTENT_SCHEMA_VERSION = "1.1.0"', 'INTENT_SCHEMA_VERSION = "9.9.9"')
    assert text != original, "fixture literal stale: INTENT_SCHEMA_VERSION line not found"
    models.write_text(text, encoding="utf-8")

    result = _run(repo_root=tmp_path)
    assert result.returncode == 1
    assert "INTENT_SCHEMA_VERSION" in result.stdout or "envelope" in result.stdout.lower()


def test_missing_allowlist_command_in_catalog(tmp_path: Path) -> None:
    _materialize_fixture(tmp_path)
    iv = tmp_path / "orchestrator/src/ingenieer/intent_validation.py"
    original = iv.read_text(encoding="utf-8")
    # Inject a fake command into the allowlist that is absent from the catalog.
    text = original.replace('"NoOp",', '"NoOp",\n        "TotallyFakeCommand",', 1)
    assert text != original, "fixture literal stale: allowlist NoOp entry not found"
    iv.write_text(text, encoding="utf-8")

    result = _run(repo_root=tmp_path)
    assert result.returncode == 1
    assert "TotallyFakeCommand" in result.stdout
