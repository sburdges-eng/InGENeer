# ADR-0023: TOTaLi Oracle Discipline

**Status:** Accepted  
**Date:** 2026-06-11  
**Deciders:** Architecture session (Phase 2.5)  
**Related:** ADR-0003 (Entity Authority), plan §4.3, req R-9, H-10

## Context

TOTaLi remains the reference oracle for surface extraction semantics during migration. InGENeer must validate `surface_core` / `geometry_core` against frozen outputs without silently drifting when TOTaLi evolves.

## Decision

1. **Freeze semantics** — TOTaLi pipeline outputs used as oracles are version-pinned by git SHA in fixture metadata.
2. **Metadata required** — Every oracle fixture includes: source SHA, extraction script path, input hash, per-quantity tolerances (see `research/totali/ORACLE_FIXTURE_PROCEDURE.md`).
3. **Drift gate** — TOTaLi semantic changes require a drift report before fixture regeneration.
4. **Seven invariants** — Subsumed into Entity Authority System; enforcement tests in TOTaLi remain the behavioral oracle until InGENeer ports equivalent guards.

## Consequences

- Phase 6 CI can fail PRs that change surface outputs without drift approval.
- TOTaLi repo stays read-only reference; no code copy into open Core from TOTaLi Python pipeline (semantics only via fixtures).

## References

- `research/totali/EXTRACTION.md`
- `research/totali/ORACLE_FIXTURE_PROCEDURE.md`
- `TOTaLi/AGENTIC_COMPLETION_PLAN.md` §1
