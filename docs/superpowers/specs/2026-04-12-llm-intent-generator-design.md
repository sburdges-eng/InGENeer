# LLM Intent Generator Design

**Date:** 2026-04-12
**Status:** Approved

## Goal

An `IntentGenerator` module that takes a natural language civil engineering request and produces a validated `ProjectContract` with ordered intents, ready for `BatchPipeline` execution.

## Architecture

A new module `ingenieer.intent_generator` wraps the Anthropic SDK. It takes a natural language request, optional document context, and optional prior `BatchResult`, then produces a validated `ProjectContract`. The system prompt includes the full 19-command catalog with parameter schemas. One retry on validation failure with error feedback to the LLM.

## Components

### GeneratorConfig (in models.py)

```python
class GeneratorConfig(BaseModel):
    api_key: str = ""  # from env ANTHROPIC_API_KEY
    model: str = "claude-sonnet-4-20250514"
    max_tokens: int = 4096
    temperature: float = 0.0
```

Added to `OrchestratorConfig` as `generator: GeneratorConfig`.

### GenerateResult (in intent_generator.py)

```python
@dataclass
class GenerateResult:
    success: bool
    contract: ProjectContract | None
    raw_response: str
    errors: list[str]
    attempts: int
```

### IntentGenerator class (in intent_generator.py)

- Constructor: `IntentGenerator(config: GeneratorConfig)`
- Method: `generate(request: str, context: str | None = None, prior_result: BatchResult | None = None) -> GenerateResult`
- Builds system prompt at runtime from the command catalog
- Calls Anthropic API with structured output (JSON mode)
- Validates response as `ProjectContract`
- On validation failure: one retry with error feedback
- On API errors (timeout, rate limit): surfaces as `GenerateResult(success=False)`, no retry

### System Prompt

Built at runtime from:
- The 19-command catalog (parameter names, types, constraints, risk tiers) read from `docs/INTENT_COMMAND_API_REFERENCE.md`
- `ProjectContract` schema shape (project metadata + ordered intents)
- Rules: unique intentIds, correct execution ordering (e.g., alignment before profile), risk tier awareness, civil engineering domain knowledge

### CLI Integration

Two new flags on `ingenieer-run`:
- `--generate "natural language request"` — generate a ProjectContract, then run it through BatchPipeline
- `--generate-only "natural language request"` — generate and print the contract JSON without executing

Both respect existing `--dry-run`, `--preview`, `--i-confirm`, `--config` flags. Missing API key produces a clear error message.

## Data Flow

```
User request (string) + optional context + optional prior BatchResult
    ↓
IntentGenerator.generate()
    ↓ builds system prompt + user message
Anthropic API call (JSON response)
    ↓ parse JSON
ProjectContract.model_validate()
    ↓ if fails: retry once with error feedback in prompt
    ↓ if passes: return GenerateResult(success=True)
    ↓
CLI: print contract → optionally pipe to BatchPipeline
```

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Missing API key | Clear error message, exit 2 |
| API timeout / rate limit | `GenerateResult(success=False)` with error, no retry |
| Invalid JSON from LLM | One retry with parse error in prompt |
| Valid JSON, bad parameters | One retry with schema validation errors in prompt |
| Both retries fail | Return `GenerateResult` with all errors + raw response |

## Context Sources

The generator accepts two optional context inputs:
- `context: str | None` — free-text description of current document state (e.g., "the site has an existing ground surface called EG and a centerline alignment called Main St CL")
- `prior_result: BatchResult | None` — serialized result from a previous batch run, providing the LLM with knowledge of what commands succeeded, what fingerprints exist, and what state the document is in

Both are optional. When provided, they're included in the user message to the LLM.

## Configuration

Via environment:
- `ANTHROPIC_API_KEY` — required for generation
- `INGENEER_GENERATOR_MODEL` — override model (default: claude-sonnet-4-20250514)

Via TOML config:
```toml
[generator]
model = "claude-opus-4-20250514"
max_tokens = 4096
temperature = 0.0
```

## Testing

- Unit tests with a mock Anthropic client (no real API calls in CI)
  - Valid generation from mock response
  - Retry on invalid JSON
  - Retry on schema validation failure
  - Both retries exhausted
  - Missing API key error
  - Context and prior_result included in prompt
- One `@pytest.mark.integration` test that calls the real API (skipped without `ANTHROPIC_API_KEY`)

## New Dependency

`anthropic>=0.40` added to `pyproject.toml` dependencies.

## Files

| File | Action |
|------|--------|
| `orchestrator/src/ingenieer/intent_generator.py` | Create |
| `orchestrator/src/ingenieer/models.py` | Modify (add GeneratorConfig) |
| `orchestrator/src/ingenieer/cli.py` | Modify (add --generate, --generate-only) |
| `orchestrator/tests/test_intent_generator.py` | Create |
| `orchestrator/tests/test_cli.py` | Modify (add generate CLI tests) |
| `orchestrator/pyproject.toml` | Modify (add anthropic dep) |
