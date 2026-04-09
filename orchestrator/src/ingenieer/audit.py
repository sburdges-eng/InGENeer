"""
Append-only audit log (JSONL) with SHA-256 chaining for chain-of-custody.

Ported from TOTaLi (`totali/audit/logger.py`); compatible verification semantics.
"""

from __future__ import annotations

import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


class AuditLogger:
    def __init__(
        self,
        log_dir: str = "audit_logs",
        project_id: str = "unknown",
        hash_algo: str = "sha256",
    ) -> None:
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.project_id = project_id
        self.hash_algo = hash_algo
        self.log_path = (
            self.log_dir / f"{project_id}_{datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')}.jsonl"
        )
        self._prev_hash = "0" * 64
        self._seq = 0

    def log(self, event_type: str, data: dict[str, Any] | None = None) -> None:
        """Append one auditable event with hash chaining."""
        self._seq += 1
        timestamp = datetime.now(timezone.utc).isoformat()

        record: dict[str, Any] = {
            "seq": self._seq,
            "timestamp": timestamp,
            "project_id": self.project_id,
            "event": event_type,
            "data": data or {},
            "prev_hash": self._prev_hash,
        }

        record_bytes = json.dumps(record, sort_keys=True, default=str).encode()
        record_hash = hashlib.new(self.hash_algo, record_bytes).hexdigest()
        record["hash"] = record_hash
        self._prev_hash = record_hash

        with self.log_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(record, default=str) + "\n")

    def verify_chain(self) -> tuple[bool, list[str]]:
        """Verify integrity of the audit log hash chain."""
        if not self.log_path.exists():
            return True, []

        errors: list[str] = []
        prev_hash = "0" * 64

        with self.log_path.open(encoding="utf-8") as handle:
            for line_num, line in enumerate(handle, 1):
                record = json.loads(line.strip())
                stored_hash = record.pop("hash")

                if record["prev_hash"] != prev_hash:
                    errors.append(
                        f"Line {line_num}: prev_hash mismatch "
                        f"(expected {prev_hash[:16]}..., got {record['prev_hash'][:16]}...)"
                    )

                record_bytes = json.dumps(record, sort_keys=True, default=str).encode()
                computed = hashlib.new(self.hash_algo, record_bytes).hexdigest()
                if computed != stored_hash:
                    errors.append(
                        f"Line {line_num}: hash mismatch "
                        f"(computed {computed[:16]}..., stored {stored_hash[:16]}...)"
                    )

                prev_hash = stored_hash

        return len(errors) == 0, errors

    def get_events(self, event_type: str | None = None) -> list[dict[str, Any]]:
        if not self.log_path.exists():
            return []

        events: list[dict[str, Any]] = []
        with self.log_path.open(encoding="utf-8") as handle:
            for line in handle:
                record = json.loads(line.strip())
                if event_type is None or record["event"] == event_type:
                    events.append(record)

        return events

    def summary(self) -> dict[str, Any]:
        events = self.get_events()
        event_counts: dict[str, int] = {}
        for item in events:
            event_counts[item["event"]] = event_counts.get(item["event"], 0) + 1

        chain_ok, _ = self.verify_chain()
        return {
            "project_id": self.project_id,
            "log_path": str(self.log_path),
            "total_events": len(events),
            "event_counts": event_counts,
            "first_event": events[0]["timestamp"] if events else None,
            "last_event": events[-1]["timestamp"] if events else None,
            "chain_valid": chain_ok,
        }
