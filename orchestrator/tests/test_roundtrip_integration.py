import os
import subprocess
import time
import urllib.request
from pathlib import Path

import pytest

from ingenieer.audit import AuditLogger
from ingenieer.models import CadIntentEnvelope, OrchestratorConfig
from ingenieer.orchestrator import PipelineOrchestrator

# REPO_ROOT is two levels up from orchestrator/tests/
REPO_ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.integration
def test_pipeline_csharp_loopback_roundtrip(tmp_path):
    """Start the real C# LoopbackHost binary and run the Python Orchestrator against it."""
    # 1. Start C# LoopbackHost
    port = 8790
    url = f"http://127.0.0.1:{port}/"
    bin_path = (
        REPO_ROOT
        / "icad-addin"
        / "InGENeer.Bridge.LoopbackHost"
        / "bin"
        / "Debug"
        / "net10.0"
        / "InGENeer.Bridge.LoopbackHost"
    )

    if not bin_path.is_file():
        pytest.skip(f"C# LoopbackHost binary not found at {bin_path}. Run 'dotnet build' first.")

    proc = subprocess.Popen(
        [str(bin_path)],
        env={**os.environ, "INGENEER_BRIDGE_URL": url},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    try:
        # Wait for it to be ready
        max_retries = 10
        ready = False
        for _i in range(max_retries):
            try:
                with urllib.request.urlopen(f"{url}v1/model-fingerprint", timeout=0.5) as resp:
                    if resp.getcode() == 200:
                        ready = True
                        break
            except Exception:  # noqa: BLE001
                time.sleep(0.5)

        if not ready:
            stdout, stderr = proc.communicate(timeout=0.5)
            pytest.fail(f"C# LoopbackHost failed to start at {url}.\nSTDOUT: {stdout}\nSTDERR: {stderr}")

        # 2. Run Orchestrator against it
        audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="roundtrip")
        out = tmp_path / "out"
        out.mkdir()

        cfg = OrchestratorConfig(
            bridge={"mode": "http", "http_base_url": url.rstrip("/"), "timeout_sec": 5.0},
            intent_validation={"enforce_json_schema": True, "enforce_command_allowlist": True},
        )

        orch = PipelineOrchestrator(cfg, audit, out)

        # Test 1: PingHost
        intent_ping = CadIntentEnvelope(intentId="rt1", command="PingHost", parameters={})
        result_ping = orch.run(intent_ping)
        assert result_ping.success, f"PingHost failed: {result_ping.errors}"

        # Test 2: CreateAlignment (Exercises L6 patterns)
        intent_align = CadIntentEnvelope(
            intentId="rt2",
            command="CreateAlignment",
            parameters={
                "name": "RT-Alignment",
                "points": [[0.0, 0.0, 0.0], [100.0, 100.0, 10.0]],
                "start_station": 0.0,
                "layer": "ALIGNMENT",
            },
            humanConfirmationToken="rt-token",
        )
        result_align = orch.run(intent_align)
        assert result_align.success, f"CreateAlignment failed: {result_align.errors}"
        telemetry_align = result_align.phases[-2].data["bridge_execution"]["telemetry"]
        assert "length" in telemetry_align
        assert telemetry_align["length"] > 0

        # Test 3: ImportLandXmlSurface (Exercises Path Resolution)
        intent_lxml = CadIntentEnvelope(
            intentId="rt3",
            command="ImportLandXmlSurface",
            parameters={
                "landxml_path_key": "surface_file",
                "surface_name": "RT-Surface",
                "layer": "TOPO"
            },
            humanConfirmationToken="rt-token"
        )
        result_lxml = orch.run(intent_lxml)
        assert result_lxml.success, f"ImportLandXmlSurface failed: {result_lxml.errors}"
        telemetry_lxml = result_lxml.phases[-2].data["bridge_execution"]["telemetry"]
        # Match the stub path in Program.cs
        assert telemetry_lxml["resolved_path"] == "C:\\Data\\Existing_Ground.xml"

        # Test 4: BalanceGrading (High-risk, execute mode, needs token)
        intent_balance = CadIntentEnvelope(
            intentId="rt4",
            command="BalanceGrading",
            parameters={
                "existing_surface": "Existing Ground",
                "proposed_surface": "Proposed Design",
                "tolerance": 5.0,
                "shrink_swell_factor": 1.1
            },
            humanConfirmationToken="rt-token"
        )
        result_balance = orch.run(intent_balance)
        assert result_balance.success, f"BalanceGrading failed: {result_balance.errors}"
        telemetry_balance = result_balance.phases[-2].data["bridge_execution"]["telemetry"]
        assert "net_volume" in telemetry_balance
        assert telemetry_balance["balanced"] is True

        # Test 5: AnalyzeStormDrainage (Low-risk, no token needed)
        intent_storm = CadIntentEnvelope(
            intentId="rt5",
            command="AnalyzeStormDrainage",
            parameters={
                "network_name": "Main Street Storm",
                "design_storm_years": 25,
                "runoff_coefficient": 0.8
            }
        )
        result_storm = orch.run(intent_storm)
        assert result_storm.success, f"AnalyzeStormDrainage failed: {result_storm.errors}"
        telemetry_storm = result_storm.phases[-2].data["bridge_execution"]["telemetry"]
        assert "peak_discharge" in telemetry_storm
        assert "max_velocity" in telemetry_storm

        # Test 6: NoOp (Smoke test baseline)
        intent_noop = CadIntentEnvelope(intentId="rt6", command="NoOp", parameters={})
        result_noop = orch.run(intent_noop)
        assert result_noop.success, f"NoOp failed: {result_noop.errors}"
        telemetry_noop = result_noop.phases[-2].data["bridge_execution"]["telemetry"]
        assert telemetry_noop["status"] == "nop_success"

        # Test 7: Double-armored validation (Bypass Orchestrator, blocked by Bridge)
        print("Testing Double-Armored Validation...")
        cfg_bypass = OrchestratorConfig(
            bridge={"mode": "http", "http_base_url": url.rstrip("/"), "timeout_sec": 5.0},
            intent_validation={"enforce_json_schema": False, "enforce_command_allowlist": True},
        )
        orch_bypass = PipelineOrchestrator(cfg_bypass, audit, out)

        intent_invalid = CadIntentEnvelope(
            intentId="rt7",
            command="CreateAlignment",
            parameters={
                "name": "Invalid Alignment",
                # "points" is intentionally missing
                "start_station": 0.0,
                "layer": "ALIGNMENT"
            },
            humanConfirmationToken="rt-token"
        )

        result_invalid = orch_bypass.run(intent_invalid)
        assert not result_invalid.success, "Bridge failed to reject invalid intent!"

        # The failure should happen during dispatch_execute
        dispatch_phase = next(p for p in result_invalid.phases if p.phase == "dispatch_execute")
        assert not dispatch_phase.success
        assert "Parameter validation failed" in dispatch_phase.message
        assert "points" in dispatch_phase.message.lower() # The missing required property

        print("Double-armored validation PASSED.")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()
