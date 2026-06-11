#!/usr/bin/env python3
"""Cross-language chain round-trip (spec §6), both directions, using the REAL
orchestrator AuditLogger — not a re-implementation.

  direction 1: C++ Store writes chain -> export_jsonl -> AuditLogger.verify_chain()
  direction 2: AuditLogger writes chain (incl. non-ASCII) -> C++ verify_jsonl_chain

usage: test_roundtrip_python.py <chain_roundtrip_tool> <repo_root>
"""

import pathlib
import subprocess
import sys
import tempfile

TOOL = sys.argv[1]
REPO = pathlib.Path(sys.argv[2])
sys.path.insert(0, str(REPO / "orchestrator" / "src"))

from ingenieer.audit import AuditLogger  # noqa: E402


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


with tempfile.TemporaryDirectory() as tmp:
    tmpdir = pathlib.Path(tmp)

    # --- direction 1: C++ -> Python --------------------------------------------------
    db = tmpdir / "rt.sqlite"
    jsonl = tmpdir / "rt_cpp.jsonl"
    r = subprocess.run([TOOL, "write", str(db), str(jsonl)], capture_output=True, text=True)
    if r.returncode != 0:
        fail(f"tool write: {r.stderr}")

    logger = AuditLogger(log_dir=str(tmpdir), project_id="roundtrip")
    logger.log_path = jsonl  # point the real verifier at the C++-written chain
    ok, errors = logger.verify_chain()
    if not ok:
        fail(f"Python verify of C++ chain: {errors}")
    events = logger.get_events()
    if len(events) != 3 or events[0]["data"]["note"] != "Müller—测试":
        fail(f"Python read-back mismatch: {events}")
    print("direction 1 OK: Python AuditLogger verified the C++-written chain")

    # --- direction 2: Python -> C++ --------------------------------------------------
    logger2 = AuditLogger(log_dir=str(tmpdir), project_id="pychain")
    logger2.log(
        "NOTE", {"note": "Müller—测试", "n": 1, "nested": {"b": True, "a": [1, 2.5, None]}}
    )
    logger2.log("NOTE", {"i": 2})
    logger2.log("PROMOTION", {"entity_id": "E1", "from_class": 0, "to_class": 1})
    ok2, errors2 = logger2.verify_chain()
    if not ok2:
        fail(f"Python self-verify: {errors2}")

    r2 = subprocess.run([TOOL, "verify", str(logger2.log_path)], capture_output=True, text=True)
    if r2.returncode != 0:
        fail(f"C++ verify of Python chain: {r2.stderr}")
    print("direction 2 OK: C++ verified the Python AuditLogger-written chain")

print("PASS")
