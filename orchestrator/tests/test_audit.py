from ingenieer.audit import AuditLogger


def test_audit_chain(tmp_path):
    log = AuditLogger(log_dir=str(tmp_path / "logs"), project_id="p1")
    log.log("a", {"k": 1})
    log.log("b", {"k": 2})
    ok, errors = log.verify_chain()
    assert ok, errors
    summary = log.summary()
    assert summary["total_events"] == 2
    assert summary["chain_valid"] is True
