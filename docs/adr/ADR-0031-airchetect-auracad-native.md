# ADR-0031: AIrchetect uses auracad-native execution (not FreeCAD)

**Status:** Proposed
**Date:** 2026-06-22
**Deciders:** Project maintainer
**Supersedes:** The "FreeCAD worker" reference in `AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md` §1.

## Context

The AutonomAtIon program defines two tracks — InGENeer (civil/survey) and AIrchetect (architectural 3D). The original architecture rules named FreeCAD as the AIrchetect execution host. Since then, auracad has delivered:

- 7 BIM component types (walls, slabs, columns, doors, windows, spaces, storeys)
- Full IFC import/export via IfcOpenShell (Phases 1–4)
- Python bindings via pybind11 (`aura` module with Document façade)
- JSON-driven CLI runner (`auracad`)
- Transaction-based undo/redo with ECS audit log
- Boolean operations (cut/fuse) for door/window placement
- `.aura` ZIP container persistence

These capabilities cover what originally motivated the FreeCAD choice for architectural 3D workflows.

## Decision

AIrchetect targets **auracad-native** as its L6 execution host, using:

- **auracad's pybind11 Python bindings** for in-process execution (L5–L6 in the same process)
- **The `auracad` CLI** for subprocess execution (JSON-driven, same deterministic guarantees)

FreeCAD is not used. The AutonomAtIon architecture rules are updated to reflect this.

## Consequences

### Positive

- **Unified kernel:** one OCCT integration, one build system, one audit trail across InGENeer and AIrchetect
- **No IPC overhead:** in-process Python bindings eliminate the separate-process boundary that FreeCAD would require
- **Existing BIM types:** AIrchetect Phase 1 can start immediately using auracad's 7 AEC types without building a FreeCAD integration layer
- **Single audit chain:** auracad's ECS audit log and transaction system serve both tracks

### Negative

- **auracad dependency grows:** auracad becomes the single kernel for all CAD automation tracks, increasing its criticality
- **New BIM types must be built in auracad:** Phase 2 types (roof, stair, curtain wall, railing) require auracad-side C++ work, not just orchestrator-side Python
- **No FreeCAD parametric design features:** users who need FreeCAD's Part/PartDesign workbench capabilities (spreadsheet-driven parametrics, constraint-based sketching with tolerance analysis) must use FreeCAD standalone

### Neutral

- The AutonomAtIon layer model (L0–L6) is unchanged — only the L6 host identity changes
- The intent contract (`CadIntentEnvelope`) is reused with an `airchetect.` command prefix for domain routing
- All AutonomAtIon rules (domain isolation, threading safety, transaction fail-safes, no API hallucinations) continue to apply

## References

- [AIRCHETECT_SCOPE.md](../../../../docs/AIRCHETECT_SCOPE.md) — full scope definition
- [ARCHITECTURE_REVIEW.md](../../../../docs/ARCHITECTURE_REVIEW.md) — platform-wide dependency matrix
- [AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](../../governance/autonomation/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md) — updated §1
- [auracad README](../../../../projects/auracad/README.md) — current capability inventory
