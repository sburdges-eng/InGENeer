"""Tests for ingenieer-run CLI (ingenieer.cli.main)."""

import json
import subprocess
import sys

INTENT_NOOP = json.dumps({"intentId": "cli-1", "command": "NoOp", "parameters": {}})

INTENT_ALIGNMENT = json.dumps({
    "intentId": "cli-2",
    "command": "CreateAlignment",
    "parameters": {
        "name": "Test CL",
        "points": [[0, 0, 0], [100, 100, 10]],
        "start_station": 0,
        "layer": "ALIGNMENT",
    },
})


def _run_cli(*args: str, stdin: str | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "ingenieer.cli", *args],
        input=stdin,
        capture_output=True,
        text=True,
        timeout=10,
    )


class TestBasicPipeline:
    def test_noop_from_stdin(self):
        r = _run_cli("--dry-run", stdin=INTENT_NOOP)
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["success"] is True
        assert len(out["phases"]) == 4

    def test_noop_from_file(self, tmp_path):
        f = tmp_path / "intent.json"
        f.write_text(INTENT_NOOP)
        r = _run_cli(str(f), "--dry-run")
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["success"] is True

    def test_missing_file_exits_nonzero(self):
        r = _run_cli("/nonexistent/path.json")
        assert r.returncode != 0


class TestExecutionModeOverrides:
    def test_dry_run_flag(self):
        r = _run_cli("--dry-run", stdin=INTENT_NOOP)
        out = json.loads(r.stdout)
        tel = out["phases"][2]["data"]["bridge_execution"]["telemetry"]
        assert tel["executionMode"] == "dry_run"

    def test_preview_flag(self):
        r = _run_cli("--preview", stdin=INTENT_NOOP)
        out = json.loads(r.stdout)
        tel = out["phases"][2]["data"]["bridge_execution"]["telemetry"]
        assert tel["executionMode"] == "preview"

    def test_dry_run_and_preview_conflict(self):
        r = _run_cli("--dry-run", "--preview", stdin=INTENT_NOOP)
        assert r.returncode == 2
        assert "only one" in r.stderr.lower()


class TestConfirmationToken:
    def test_i_confirm_sets_token(self):
        intent = json.dumps({
            "intentId": "cli-tok",
            "command": "DrawPolylineFromCoordinates",
            "parameters": {
                "points": [[0, 0, 0], [10, 10, 1]],
                "layer": "BOUNDARY",
                "closed": False,
            },
        })
        r = _run_cli("--i-confirm", "operator-yes", stdin=intent)
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["success"] is True

    def test_empty_i_confirm_rejected(self):
        r = _run_cli("--i-confirm", "  ", stdin=INTENT_NOOP)
        assert r.returncode == 2
        assert "non-empty" in r.stderr.lower()


class TestPrintPlan:
    def test_print_plan_exits_after_validate(self):
        r = _run_cli("--print-plan", "--dry-run", stdin=INTENT_NOOP)
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["success"] is True
        assert len(out["phases"]) == 1
        assert out["phases"][0]["phase"] == "validate_intent"
        assert "intent" in out
        assert "intent_schema_path" in out

    def test_print_plan_invalid_intent_exits_nonzero(self):
        bad = json.dumps({"intentId": "x", "command": "NotInCatalog", "parameters": {}})
        r = _run_cli("--print-plan", stdin=bad)
        assert r.returncode == 1
        out = json.loads(r.stdout)
        assert out["success"] is False
        assert any("allowlist" in e for e in out["errors"])


class TestPhaseSelection:
    def test_phase_validate_only(self):
        r = _run_cli("--phase", "validate_intent", "--dry-run", stdin=INTENT_NOOP)
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert len(out["phases"]) == 1
        assert out["phases"][0]["phase"] == "validate_intent"

    def test_phase_through_dispatch(self):
        r = _run_cli("--phase", "dispatch_execute", "--dry-run", stdin=INTENT_NOOP)
        assert r.returncode == 0
        out = json.loads(r.stdout)
        phases = [p["phase"] for p in out["phases"]]
        assert phases == ["validate_intent", "sync_baseline", "dispatch_execute"]


class TestConfigFile:
    def test_config_file_applied(self, tmp_path):
        cfg = tmp_path / "config.json"
        cfg.write_text(json.dumps({"project": {"name": "cli-test-proj"}}))
        r = _run_cli("--config", str(cfg), "--dry-run", stdin=INTENT_NOOP)
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["project_id"] == "cli-test-proj"


class TestValidationErrors:
    def test_high_risk_without_token_fails(self):
        intent = json.dumps({
            "intentId": "no-tok",
            "command": "DrawPolylineFromCoordinates",
            "parameters": {
                "points": [[0, 0, 0], [10, 10, 1]],
                "layer": "BOUNDARY",
                "closed": False,
            },
            "executionMode": "execute",
        })
        r = _run_cli(stdin=intent)
        assert r.returncode == 1
        out = json.loads(r.stdout)
        assert out["success"] is False
        assert any("humanConfirmationToken" in e for e in out["errors"])
