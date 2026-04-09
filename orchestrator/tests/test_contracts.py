import pytest

from ingenieer.contracts import (
    BASE_INVARIANTS,
    build_contract_payload,
    canonical_json,
    validate_contract_payload,
)


def test_build_and_validate_roundtrip():
    payload = build_contract_payload(
        artifact_type="test_artifact",
        metadata={"ok": True},
        data={"x": 1},
        paths={"rel": "out/file.txt"},
    )
    assert validate_contract_payload(payload) == []
    assert payload["invariants"][: len(BASE_INVARIANTS)] == list(BASE_INVARIANTS)


def test_rejects_absolute_path_in_paths():
    with pytest.raises(ValueError, match="absolute path"):
        build_contract_payload(artifact_type="t", paths={"bad": "/etc/passwd"})


def test_rejects_path_traversal():
    with pytest.raises(ValueError, match="path traversal"):
        build_contract_payload(artifact_type="t", paths={"bad": "../secret"})


def test_canonical_json_stable():
    a = build_contract_payload(artifact_type="t", metadata={"z": 1}, data={"b": 2})
    b = build_contract_payload(artifact_type="t", metadata={"z": 1}, data={"b": 2})
    assert canonical_json(a) == canonical_json(b)
