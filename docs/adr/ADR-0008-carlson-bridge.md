# ADR-0008 — Carlson/IntelliCAD Bridge: Transitional First, Strategic Second

**Status:** Accepted (D9).

## Decision
The legacy InGENeer C# IntelliCAD/Carlson bridge ships as a **migration ramp**: it exists to move firms off Carlson/Civil3D with minimal disruption. Investment stays proportional to that role. If mixed-shop demand persists after native workflows mature, the bridge graduates to maintained, first-class interop ("strategic second").

## Rejected
- Strategic-first: front-loads investment in someone else's platform.
- Cutting it: abandons the most credible adoption path for entrenched firms.

## Consequences
Bridge lives under `interop_core`/tools, retains domain isolation (no LLM in C# execution paths); deprecation criteria defined when native field-to-finish reaches parity for migrated workflows.
