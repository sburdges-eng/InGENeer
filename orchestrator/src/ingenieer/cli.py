"""Thin CLI: run the pipeline for one intent JSON envelope."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from ingenieer.audit import AuditLogger
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
    args = parser.parse_args()

    raw = _read_intent_json(args.intent)
    intent = CadIntentEnvelope.model_validate(raw)

    cfg_dict: dict = {}
    if args.config and args.config.is_file():
        cfg_dict = json.loads(args.config.read_text(encoding="utf-8"))

    config = OrchestratorConfig.model_validate(cfg_dict)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    audit = AuditLogger(
        log_dir=str(args.audit_dir),
        project_id=config.project.name,
        hash_algo=config.audit.hash_algorithm,
    )
    orch = PipelineOrchestrator(config, audit, args.output_dir)
    result = orch.run(intent, phase=args.phase)

    out = {
        "success": result.success,
        "project_id": result.project_id,
        "errors": result.errors,
        "phases": [p.model_dump(mode="json") for p in result.phases],
    }
    print(json.dumps(out, indent=2))
    sys.exit(0 if result.success else 1)


def _read_intent_json(path: Path | None) -> object:
    if path is None:
        return json.load(sys.stdin)
    if not path.is_file():
        raise SystemExit(f"intent file not found: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
