---
name: intent-command-gen
description: "Scaffold a new AutonomAtIon intent command across all layers: catalog entry, schema allowlist, C# dispatcher stub, and test cases. Usage: /intent-command-gen <CommandName> <risk:low|high>"
disable-model-invocation: true
args: command_name risk_tier
---

# Generate Intent Command

Scaffold a new intent command across the AutonomAtIon stack. This enforces consistency between the intent catalog, Python validation, C# dispatch, and tests.

## Inputs

- `command_name`: PascalCase command name (e.g., `DrawPolylineFromCoordinates`)
- `risk_tier`: `low` or `high` (high requires `humanConfirmationToken`)

## Steps

### 1. Validate the command doesn't already exist

```bash
cd /Users/seanburdges/Dev/InGENeer
grep -n "<command_name>" orchestrator/src/ingenieer/intent_validation.py
grep -n "<command_name>" docs/INTENT_COMMAND_CATALOG.md
```

If found, STOP and report: "Command already exists."

### 2. Add to INTENT_COMMAND_CATALOG.md

Append a new row to the command table in `docs/INTENT_COMMAND_CATALOG.md`:

```markdown
| <command_name> | <risk_tier> | <parameters description> | <execution semantics> |
```

Ask the user to describe parameters and semantics before writing.

### 3. Add to Python allowlist

In `orchestrator/src/ingenieer/intent_validation.py`:
- Add `"<command_name>"` to the `ALLOWED_COMMANDS` frozenset
- Add `"<command_name>": "<risk_tier>"` to the `COMMAND_RISK` dict

### 4. Add C# dispatcher stub

In `icad-addin/InGENeer.IcadBridge/IntentRouter.cs`, add a case to the command switch:

```csharp
case "<command_name>":
    // TODO: Implement <command_name> using Carlson iCAD API.
    // Requires: [cite specific API docs needed]
    // Parameters: <describe expected parameters>
    return BridgeExecutionResult.Stub("<command_name>", intent);
```

**Critical**: Do NOT invent Carlson/ITC/IntelliCAD API calls. Use `// TODO` with a doc citation request per Rule 4.

### 5. Add test cases

In `orchestrator/tests/test_intent_validation.py`, add:
- Test that the command passes validation with valid envelope
- Test that high-risk commands require `humanConfirmationToken`

In `orchestrator/tests/test_orchestrator.py`, add:
- Dry-run test for the new command
- If high-risk: test that execute mode without confirmation is rejected

### 6. Verify

```bash
cd /Users/seanburdges/Dev/InGENeer/orchestrator
ruff check src tests
python -m pytest -q
```

### 7. Report

Show the user what was created:
- Catalog entry (with link to file:line)
- Allowlist + risk tier entries
- C# stub (with `// TODO` markers)
- Test cases added
- Lint + test results
