"""Tests for LLM intent generator with mock Anthropic client."""

import json
from unittest.mock import MagicMock, patch

from ingenieer.intent_generator import IntentGenerator
from ingenieer.models import GeneratorConfig

VALID_CONTRACT_JSON = json.dumps({
    "project": {"name": "test-site"},
    "intents": [
        {
            "intentId": "gen-1",
            "command": "CreateAlignment",
            "parameters": {
                "name": "Main St CL",
                "points": [[0.0, 0.0, 0.0], [100.0, 50.0, 5.0]],
                "start_station": 0.0,
                "layer": "ALIGNMENT",
            },
        },
    ],
})

INVALID_JSON = "not json at all {"

# Missing intentId (required, min_length=1) — will fail ProjectContract.model_validate
VALID_JSON_BAD_SCHEMA = json.dumps({
    "project": {"name": "bad"},
    "intents": [
        {
            "command": "CreateAlignment",
            "parameters": {},
        },
    ],
})


def _mock_response(text: str) -> MagicMock:
    block = MagicMock()
    block.type = "text"
    block.text = text
    msg = MagicMock()
    msg.content = [block]
    msg.stop_reason = "end_turn"
    msg.usage = MagicMock(input_tokens=100, output_tokens=200)
    return msg


def _make_generator(api_key: str = "test-key") -> IntentGenerator:
    return IntentGenerator(GeneratorConfig(api_key=api_key))


@patch("ingenieer.intent_generator.anthropic")
def test_generate_valid_contract(mock_anthropic):
    client = MagicMock()
    mock_anthropic.Anthropic.return_value = client
    client.messages.create.return_value = _mock_response(VALID_CONTRACT_JSON)

    gen = _make_generator()
    result = gen.generate("create an alignment for Main Street")

    assert result.success
    assert result.contract is not None
    assert result.contract.intents[0].command == "CreateAlignment"
    assert result.attempts == 1
    assert result.errors == []


@patch("ingenieer.intent_generator.anthropic")
def test_generate_retries_on_invalid_json(mock_anthropic):
    client = MagicMock()
    mock_anthropic.Anthropic.return_value = client
    client.messages.create.side_effect = [
        _mock_response(INVALID_JSON),
        _mock_response(VALID_CONTRACT_JSON),
    ]

    gen = _make_generator()
    result = gen.generate("create an alignment")

    assert result.success
    assert result.attempts == 2


@patch("ingenieer.intent_generator.anthropic")
def test_generate_retries_on_schema_failure(mock_anthropic):
    client = MagicMock()
    mock_anthropic.Anthropic.return_value = client
    client.messages.create.side_effect = [
        _mock_response(VALID_JSON_BAD_SCHEMA),
        _mock_response(VALID_CONTRACT_JSON),
    ]

    gen = _make_generator()
    result = gen.generate("create an alignment")

    assert result.success
    assert result.attempts == 2


@patch("ingenieer.intent_generator.anthropic")
def test_generate_exhausts_retries(mock_anthropic):
    client = MagicMock()
    mock_anthropic.Anthropic.return_value = client
    client.messages.create.side_effect = [
        _mock_response(INVALID_JSON),
        _mock_response(INVALID_JSON),
    ]

    gen = _make_generator()
    result = gen.generate("create an alignment")

    assert not result.success
    assert result.contract is None
    assert result.attempts == 2
    assert len(result.errors) > 0


def test_generate_missing_api_key():
    gen = _make_generator(api_key="")
    result = gen.generate("anything")

    assert not result.success
    assert result.attempts == 0
    assert any("api_key" in e.lower() or "api key" in e.lower() for e in result.errors)


@patch("ingenieer.intent_generator.anthropic")
def test_generate_includes_context_in_prompt(mock_anthropic):
    client = MagicMock()
    mock_anthropic.Anthropic.return_value = client
    client.messages.create.return_value = _mock_response(VALID_CONTRACT_JSON)

    gen = _make_generator()
    gen.generate(
        "create a profile",
        context="existing alignment Main St CL on layer ALIGNMENT",
    )

    call_args = client.messages.create.call_args
    user_msg = call_args.kwargs["messages"][0]["content"]
    assert "Main St CL" in user_msg
    assert "ALIGNMENT" in user_msg


@patch("ingenieer.intent_generator.anthropic")
def test_generate_includes_prior_result(mock_anthropic):
    from ingenieer.batch import BatchResult

    client = MagicMock()
    mock_anthropic.Anthropic.return_value = client
    client.messages.create.return_value = _mock_response(VALID_CONTRACT_JSON)

    prior = BatchResult(
        project_id="prev",
        success=True,
        completed_steps=1,
        total_steps=1,
        last_good_fingerprint="fp-abc",
    )
    gen = _make_generator()
    gen.generate("continue the design", prior_result=prior)

    call_args = client.messages.create.call_args
    user_msg = call_args.kwargs["messages"][0]["content"]
    assert "fp-abc" in user_msg


@patch("ingenieer.intent_generator.anthropic")
def test_generate_api_error_no_retry(mock_anthropic):
    client = MagicMock()
    mock_anthropic.Anthropic.return_value = client
    client.messages.create.side_effect = RuntimeError("connection refused")

    gen = _make_generator()
    result = gen.generate("anything")

    assert not result.success
    assert result.attempts == 1
    assert any("connection" in e.lower() or "api" in e.lower() for e in result.errors)
