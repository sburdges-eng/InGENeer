import json

from ingenieer.audit_reader import AuditReader


def test_query_filters_and_combines_filters(tmp_path):
    _write_jsonl(
        tmp_path / "a.jsonl",
        [
            {
                "seq": 1,
                "timestamp": "2026-01-01T00:00:00+00:00",
                "project_id": "p1",
                "event": "dispatch",
                "data": {"command": "NoOp"},
                "prev_hash": "0" * 64,
                "hash": "a" * 64,
            },
            {
                "seq": 2,
                "timestamp": "2026-01-01T00:05:00+00:00",
                "project_id": "p1",
                "event": "dispatch",
                "data": {"command": "CreateAlignment"},
                "prev_hash": "a" * 64,
                "hash": "b" * 64,
            },
        ],
    )
    _write_jsonl(
        tmp_path / "b.jsonl",
        [
            {
                "seq": 1,
                "timestamp": "2026-01-01T00:10:00+00:00",
                "project_id": "p2",
                "event": "dispatch",
                "data": {"command": "NoOp"},
                "prev_hash": "0" * 64,
                "hash": "c" * 64,
            },
            {
                "seq": 2,
                "timestamp": "2026-01-01T00:15:00+00:00",
                "project_id": "p1",
                "event": "pipeline_complete",
                "data": {"command": "NoOp"},
                "prev_hash": "c" * 64,
                "hash": "d" * 64,
            },
        ],
    )

    reader = AuditReader(tmp_path)

    project_entries = reader.query(project_id="p1")
    assert [entry["timestamp"] for entry in project_entries] == [
        "2026-01-01T00:00:00+00:00",
        "2026-01-01T00:05:00+00:00",
        "2026-01-01T00:15:00+00:00",
    ]

    noop_entries = reader.query(command="NoOp")
    assert [entry["project_id"] for entry in noop_entries] == ["p1", "p2", "p1"]

    combined = reader.query(
        project_id="p1",
        command="NoOp",
        after="2026-01-01T00:01:00+00:00",
        before="2026-01-01T00:20:00+00:00",
    )
    assert [entry["timestamp"] for entry in combined] == ["2026-01-01T00:15:00+00:00"]


def test_query_returns_empty_results(tmp_path):
    _write_jsonl(
        tmp_path / "only.jsonl",
        [
            {
                "seq": 1,
                "timestamp": "2026-01-01T00:00:00+00:00",
                "project_id": "p1",
                "event": "dispatch",
                "data": {"command": "NoOp"},
                "prev_hash": "0" * 64,
                "hash": "a" * 64,
            }
        ],
    )

    reader = AuditReader(tmp_path)

    assert reader.query(project_id="missing") == []
    assert reader.query(command="CreateAlignment") == []


def test_commands_summary(tmp_path):
    _write_jsonl(
        tmp_path / "summary.jsonl",
        [
            {
                "seq": 1,
                "timestamp": "2026-01-01T00:00:00+00:00",
                "project_id": "p1",
                "event": "dispatch",
                "data": {"command": "NoOp"},
                "prev_hash": "0" * 64,
                "hash": "a" * 64,
            },
            {
                "seq": 2,
                "timestamp": "2026-01-01T00:05:00+00:00",
                "project_id": "p1",
                "event": "dispatch",
                "data": {"command": "NoOp"},
                "prev_hash": "a" * 64,
                "hash": "b" * 64,
            },
            {
                "seq": 3,
                "timestamp": "2026-01-01T00:10:00+00:00",
                "project_id": "p2",
                "event": "dispatch",
                "data": {"command": "CreateAlignment"},
                "prev_hash": "b" * 64,
                "hash": "c" * 64,
            },
        ],
    )

    reader = AuditReader(tmp_path)

    assert reader.commands_summary() == {"NoOp": 2, "CreateAlignment": 1}
    assert reader.commands_summary(project_id="p1") == {"NoOp": 2}


def _write_jsonl(path, entries):
    with path.open("w", encoding="utf-8") as handle:
        for entry in entries:
            handle.write(json.dumps(entry) + "\n")
