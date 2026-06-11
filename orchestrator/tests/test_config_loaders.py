from ingenieer.models import BridgeConfig, OrchestratorConfig


def test_from_env_with_all_vars_set(monkeypatch):
    monkeypatch.setenv("INGENEER_BRIDGE_MODE", "http")
    monkeypatch.setenv("INGENEER_BRIDGE_URL", "http://127.0.0.1:9000")
    monkeypatch.setenv("INGENEER_BRIDGE_TIMEOUT", "12.5")
    monkeypatch.setenv("INGENEER_BRIDGE_FINGERPRINT", "fp-env")
    monkeypatch.setenv("INGENEER_SCHEMA_ENFORCE", "false")
    monkeypatch.setenv("INGENEER_MAX_VERIFY_ATTEMPTS", "6")
    monkeypatch.setenv("INGENEER_VERIFY_BACKOFF", "1.25")

    config = OrchestratorConfig.from_env()

    assert config.bridge.mode == "http"
    assert config.bridge.http_base_url == "http://127.0.0.1:9000"
    assert config.bridge.timeout_sec == 12.5
    assert config.bridge.mock_model_fingerprint == "fp-env"
    assert config.intent_validation.enforce_json_schema is False
    assert config.intent_validation.enforce_command_allowlist is False
    assert config.max_verification_attempts == 6
    assert config.verification_backoff_sec == 1.25


def test_from_env_with_defaults(monkeypatch):
    for name in (
        "INGENEER_BRIDGE_MODE",
        "INGENEER_BRIDGE_URL",
        "INGENEER_BRIDGE_TIMEOUT",
        "INGENEER_BRIDGE_FINGERPRINT",
        "INGENEER_SCHEMA_ENFORCE",
        "INGENEER_MAX_VERIFY_ATTEMPTS",
        "INGENEER_VERIFY_BACKOFF",
    ):
        monkeypatch.delenv(name, raising=False)

    config = OrchestratorConfig.from_env()

    assert config.bridge.mode == "mock"
    assert config.bridge.http_base_url == "http://127.0.0.1:8765"
    assert config.bridge.timeout_sec == 10.0
    assert config.bridge.mock_model_fingerprint == "stub-fingerprint"
    assert config.intent_validation.enforce_json_schema is True
    assert config.intent_validation.enforce_command_allowlist is True
    assert config.max_verification_attempts == 3
    assert config.verification_backoff_sec == 0.5


def test_from_toml_with_valid_file(tmp_path):
    config_path = tmp_path / "orchestrator.toml"
    config_path.write_text(
        """
[bridge]
mode = "http"
http_base_url = "http://127.0.0.1:8765"
timeout_sec = 10.0
mock_model_fingerprint = "fp-toml"

[intent_validation]
enforce_json_schema = true
enforce_command_allowlist = false
max_verification_attempts = 4
verification_backoff_sec = 0.75
""".strip(),
        encoding="utf-8",
    )

    config = OrchestratorConfig.from_toml(config_path)

    assert config.bridge.mode == "http"
    assert config.bridge.http_base_url == "http://127.0.0.1:8765"
    assert config.bridge.timeout_sec == 10.0
    assert config.bridge.mock_model_fingerprint == "fp-toml"
    assert config.intent_validation.enforce_json_schema is True
    assert config.intent_validation.enforce_command_allowlist is False
    assert config.max_verification_attempts == 4
    assert config.verification_backoff_sec == 0.75


def test_from_toml_missing_file_raises(tmp_path):
    missing_path = tmp_path / "missing.toml"

    try:
        OrchestratorConfig.from_toml(missing_path)
    except FileNotFoundError:
        pass
    else:
        raise AssertionError("expected FileNotFoundError")


def test_bridge_config_deadline_sec_default():
    config = BridgeConfig()

    assert config.deadline_sec == 60.0


def test_generator_config_defaults():
    from ingenieer.models import GeneratorConfig

    cfg = GeneratorConfig()
    assert cfg.api_key == ""
    assert cfg.model == "claude-sonnet-4-20250514"
    assert cfg.max_tokens == 4096
    assert cfg.temperature == 0.0


def test_orchestrator_config_has_generator():
    from ingenieer.models import OrchestratorConfig

    cfg = OrchestratorConfig()
    assert cfg.generator.api_key == ""
    assert cfg.generator.model == "claude-sonnet-4-20250514"
