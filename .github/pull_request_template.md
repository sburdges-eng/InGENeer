## Summary

<!-- What changed and why (one short paragraph). -->

## Type of change

- [ ] Orchestrator (Python)
- [ ] Bridge / host (`icad-addin/`)
- [ ] Schema / intent catalog / contracts
- [ ] Docs only
- [ ] CI / tooling

## Checklist

- [ ] Read relevant AutonomAtIon / playbook sections for this layer
- [ ] `python -m pytest -q` from `orchestrator/` (if Python touched)
- [ ] `dotnet build icad-addin/InGENeer.IcadAddin.slnx` (if C# touched)
- [ ] If intent envelope or wire shape changed: schema + catalog + version bumps aligned

## Risk / rollout

<!-- e.g. breaking schema bump, needs host upgrade, feature-flagged -->
