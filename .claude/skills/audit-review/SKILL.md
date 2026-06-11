---
name: audit-review
description: "Summarize InGENeer audit logs (audit_logs/*.jsonl) — command counts, event/phase breakdown, validation rejections, high-risk confirmations, and per-project activity. Usage: /audit-review [project_id]"
disable-model-invocation: true
args: project_id
---

# Audit Review

Summarize the hash-chained audit logs in `audit_logs/*.jsonl` using the existing `AuditReader`
(`orchestrator/src/ingenieer/audit_reader.py`). Read-only.

## Steps

### 1. Run the summary

From `orchestrator/` (so the package is importable), run this inline script. Pass the optional
`project_id` argument to scope the report; omit it for all projects.

```bash
cd orchestrator && python - "$@" <<'PY'
import sys
from collections import Counter
from ingenieer.audit_reader import AuditReader

project = sys.argv[1] if len(sys.argv) > 1 else None
reader = AuditReader("../audit_logs")
entries = reader.query(project_id=project)

print(f"entries: {len(entries)}  project: {project or 'ALL'}")

events = Counter(e.get("event") for e in entries)
print("\nevents:")
for name, n in events.most_common():
    print(f"  {n:5d}  {name}")

print("\ncommands:")
for cmd, n in sorted(reader.commands_summary(project_id=project).items(), key=lambda kv: -kv[1]):
    print(f"  {n:5d}  {cmd}")

# Surface notable events without hardcoding an event taxonomy: anything whose name or data
# suggests rejection/failure/confirmation.
notable = [
    e for e in entries
    if any(k in (e.get("event") or "").lower() for k in ("reject", "fail", "error", "confirm", "deny"))
]
print(f"\nnotable events (reject/fail/error/confirm): {len(notable)}")
for e in notable[:20]:
    print(f"  seq={e.get('seq')} {e.get('event')} data={e.get('data')}")
PY
```

### 2. Report

Summarize for the user: total entries, the event breakdown, command frequency, and any notable
rejection/failure/confirmation events. If the user asked about a specific run, filter by the
matching `project_id` (e.g. the `e2e` logs are `audit_logs/e2e_*.jsonl` → `project_id="e2e"`).

Do not modify or delete any audit log — these are an integrity record (hash-chained via `prev_hash`/`hash`).
