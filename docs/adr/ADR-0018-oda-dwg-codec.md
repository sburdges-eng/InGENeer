# ADR-0018 — DWG via ODA Membership

**Status:** Accepted (D19). Terms verified — see [ADR-0020](ADR-0020-oda-terms-verification.md) (R-10 resolved). Target membership: **Sustaining** before production distribution.

## Decision
License the Open Design Alliance SDK for DWG read/write. Industry-grade fidelity is essential for Carlson/Civil3D migration trust (ADR-0008). The codec lives in a **closed** interop module or subprocess — never exposed through open Core headers (C-2.2); the open Core ships DXF, LandXML, IFC, glTF, PNEZD.

## Rejected
- libredwg (GPL): fidelity gaps on newer DWG versions; too risky for migration-critical workflows even quarantined in a subprocess.
- DXF-only launch: weakens the migration ramp that the go-to-market depends on.

## Consequences
Annual cost accepted; auracad's existing subprocess-bridge pattern (crash containment, license isolation) carries forward as the integration shape.
