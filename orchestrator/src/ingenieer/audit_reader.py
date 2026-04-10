"""Read and filter append-only audit JSONL files."""

from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path
from typing import Any


class AuditReader:
    def __init__(self, log_dir: str | Path) -> None:
        self.log_dir = Path(log_dir)

    def query(
        self,
        project_id: str | None = None,
        command: str | None = None,
        after: str | None = None,
        before: str | None = None,
    ) -> list[dict[str, Any]]:
        after_dt = _parse_timestamp(after) if after is not None else None
        before_dt = _parse_timestamp(before) if before is not None else None

        matches: list[tuple[datetime, dict[str, Any]]] = []
        for entry in self._iter_entries():
            if project_id is not None and entry.get("project_id") != project_id:
                continue

            entry_command = entry.get("data", {}).get("command")
            if command is not None and entry_command != command:
                continue

            entry_ts = _parse_timestamp(entry["timestamp"])
            if after_dt is not None and entry_ts <= after_dt:
                continue
            if before_dt is not None and entry_ts >= before_dt:
                continue

            matches.append((entry_ts, entry))

        matches.sort(key=lambda item: item[0])
        return [entry for _, entry in matches]

    def commands_summary(self, project_id: str | None = None) -> dict[str, int]:
        summary: dict[str, int] = {}
        for entry in self.query(project_id=project_id):
            command = entry.get("data", {}).get("command")
            if isinstance(command, str) and command:
                summary[command] = summary.get(command, 0) + 1
        return summary

    def _iter_entries(self) -> list[dict[str, Any]]:
        if not self.log_dir.exists():
            return []

        entries: list[dict[str, Any]] = []
        for path in sorted(self.log_dir.glob("*.jsonl")):
            with path.open(encoding="utf-8") as handle:
                for line in handle:
                    stripped = line.strip()
                    if stripped:
                        entries.append(json.loads(stripped))
        return entries


def _parse_timestamp(value: str) -> datetime:
    return datetime.fromisoformat(value)
