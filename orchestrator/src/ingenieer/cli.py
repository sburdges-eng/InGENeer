"""Thin CLI: run the pipeline for one intent JSON envelope (or a batch contract)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from ingenieer.audit import AuditLogger
from ingenieer.batch import BatchPipeline, ProjectContract
from ingenieer.intent_validation import default_intent_schema_path
from ingenieer.models import CadIntentEnvelope, OrchestratorConfig
from ingenieer.orchestrator import PipelineOrchestrator


def main() -> None:
    parser = argparse.ArgumentParser(description="InGENeer: run orchestrator pipeline for one intent JSON.")
    parser.add_argument(
        "intent",
        nargs="?",
        type=Path,
        help="Path to JSON file (omit to read stdin)",
    )
    parser.add_argument(
        "--batch",
        nargs="?",
        const="_stdin_",
        default=None,
        metavar="CONTRACT_FILE",
        help="Run a batch of intents from a project contract JSON (omit path to read stdin)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("ingenieer_out"),
        help="Writable directory for pipeline outputs / context",
    )
    parser.add_argument(
        "--audit-dir",
        type=Path,
        default=Path("audit_logs"),
        help="Directory for JSONL audit logs",
    )
    parser.add_argument(
        "--config",
        type=Path,
        help="Optional JSON file merged into OrchestratorConfig",
    )
    parser.add_argument(
        "--phase",
        default="all",
        help="Run through this phase only (validate_intent, sync_baseline, dispatch_execute, verify_result, or all)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Force intent executionMode to dry_run (plan without committed host mutation)",
    )
    parser.add_argument(
        "--preview",
        action="store_true",
        help="Force intent executionMode to preview",
    )
    parser.add_argument(
        "--i-confirm",
        metavar="TOKEN",
        default=None,
        help="Set humanConfirmationToken for high-risk execute intents (non-interactive approval)",
    )
    parser.add_argument(
        "--print-plan",
        action="store_true",
        help="After validate_intent, print normalized intent + schema path and exit 0 (no dispatch)",
    )
    args = parser.parse_args()

    if args.dry_run and args.preview:
        print("error: use only one of --dry-run and --preview", file=sys.stderr)
        sys.exit(2)

    cfg_dict: dict = {}
    if args.config and args.config.is_file():
        cfg_dict = json.loads(args.config.read_text(encoding="utf-8"))

    config = OrchestratorConfig.model_validate(cfg_dict)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    # ── Batch mode ──────────────────────────────────────────────
    if args.batch is not None:
        raw_contract = _read_contract_json(args.batch)
        contract = ProjectContract.model_validate(raw_contract)

        if args.dry_run:
            contract = contract.model_copy(update={"execution_mode_override": "dry_run"})
        elif args.preview:
            contract = contract.model_copy(update={"execution_mode_override": "preview"})
        if args.i_confirm is not None:
            token = args.i_confirm.strip()
            if not token:
                print("error: --i-confirm must be non-empty", file=sys.stderr)
                sys.exit(2)
            contract = contract.model_copy(update={"human_confirmation_token": token})

        audit = AuditLogger(
            log_dir=str(args.audit_dir),
            project_id=contract.project.name,
            hash_algo=config.audit.hash_algorithm,
        )
        pipeline = BatchPipeline(config, audit, args.output_dir)
        batch_result = pipeline.run(contract)

        out = {
            "success": batch_result.success,
            "project_id": batch_result.project_id,
            "completed_steps": batch_result.completed_steps,
            "total_steps": batch_result.total_steps,
            "last_good_fingerprint": batch_result.last_good_fingerprint,
            "errors": batch_result.errors,
            "steps": [
                {
                    "intent_id": s.intent_id,
                    "command": s.command,
                    "success": s.pipeline_result.success,
                    "fingerprint_before": s.fingerprint_before,
                    "fingerprint_after": s.fingerprint_after,
                }
                for s in batch_result.step_results
            ],
        }
        print(json.dumps(out, indent=2))
        sys.exit(0 if batch_result.success else 1)

    # ── Single-intent mode ──────────────────────────────────────
    raw = _read_intent_json(args.intent)
    intent = CadIntentEnvelope.model_validate(raw)

    if args.dry_run:
        intent = intent.model_copy(update={"executionMode": "dry_run"})
    elif args.preview:
        intent = intent.model_copy(update={"executionMode": "preview"})
    if args.i_confirm is not None:
        token = args.i_confirm.strip()
        if not token:
            print("error: --i-confirm must be non-empty", file=sys.stderr)
            sys.exit(2)
        intent = intent.model_copy(update={"humanConfirmationToken": token})

    audit = AuditLogger(
        log_dir=str(args.audit_dir),
        project_id=config.project.name,
        hash_algo=config.audit.hash_algorithm,
    )
    orch = PipelineOrchestrator(config, audit, args.output_dir)

    if args.print_plan:
        result = orch.run(intent, phase="validate_intent")
        plan_out = {
            "success": result.success,
            "intent_schema_path": str(default_intent_schema_path()),
            "intent": intent.model_dump(mode="json"),
            "errors": result.errors,
            "phases": [p.model_dump(mode="json") for p in result.phases],
        }
        print(json.dumps(plan_out, indent=2))
        sys.exit(0 if result.success else 1)

    result = orch.run(intent, phase=args.phase)

    last_verify = None
    for p in reversed(result.phases):
        if p.phase == "verify_result":
            last_verify = p.data.get("verification")
            break

    out = {
        "success": result.success,
        "project_id": result.project_id,
        "errors": result.errors,
        "phases": [p.model_dump(mode="json") for p in result.phases],
        "verification": last_verify,
    }
    print(json.dumps(out, indent=2))
    sys.exit(0 if result.success else 1)


def _read_contract_json(path_or_sentinel: str) -> object:
    """Read a project contract JSON from stdin or a file path."""
    if path_or_sentinel == "_stdin_":
        return json.load(sys.stdin)
    p = Path(path_or_sentinel)
    if not p.is_file():
        raise SystemExit(f"contract file not found: {p}")
    return json.loads(p.read_text(encoding="utf-8"))


def _read_intent_json(path: Path | None) -> object:
    if path is None:
        return json.load(sys.stdin)
    if not path.is_file():
        raise SystemExit(f"intent file not found: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
