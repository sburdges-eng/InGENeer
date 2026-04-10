## Summary

<!-- What changed and why (one short paragraph). -->

## Contract / versioning

- [ ] N/A — no intent envelope or wire contract changes
- [ ] Intent: updated `schemas/cad_intent_envelope.schema.json`, catalog, and Pydantic/C# DTOs together; noted `schemaVersion` bump
- [ ] Wire: updated `ingenieer.contracts` / `SCHEMA_VERSION` if outer payload shape changed

## Checks

- [ ] `cd orchestrator && ruff check src tests && python -m pytest -q`
- [ ] `dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release` (if C# touched)

## Notes

<!-- Optional: risk, follow-ups, air-gap / host testing. -->
