# LLM Intent Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an `IntentGenerator` that takes a natural language civil engineering request and produces a validated `ProjectContract`, with CLI integration via `--generate` and `--generate-only` flags.

**Architecture:** A new `ingenieer.intent_generator` module wraps the Anthropic SDK. It builds a system prompt from the 19-command catalog, calls the API, validates the JSON response as a `ProjectContract`, and retries once on validation failure with error feedback. The CLI wires generation into the existing batch pipeline.

**Tech Stack:** Python 3.11+, anthropic SDK, pydantic, pytest

---

## File Structure

| File | Responsibility |
|------|----------------|
| `orchestrator/src/ingenieer/intent_generator.py` | **Create.** `GenerateResult` dataclass, `IntentGenerator` class, system prompt builder, Anthropic API call, validation + retry logic. |
| `orchestrator/src/ingenieer/models.py` | **Modify.** Add `GeneratorConfig` model to `OrchestratorConfig`. |
| `orchestrator/src/ingenieer/cli.py` | **Modify.** Add `--generate` and `--generate-only` flags. |
| `orchestrator/tests/test_intent_generator.py` | **Create.** Unit tests with mock Anthropic client + one integration test. |
| `orchestrator/tests/test_cli.py` | **Modify.** Add CLI tests for generate flags. |
| `orchestrator/pyproject.toml` | **Modify.** Add `anthropic>=0.40` dependency. |

---

### Task 1: Add anthropic Dependency and GeneratorConfig

**Files:**
- Modify: `orchestrator/pyproject.toml`
- Modify: `orchestrator/src/ingenieer/models.py`
- Modify: `orchestrator/tests/test_config_loaders.py`

- [ ] **Step 1: Write failing test for GeneratorConfig**

Add to `orchestrator/tests/test_config_loaders.py`:

```python
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd orchestrator && python3 -m pytest tests/test_config_loaders.py::test_generator_config_defaults -q`
Expected: FAIL — GeneratorConfig not found

- [ ] **Step 3: Add GeneratorConfig to models.py**

In `orchestrator/src/ingenieer/models.py`, add after `IntentValidationConfig`:

```python
class GeneratorConfig(BaseModel):
    """LLM intent generation settings."""

    api_key: str = ""
    model: str = "claude-sonnet-4-20250514"
    max_tokens: int = Field(default=4096, ge=256, le=16384)
    temperature: float = Field(default=0.0, ge=0.0, le=1.0)
```

Add to `OrchestratorConfig` after `intent_validation`:

```python
    generator: GeneratorConfig = Field(default_factory=GeneratorConfig)
```

Add `anthropic>=0.40` to `pyproject.toml` dependencies:

```toml
dependencies = [
    "pydantic>=2.5",
    "jsonschema>=4.20",
    "anthropic>=0.40",
]
```

- [ ] **Step 4: Install and run tests**

Run:
```bash
cd orchestrator && pip install -e ".[dev]" && python3 -m pytest tests/test_config_loaders.py -q
```
Expected: all pass

- [ ] **Step 5: Commit**

```bash
git add orchestrator/pyproject.toml orchestrator/src/ingenieer/models.py orchestrator/tests/test_config_loaders.py
git commit -m "feat: add anthropic dep and GeneratorConfig to OrchestratorConfig"
```

---

### Task 2: IntentGenerator Core — System Prompt and Generate Method

**Files:**
- Create: `orchestrator/src/ingenieer/intent_generator.py`
- Create: `orchestrator/tests/test_intent_generator.py`

- [ ] **Step 1: Write failing tests with mock Anthropic client**

```python
# orchestrator/tests/test_intent_generator.py
"""Tests for LLM intent generator with mock Anthropic client."""

import json
from dataclasses import dataclass
from unittest.mock import MagicMock, patch

import pytest

from ingenieer.intent_generator import GenerateResult, IntentGenerator
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

VALID_JSON_BAD_SCHEMA = json.dumps({
    "project": {"name": "bad"},
    "intents": [
        {
            "intentId": "gen-bad",
            "command": "CreateAlignment",
            "parameters": {},  # missing required fields
        },
    ],
})


def _mock_response(text: str) -> MagicMock:
    """Build a mock Anthropic Message response."""
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
    import anthropic as real_anthropic

    client = MagicMock()
    mock_anthropic.Anthropic.return_value = client
    mock_anthropic.APIError = real_anthropic.APIError
    client.messages.create.side_effect = real_anthropic.APIError(
        message="rate limited",
        request=MagicMock(),
        body=None,
    )

    gen = _make_generator()
    result = gen.generate("anything")

    assert not result.success
    assert result.attempts == 1
    assert any("rate" in e.lower() or "api" in e.lower() for e in result.errors)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd orchestrator && python3 -m pytest tests/test_intent_generator.py -q`
Expected: FAIL — module not found

- [ ] **Step 3: Implement IntentGenerator**

```python
# orchestrator/src/ingenieer/intent_generator.py
"""LLM-powered intent generation: natural language → validated ProjectContract."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import anthropic

from ingenieer.batch import BatchResult, ProjectContract
from ingenieer.models import GeneratorConfig

_CATALOG_PATH = Path(__file__).resolve().parents[3] / "docs" / "INTENT_COMMAND_API_REFERENCE.md"

_SYSTEM_PROMPT_TEMPLATE = """\
You are an InGENeer intent generator. You produce structured JSON project contracts \
for a civil engineering CAD automation system.

Your output MUST be a valid JSON object matching this exact shape:
{{
  "project": {{"name": "<project-name>", "version": "1.0.0"}},
  "intents": [
    {{
      "intentId": "<unique-id>",
      "command": "<command-name>",
      "parameters": {{...}}
    }}
  ]
}}

Rules:
1. Every intentId must be unique within the contract.
2. Use ONLY commands from the catalog below. Do not invent commands.
3. Order intents logically: surfaces before alignments, alignments before profiles, \
profiles before cross-sections, cross-sections before corridors.
4. Fill in all required parameters with realistic civil engineering values.
5. Use descriptive layer names following civil engineering conventions (e.g., C-ROAD-CNTR, C-TOPO-SURF).
6. Output ONLY the JSON object. No markdown, no explanation, no code fences.

## Command Catalog

{catalog}
"""


def _load_catalog() -> str:
    if _CATALOG_PATH.is_file():
        return _CATALOG_PATH.read_text(encoding="utf-8")
    return "(catalog not found — use only: NoOp, PingHost, GetModelFingerprint)"


def _build_system_prompt() -> str:
    return _SYSTEM_PROMPT_TEMPLATE.format(catalog=_load_catalog())


def _build_user_message(
    request: str,
    context: str | None,
    prior_result: BatchResult | None,
) -> str:
    parts = [f"Engineering request: {request}"]
    if context:
        parts.append(f"\nCurrent document context:\n{context}")
    if prior_result:
        summary = {
            "project_id": prior_result.project_id,
            "success": prior_result.success,
            "completed_steps": prior_result.completed_steps,
            "last_good_fingerprint": prior_result.last_good_fingerprint,
            "completed_commands": [
                sr.command
                for sr in prior_result.step_results
                if sr.pipeline_result.success
            ],
        }
        parts.append(f"\nPrior batch result:\n{json.dumps(summary, indent=2)}")
    return "\n".join(parts)


def _extract_text(response: Any) -> str:
    for block in response.content:
        if getattr(block, "type", None) == "text":
            return block.text
    return ""


@dataclass
class GenerateResult:
    success: bool
    contract: ProjectContract | None
    raw_response: str
    errors: list[str] = field(default_factory=list)
    attempts: int = 0


class IntentGenerator:
    def __init__(self, config: GeneratorConfig) -> None:
        self._config = config

    def generate(
        self,
        request: str,
        context: str | None = None,
        prior_result: BatchResult | None = None,
    ) -> GenerateResult:
        if not self._config.api_key:
            return GenerateResult(
                success=False,
                contract=None,
                raw_response="",
                errors=["generator api_key is not set (set ANTHROPIC_API_KEY)"],
                attempts=0,
            )

        client = anthropic.Anthropic(api_key=self._config.api_key)
        system_prompt = _build_system_prompt()
        user_message = _build_user_message(request, context, prior_result)

        errors: list[str] = []
        raw = ""
        max_attempts = 2

        for attempt in range(1, max_attempts + 1):
            messages = [{"role": "user", "content": user_message}]
            if attempt > 1 and errors:
                messages.append({
                    "role": "assistant",
                    "content": raw,
                })
                messages.append({
                    "role": "user",
                    "content": (
                        "The previous response had validation errors:\n"
                        + "\n".join(f"- {e}" for e in errors)
                        + "\n\nPlease fix the JSON and try again. Output ONLY the corrected JSON."
                    ),
                })

            try:
                response = client.messages.create(
                    model=self._config.model,
                    max_tokens=self._config.max_tokens,
                    temperature=self._config.temperature,
                    system=system_prompt,
                    messages=messages,
                )
            except Exception as exc:
                return GenerateResult(
                    success=False,
                    contract=None,
                    raw_response=raw,
                    errors=[f"API error: {exc}"],
                    attempts=attempt,
                )

            raw = _extract_text(response)
            errors = []

            # Parse JSON
            try:
                parsed = json.loads(raw)
            except json.JSONDecodeError as exc:
                errors.append(f"Invalid JSON: {exc.msg} at position {exc.pos}")
                continue

            # Validate as ProjectContract
            try:
                contract = ProjectContract.model_validate(parsed)
            except Exception as exc:
                errors.append(f"Schema validation: {exc}")
                continue

            return GenerateResult(
                success=True,
                contract=contract,
                raw_response=raw,
                errors=[],
                attempts=attempt,
            )

        return GenerateResult(
            success=False,
            contract=None,
            raw_response=raw,
            errors=errors,
            attempts=max_attempts,
        )
```

- [ ] **Step 4: Run tests**

Run: `cd orchestrator && python3 -m pytest tests/test_intent_generator.py -q`
Expected: 8 passed

- [ ] **Step 5: Lint and commit**

```bash
cd orchestrator && ruff check src/ingenieer/intent_generator.py tests/test_intent_generator.py
git add orchestrator/src/ingenieer/intent_generator.py orchestrator/tests/test_intent_generator.py
git commit -m "feat: add IntentGenerator with system prompt, validation, and retry"
```

---

### Task 3: CLI --generate and --generate-only Flags

**Files:**
- Modify: `orchestrator/src/ingenieer/cli.py`
- Modify: `orchestrator/tests/test_cli.py`

- [ ] **Step 1: Write failing CLI tests**

Add to `orchestrator/tests/test_cli.py`:

```python
from unittest.mock import patch


MOCK_CONTRACT_JSON = json.dumps({
    "project": {"name": "generated"},
    "intents": [
        {"intentId": "g1", "command": "NoOp", "parameters": {}},
    ],
})


def _mock_generate_success(*args, **kwargs):
    from ingenieer.batch import ProjectContract
    from ingenieer.intent_generator import GenerateResult

    return GenerateResult(
        success=True,
        contract=ProjectContract.model_validate(json.loads(MOCK_CONTRACT_JSON)),
        raw_response=MOCK_CONTRACT_JSON,
        attempts=1,
    )


def _mock_generate_failure(*args, **kwargs):
    from ingenieer.intent_generator import GenerateResult

    return GenerateResult(
        success=False,
        contract=None,
        raw_response="bad json",
        errors=["validation failed"],
        attempts=2,
    )


class TestGenerateMode:
    @patch("ingenieer.cli.IntentGenerator")
    def test_generate_only_prints_contract(self, mock_cls):
        instance = mock_cls.return_value
        instance.generate.return_value = _mock_generate_success()
        r = _run_cli("--generate-only", "create a noop", "--dry-run")
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["success"] is True
        assert "contract" in out

    @patch("ingenieer.cli.IntentGenerator")
    def test_generate_only_failure_exits_nonzero(self, mock_cls):
        instance = mock_cls.return_value
        instance.generate.return_value = _mock_generate_failure()
        r = _run_cli("--generate-only", "bad request")
        assert r.returncode == 1
        out = json.loads(r.stdout)
        assert out["success"] is False

    @patch("ingenieer.cli.IntentGenerator")
    def test_generate_runs_batch_pipeline(self, mock_cls):
        instance = mock_cls.return_value
        instance.generate.return_value = _mock_generate_success()
        r = _run_cli("--generate", "create a noop", "--dry-run")
        assert r.returncode == 0
        out = json.loads(r.stdout)
        assert out["success"] is True
        assert "completed_steps" in out

    def test_generate_without_api_key_exits_nonzero(self):
        r = _run_cli("--generate-only", "anything")
        assert r.returncode == 1
        out = json.loads(r.stdout)
        assert out["success"] is False
        assert any("api_key" in e.lower() or "api key" in e.lower() for e in out["errors"])

    def test_generate_and_batch_conflict(self):
        r = _run_cli("--generate", "x", "--batch", stdin='{}')
        assert r.returncode == 2
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd orchestrator && python3 -m pytest tests/test_cli.py::TestGenerateMode -q`
Expected: FAIL — unrecognized argument

- [ ] **Step 3: Add --generate and --generate-only to CLI**

In `orchestrator/src/ingenieer/cli.py`, add after the `--batch` argument:

```python
    parser.add_argument(
        "--generate",
        metavar="REQUEST",
        default=None,
        help="Generate a project contract from a natural language request, then execute it",
    )
    parser.add_argument(
        "--generate-only",
        metavar="REQUEST",
        default=None,
        help="Generate a project contract from a natural language request and print it (no execution)",
    )
    parser.add_argument(
        "--context",
        default=None,
        help="Document context for intent generation (e.g., existing surfaces, alignments)",
    )
```

Add the generate path in `main()` after config loading, before batch mode. Add the import at the top:

```python
from ingenieer.intent_generator import IntentGenerator
```

The generate path:

```python
    # ── Conflict checks ────────────────────────────────────────
    gen_request = args.generate or args.generate_only
    if gen_request and args.batch is not None:
        print("error: use --generate or --batch, not both", file=sys.stderr)
        sys.exit(2)

    # ── Generate mode ──────────────────────────────────────────
    if gen_request:
        api_key = config.generator.api_key or os.environ.get("ANTHROPIC_API_KEY", "")
        gen_config = config.generator.model_copy(update={"api_key": api_key})
        generator = IntentGenerator(gen_config)
        gen_result = generator.generate(gen_request, context=args.context)

        if not gen_result.success or gen_result.contract is None:
            out = {
                "success": False,
                "errors": gen_result.errors,
                "raw_response": gen_result.raw_response,
                "attempts": gen_result.attempts,
            }
            print(json.dumps(out, indent=2))
            sys.exit(1)

        # --generate-only: print contract and exit
        if args.generate_only:
            out = {
                "success": True,
                "contract": gen_result.contract.model_dump(mode="json"),
                "attempts": gen_result.attempts,
            }
            print(json.dumps(out, indent=2))
            sys.exit(0)

        # --generate: run the contract through BatchPipeline
        contract = gen_result.contract
        if args.dry_run:
            contract = contract.model_copy(update={"execution_mode_override": "dry_run"})
        elif args.preview:
            contract = contract.model_copy(update={"execution_mode_override": "preview"})
        if args.i_confirm is not None:
            token = args.i_confirm.strip()
            if not token:
                print("error: --i-confirm must be non-empty", file=sys.stderr)
                sys.exit(2)
            contract = contract.model_copy(update={"human_confirmation_token": token})

        audit = AuditLogger(
            log_dir=str(args.audit_dir),
            project_id=contract.project.name,
            hash_algo=config.audit.hash_algorithm,
        )
        pipeline = BatchPipeline(config, audit, args.output_dir)
        batch_result = pipeline.run(contract)

        out = {
            "success": batch_result.success,
            "project_id": batch_result.project_id,
            "completed_steps": batch_result.completed_steps,
            "total_steps": batch_result.total_steps,
            "last_good_fingerprint": batch_result.last_good_fingerprint,
            "errors": batch_result.errors,
            "steps": [
                {
                    "intent_id": s.intent_id,
                    "command": s.command,
                    "success": s.pipeline_result.success,
                    "fingerprint_before": s.fingerprint_before,
                    "fingerprint_after": s.fingerprint_after,
                }
                for s in batch_result.step_results
            ],
        }
        print(json.dumps(out, indent=2))
        sys.exit(0 if batch_result.success else 1)
```

Add `import os` to the imports at the top of cli.py.

- [ ] **Step 4: Run CLI tests**

Run: `cd orchestrator && python3 -m pytest tests/test_cli.py -q`
Expected: all pass

- [ ] **Step 5: Run full suite**

Run: `cd orchestrator && ruff check src tests && python3 -m pytest -q`
Expected: all pass

- [ ] **Step 6: Commit**

```bash
git add orchestrator/src/ingenieer/cli.py orchestrator/tests/test_cli.py
git commit -m "feat: add --generate and --generate-only CLI flags for LLM intent generation"
```

---

### Task 4: Integration Test (Real API)

**Files:**
- Modify: `orchestrator/tests/test_intent_generator.py`

- [ ] **Step 1: Add integration test**

Add to `orchestrator/tests/test_intent_generator.py`:

```python
@pytest.mark.integration
def test_generate_real_api():
    """Call the real Anthropic API. Skipped without ANTHROPIC_API_KEY."""
    import os

    api_key = os.environ.get("ANTHROPIC_API_KEY", "")
    if not api_key:
        pytest.skip("ANTHROPIC_API_KEY not set")

    gen = IntentGenerator(GeneratorConfig(api_key=api_key))
    result = gen.generate(
        "Create a centerline alignment for Main Street from (0,0,0) to (500,200,10), "
        "then create a vertical profile with two PVIs.",
        context="Empty document, no existing surfaces or alignments.",
    )

    assert result.success, f"Generation failed: {result.errors}"
    assert result.contract is not None
    assert len(result.contract.intents) >= 2
    commands = [i.command for i in result.contract.intents]
    assert "CreateAlignment" in commands
    assert "CreateProfile" in commands
    # Alignment should come before profile
    assert commands.index("CreateAlignment") < commands.index("CreateProfile")
```

- [ ] **Step 2: Run test (if API key available)**

Run: `cd orchestrator && python3 -m pytest tests/test_intent_generator.py::test_generate_real_api -q`
Expected: PASS (or skip if no API key)

- [ ] **Step 3: Commit**

```bash
git add orchestrator/tests/test_intent_generator.py
git commit -m "test: add real-API integration test for intent generator"
```

---

### Task 5: Final Verification

**Files:** None (verification only)

- [ ] **Step 1: Full lint**

Run: `cd orchestrator && ruff check src tests`
Expected: All checks passed

- [ ] **Step 2: Full test suite**

Run: `cd orchestrator && python3 -m pytest -q -m "not integration"`
Expected: all pass (~175+ tests)

- [ ] **Step 3: C# build**

Run: `dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release`
Expected: 0 warnings

- [ ] **Step 4: E2E smoke test (if API key available)**

```bash
ANTHROPIC_API_KEY=your-key python3 -m ingenieer.cli --generate-only "create an alignment for Main Street" --dry-run
```

Expected: JSON with `success: true` and a valid contract

- [ ] **Step 5: Push branch**

```bash
git push -u origin feat/llm-intent-generator
```
