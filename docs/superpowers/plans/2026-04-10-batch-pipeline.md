# Batch Pipeline + Project Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable sequential execution of multiple intents in a single pipeline run, with fingerprint threading between steps, per-step rollback semantics, and a project-level contract envelope.

**Architecture:** A `BatchPipeline` wraps the existing `PipelineOrchestrator`, running each intent sequentially. After each successful dispatch+verify, the resulting `modelFingerprintAfter` is threaded into the next intent's `modelFingerprintExpected`. If any step fails, the batch halts and reports which steps succeeded, which failed, and the last known good fingerprint. A `ProjectContract` model defines the batch input format (project metadata + ordered intent list). The CLI gets a `--batch` flag to accept a project contract JSON file.

**Tech Stack:** Python 3.11+, pydantic, pytest, existing ingenieer modules

---

## File Structure

| File | Responsibility |
|------|----------------|
| `orchestrator/src/ingenieer/batch.py` | **Create.** `ProjectContract` model, `BatchResult` model, `BatchPipeline` class with `run(contract)` method. |
| `orchestrator/src/ingenieer/cli.py` | **Modify.** Add `--batch` flag that accepts a project contract JSON file and runs `BatchPipeline`. |
| `orchestrator/tests/test_batch.py` | **Create.** Unit tests for batch pipeline: happy path, mid-sequence failure, fingerprint threading, empty intents, dry_run mode. |
| `orchestrator/tests/test_cli.py` | **Modify.** Add CLI tests for `--batch` flag. |
| `schemas/project_contract.schema.json` | **Create.** JSON Schema for the project contract input format. |

---

### Task 1: ProjectContract and BatchResult Models

**Files:**
- Create: `orchestrator/src/ingenieer/batch.py`
- Create: `orchestrator/tests/test_batch.py`

- [ ] **Step 1: Write failing tests for the models**

```python
# orchestrator/tests/test_batch.py
from ingenieer.batch import BatchResult, ProjectContract
from ingenieer.models import CadIntentEnvelope


def test_project_contract_parses_minimal():
    raw = {
        "project": {"name": "site-grading", "version": "1.0.0"},
        "intents": [
            {"intentId": "s1", "command": "NoOp", "parameters": {}},
        ],
    }
    pc = ProjectContract.model_validate(raw)
    assert pc.project.name == "site-grading"
    assert len(pc.intents) == 1
    assert isinstance(pc.intents[0], CadIntentEnvelope)


def test_project_contract_rejects_empty_intents():
    import pytest

    with pytest.raises(Exception):
        ProjectContract.model_validate(
            {"project": {"name": "empty"}, "intents": []}
        )


def test_batch_result_defaults():
    br = BatchResult(project_id="test")
    assert br.success is True
    assert br.completed_steps == 0
    assert br.total_steps == 0
    assert br.step_results == []
    assert br.last_good_fingerprint is None
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd orchestrator && python3 -m pytest tests/test_batch.py -q`
Expected: FAIL — module not found

- [ ] **Step 3: Implement models**

```python
# orchestrator/src/ingenieer/batch.py
"""Batch pipeline: run a sequence of intents with fingerprint threading."""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, Field, field_validator

from ingenieer.models import (
    CadIntentEnvelope,
    PipelineResult,
    ProjectConfig,
)


class ProjectContract(BaseModel):
    """Project-level envelope: metadata + ordered list of intents to execute."""

    project: ProjectConfig = Field(default_factory=ProjectConfig)
    intents: list[CadIntentEnvelope] = Field(min_length=1)
    execution_mode_override: str | None = Field(
        default=None,
        description="If set, override executionMode on all intents (dry_run, preview).",
    )
    human_confirmation_token: str | None = Field(
        default=None,
        description="If set, apply to all high-risk intents that lack one.",
    )

    @field_validator("intents")
    @classmethod
    def _validate_unique_intent_ids(cls, v: list[CadIntentEnvelope]) -> list[CadIntentEnvelope]:
        ids = [i.intentId for i in v]
        dupes = [x for x in ids if ids.count(x) > 1]
        if dupes:
            raise ValueError(f"duplicate intentIds in batch: {set(dupes)}")
        return v


class StepResult(BaseModel):
    """Result of one intent within a batch."""

    step_index: int
    intent_id: str
    command: str
    pipeline_result: PipelineResult
    fingerprint_before: str | None = None
    fingerprint_after: str | None = None


class BatchResult(BaseModel):
    """Aggregate result of a batch pipeline run."""

    project_id: str
    success: bool = True
    completed_steps: int = 0
    total_steps: int = 0
    step_results: list[StepResult] = Field(default_factory=list)
    last_good_fingerprint: str | None = None
    errors: list[str] = Field(default_factory=list)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd orchestrator && python3 -m pytest tests/test_batch.py -q`
Expected: 3 passed

- [ ] **Step 5: Commit**

```bash
git add orchestrator/src/ingenieer/batch.py orchestrator/tests/test_batch.py
git commit -m "feat: add ProjectContract and BatchResult models"
```

---

### Task 2: BatchPipeline Runner

**Files:**
- Modify: `orchestrator/src/ingenieer/batch.py`
- Modify: `orchestrator/tests/test_batch.py`

- [ ] **Step 1: Write failing tests for batch execution**

Add to `orchestrator/tests/test_batch.py`:

```python
from ingenieer.audit import AuditLogger
from ingenieer.batch import BatchPipeline, BatchResult, ProjectContract
from ingenieer.models import CadIntentEnvelope, OrchestratorConfig


def test_batch_runs_all_intents(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="batch-test")
    out = tmp_path / "out"
    out.mkdir()
    contract = ProjectContract.model_validate({
        "project": {"name": "batch-test"},
        "intents": [
            {"intentId": "b1", "command": "NoOp", "parameters": {}},
            {"intentId": "b2", "command": "PingHost", "parameters": {}},
            {"intentId": "b3", "command": "GetModelFingerprint", "parameters": {}},
        ],
    })
    bp = BatchPipeline(OrchestratorConfig(), audit, out)
    result = bp.run(contract)

    assert result.success
    assert result.completed_steps == 3
    assert result.total_steps == 3
    assert len(result.step_results) == 3
    assert all(sr.pipeline_result.success for sr in result.step_results)


def test_batch_halts_on_failure(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="batch-fail")
    out = tmp_path / "out"
    out.mkdir()
    contract = ProjectContract.model_validate({
        "project": {"name": "batch-fail"},
        "intents": [
            {"intentId": "ok1", "command": "NoOp", "parameters": {}},
            {
                "intentId": "fail1",
                "command": "DrawPolylineFromCoordinates",
                "parameters": {"points": [[0, 0, 0], [1, 1, 1]], "layer": "X", "closed": False},
                "executionMode": "execute",
                # Missing humanConfirmationToken for high-risk command
            },
            {"intentId": "never", "command": "NoOp", "parameters": {}},
        ],
    })
    bp = BatchPipeline(OrchestratorConfig(), audit, out)
    result = bp.run(contract)

    assert not result.success
    assert result.completed_steps == 1  # only NoOp succeeded
    assert result.total_steps == 3
    assert len(result.step_results) == 2  # NoOp + failed DrawPolyline
    assert result.step_results[0].pipeline_result.success
    assert not result.step_results[1].pipeline_result.success


def test_batch_threads_fingerprint(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="batch-fp")
    out = tmp_path / "out"
    out.mkdir()
    contract = ProjectContract.model_validate({
        "project": {"name": "batch-fp"},
        "intents": [
            {"intentId": "fp1", "command": "NoOp", "parameters": {}},
            {"intentId": "fp2", "command": "NoOp", "parameters": {}},
        ],
    })
    bp = BatchPipeline(OrchestratorConfig(), audit, out)
    result = bp.run(contract)

    assert result.success
    # Second step should have fingerprint_before set from first step's fingerprint_after
    assert result.step_results[1].fingerprint_before is not None
    assert result.step_results[0].fingerprint_after == result.step_results[1].fingerprint_before


def test_batch_execution_mode_override(tmp_path):
    audit = AuditLogger(log_dir=str(tmp_path / "audit"), project_id="batch-dry")
    out = tmp_path / "out"
    out.mkdir()
    contract = ProjectContract.model_validate({
        "project": {"name": "batch-dry"},
        "intents": [
            {
                "intentId": "dry1",
                "command": "DrawPolylineFromCoordinates",
                "parameters": {"points": [[0, 0, 0], [1, 1, 1]], "layer": "X", "closed": False},
                "executionMode": "execute",
            },
        ],
        "execution_mode_override": "dry_run",
    })
    bp = BatchPipeline(OrchestratorConfig(), audit, out)
    result = bp.run(contract)

    # Should succeed because dry_run doesn't need humanConfirmationToken
    assert result.success


def test_batch_empty_contract_rejected():
    import pytest

    with pytest.raises(Exception):
        ProjectContract.model_validate({
            "project": {"name": "empty"},
            "intents": [],
        })


def test_batch_duplicate_intent_ids_rejected():
    import pytest

    with pytest.raises(Exception, match="duplicate"):
        ProjectContract.model_validate({
            "project": {"name": "dupes"},
            "intents": [
                {"intentId": "same", "command": "NoOp", "parameters": {}},
                {"intentId": "same", "command": "PingHost", "parameters": {}},
            ],
        })
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd orchestrator && python3 -m pytest tests/test_batch.py -q`
Expected: FAIL — BatchPipeline not found

- [ ] **Step 3: Implement BatchPipeline**

Add to `orchestrator/src/ingenieer/batch.py`:

```python
from pathlib import Path

from ingenieer.audit import AuditLogger
from ingenieer.bridge_client import BridgeClient, create_bridge_client
from ingenieer.orchestrator import PipelineOrchestrator


class BatchPipeline:
    """Run a ProjectContract: sequential intents with fingerprint threading."""

    def __init__(
        self,
        config: OrchestratorConfig,
        audit: AuditLogger,
        output_dir: Path,
        *,
        bridge: BridgeClient | None = None,
    ) -> None:
        self._config = config
        self._audit = audit
        self._output_dir = Path(output_dir)
        self._bridge = bridge

    def run(self, contract: ProjectContract) -> BatchResult:
        config = self._config.model_copy(
            update={"project": contract.project}
        )
        bridge = self._bridge or create_bridge_client(config)
        orch = PipelineOrchestrator(config, self._audit, self._output_dir, bridge=bridge)

        result = BatchResult(
            project_id=contract.project.name,
            total_steps=len(contract.intents),
        )
        current_fingerprint: str | None = None

        for idx, intent in enumerate(contract.intents):
            # Apply contract-level overrides
            updates: dict[str, Any] = {}
            if contract.execution_mode_override:
                updates["executionMode"] = contract.execution_mode_override
            if contract.human_confirmation_token and not intent.humanConfirmationToken:
                updates["humanConfirmationToken"] = contract.human_confirmation_token
            if current_fingerprint:
                updates["modelFingerprintExpected"] = current_fingerprint
            if updates:
                intent = intent.model_copy(update=updates)

            self._audit.log("batch_step_start", {
                "step": idx,
                "intentId": intent.intentId,
                "command": intent.command,
            })

            pr = orch.run(intent)

            # Extract fingerprint from dispatch telemetry
            fp_after: str | None = None
            for phase in pr.phases:
                if phase.phase == "dispatch_execute" and phase.success:
                    be = phase.data.get("bridge_execution", {})
                    tel = be.get("telemetry", {})
                    fp_after = tel.get("modelFingerprintAfter")

            step = StepResult(
                step_index=idx,
                intent_id=intent.intentId,
                command=intent.command,
                pipeline_result=pr,
                fingerprint_before=current_fingerprint,
                fingerprint_after=fp_after,
            )
            result.step_results.append(step)

            if pr.success:
                result.completed_steps += 1
                if fp_after:
                    current_fingerprint = fp_after
                    result.last_good_fingerprint = fp_after
                self._audit.log("batch_step_complete", {
                    "step": idx,
                    "intentId": intent.intentId,
                    "fingerprint_after": fp_after,
                })
            else:
                result.success = False
                result.errors.extend(pr.errors)
                self._audit.log("batch_step_failed", {
                    "step": idx,
                    "intentId": intent.intentId,
                    "errors": pr.errors,
                })
                break  # halt on first failure

        self._audit.log("batch_complete", {
            "success": result.success,
            "completed": result.completed_steps,
            "total": result.total_steps,
            "last_good_fingerprint": result.last_good_fingerprint,
        })
        return result
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd orchestrator && python3 -m pytest tests/test_batch.py -q`
Expected: 9 passed (3 model + 6 pipeline)

- [ ] **Step 5: Lint and commit**

```bash
cd orchestrator && ruff check src/ingenieer/batch.py tests/test_batch.py
git add orchestrator/src/ingenieer/batch.py orchestrator/tests/test_batch.py
git commit -m "feat: add BatchPipeline with fingerprint threading and halt-on-failure"
```

---

### Task 3: Project Contract JSON Schema

**Files:**
- Create: `schemas/project_contract.schema.json`

- [ ] **Step 1: Write the schema**

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://ingenieer.local/schemas/project_contract.schema.json",
  "title": "ProjectContract",
  "description": "Batch input: project metadata + ordered list of intent envelopes.",
  "type": "object",
  "required": ["intents"],
  "properties": {
    "project": {
      "type": "object",
      "properties": {
        "name": { "type": "string", "minLength": 1 },
        "version": { "type": "string" }
      }
    },
    "intents": {
      "type": "array",
      "minItems": 1,
      "items": { "$ref": "cad_intent_envelope.schema.json" }
    },
    "execution_mode_override": {
      "oneOf": [
        { "type": "string", "enum": ["dry_run", "preview"] },
        { "type": "null" }
      ]
    },
    "human_confirmation_token": {
      "oneOf": [
        { "type": "string", "minLength": 1 },
        { "type": "null" }
      ]
    }
  }
}
```

- [ ] **Step 2: Commit**

```bash
git add schemas/project_contract.schema.json
git commit -m "feat: add project_contract.schema.json for batch input validation"
```

---

### Task 4: CLI --batch Flag

**Files:**
- Modify: `orchestrator/src/ingenieer/cli.py`
- Modify: `orchestrator/tests/test_cli.py`

- [ ] **Step 1: Write failing CLI tests**

Add to `orchestrator/tests/test_cli.py`:

```python
import json
import subprocess
import sys


def _run_cli(*args: str, stdin: str | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "ingenieer.cli", *args],
        input=stdin,
        capture_output=True,
        text=True,
        timeout=10,
    )


BATCH_CONTRACT = json.dumps({
    "project": {"name": "cli-batch-test"},
    "intents": [
        {"intentId": "cb1", "command": "NoOp", "parameters": {}},
        {"intentId": "cb2", "command": "PingHost", "parameters": {}},
    ],
})


class TestBatchMode:
    def test_batch_from_stdin(self):
        r = _run_cli("--batch", "--dry-run", stdin=BATCH_CONTRACT)
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["success"] is True
        assert out["completed_steps"] == 2
        assert out["total_steps"] == 2

    def test_batch_from_file(self, tmp_path):
        f = tmp_path / "contract.json"
        f.write_text(BATCH_CONTRACT)
        r = _run_cli("--batch", str(f), "--dry-run")
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["success"] is True

    def test_batch_with_failure_exits_nonzero(self):
        contract = json.dumps({
            "project": {"name": "cli-batch-fail"},
            "intents": [
                {
                    "intentId": "bf1",
                    "command": "DrawPolylineFromCoordinates",
                    "parameters": {"points": [[0, 0, 0], [1, 1, 1]], "layer": "X", "closed": False},
                    "executionMode": "execute",
                },
            ],
        })
        r = _run_cli("--batch", stdin=contract)
        assert r.returncode == 1
        out = json.loads(r.stdout)
        assert out["success"] is False

    def test_batch_and_single_intent_conflict(self):
        r = _run_cli("--batch", stdin='{"intents":[]}')
        # Should fail because empty intents
        assert r.returncode != 0
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd orchestrator && python3 -m pytest tests/test_cli.py::TestBatchMode -q`
Expected: FAIL — unrecognized argument `--batch`

- [ ] **Step 3: Add --batch to CLI**

In `orchestrator/src/ingenieer/cli.py`, add the `--batch` argument and batch execution path:

```python
    parser.add_argument(
        "--batch",
        nargs="?",
        const="_stdin_",
        default=None,
        metavar="CONTRACT_FILE",
        help="Run a batch of intents from a project contract JSON (omit path to read stdin)",
    )
```

In the `main()` function, add the batch path before the single-intent path:

```python
    if args.batch is not None:
        raw_contract = _read_contract_json(args.batch)
        from ingenieer.batch import BatchPipeline, ProjectContract

        contract = ProjectContract.model_validate(raw_contract)
        if args.dry_run:
            contract = contract.model_copy(update={"execution_mode_override": "dry_run"})
        elif args.preview:
            contract = contract.model_copy(update={"execution_mode_override": "preview"})
        if args.i_confirm:
            contract = contract.model_copy(update={"human_confirmation_token": args.i_confirm})

        config = OrchestratorConfig.model_validate(cfg_dict)
        config = config.model_copy(update={"project": contract.project})
        args.output_dir.mkdir(parents=True, exist_ok=True)
        audit = AuditLogger(
            log_dir=str(args.audit_dir),
            project_id=contract.project.name,
            hash_algo=config.audit.hash_algorithm,
        )
        bp = BatchPipeline(config, audit, args.output_dir)
        result = bp.run(contract)

        out = {
            "success": result.success,
            "project_id": result.project_id,
            "completed_steps": result.completed_steps,
            "total_steps": result.total_steps,
            "last_good_fingerprint": result.last_good_fingerprint,
            "errors": result.errors,
            "steps": [
                {
                    "intent_id": sr.intent_id,
                    "command": sr.command,
                    "success": sr.pipeline_result.success,
                    "fingerprint_before": sr.fingerprint_before,
                    "fingerprint_after": sr.fingerprint_after,
                }
                for sr in result.step_results
            ],
        }
        print(json.dumps(out, indent=2))
        sys.exit(0 if result.success else 1)
```

Add the helper:

```python
def _read_contract_json(path_or_sentinel: str) -> object:
    if path_or_sentinel == "_stdin_":
        return json.load(sys.stdin)
    p = Path(path_or_sentinel)
    if not p.is_file():
        raise SystemExit(f"contract file not found: {p}")
    return json.loads(p.read_text(encoding="utf-8"))
```

- [ ] **Step 4: Run tests**

Run: `cd orchestrator && python3 -m pytest tests/test_cli.py -q`
Expected: all pass (existing + new batch tests)

- [ ] **Step 5: Run full suite**

Run: `cd orchestrator && ruff check src tests && python3 -m pytest -q`
Expected: all pass

- [ ] **Step 6: Commit**

```bash
git add orchestrator/src/ingenieer/cli.py orchestrator/tests/test_cli.py
git commit -m "feat: add --batch flag to ingenieer-run CLI for project contract execution"
```

---

### Task 5: Final Verification

**Files:** None (verification only)

- [ ] **Step 1: Full lint**

Run: `cd orchestrator && ruff check src tests`
Expected: All checks passed

- [ ] **Step 2: Full test suite**

Run: `cd orchestrator && python3 -m pytest -q`
Expected: all pass (~170+ tests)

- [ ] **Step 3: C# build still works**

Run: `dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release`
Expected: 0 warnings

- [ ] **Step 4: Test batch CLI end-to-end**

```bash
echo '{"project":{"name":"e2e"},"intents":[{"intentId":"e1","command":"NoOp","parameters":{}},{"intentId":"e2","command":"PingHost","parameters":{}}]}' | python3 -m ingenieer.cli --batch --dry-run
```

Expected: JSON output with `success: true`, `completed_steps: 2`

- [ ] **Step 5: Push branch**

```bash
git push -u origin feat/batch-pipeline
```
