# ADR-0020 — ODA Membership Terms vs Open-Core Structure (R-10)

**Status:** Accepted (resolves risk R-10).  
**Date:** 2026-06-11  
**Supersedes:** Pending-verification note in [ADR-0018](ADR-0018-oda-dwg-codec.md).  
**Sources reviewed:** ODA Membership Rules and Policies (Oct 2025), Commercial Membership Agreement, [oda-membership](https://www.opendesign.com/oda-membership), [business FAQ](https://www.opendesign.com/faq/business-questions) (accessed 2026-06-11).

## Context

ADR-0018 chose ODA for DWG fidelity. Risk R-10 asked whether ODA redistribution terms fit an open-core monorepo where `libs/` is permissively licensed and publicly distributed.

## Findings (verified)

| Topic | ODA rule | InGENeer implication |
|-------|----------|----------------------|
| SDK license shape | Members receive a **limited, nonexclusive, royalty-free Object Code** license to Development Tools; **standalone sublicense of Development Tools is prohibited** | ODA binaries/libs **cannot** ship inside the open Core repo or headers |
| Distribution vehicle | Redistribution is only as part of a **Member Application** (finished product), not as SDK resale | DWG support ships as **closed bridge** (subprocess or closed package), not as open `libs/interop_core` source |
| Commercial tier | **≤100 seats/year** (paid + trial licenses count) | Insufficient for v1 GTM beyond pilot; **not** the target membership |
| Sustaining tier | **Unlimited** commercial distribution; **web/SaaS permitted** | **Minimum tier** for product distribution |
| Founding tier | Source access + Git repos | Optional later; not required for subprocess bridge pattern |
| Tracking / audit | ODA may embed passive tracking; members must preserve notices and furnish Member Application copies on request | Closed bridge build must preserve ODA proprietary notices; legal review at packaging time |
| Policy changes | ODA may modify Membership Rules with board approval + notice | Monitor ODA policy page quarterly; drift report if terms conflict with C-2.2 |

## Decision

1. **R-10 is resolved:** open-core + closed ODA bridge is **compatible** when C-2.2 is enforced mechanically.
2. **Membership tier:** plan for **Sustaining** (not Commercial) before any paid DWG distribution beyond internal pilot (≤100 seats).
3. **Integration shape (unchanged from ADR-0018):** auracad-style **subprocess bridge** — open Core talks DWG only via IPC/API to a **closed** `interop_odaw` (working name) binary; no ODA headers in open `libs/` public API.
4. **Repo layout (pre–Stage 1):** closed ODA artifacts live outside permissively licensed trees (e.g. closed package, private build lane, or `apps/` distribution bundle) — never in Apache-licensed `libs/` sources.
5. **Purchase gate:** signing ODA membership remains a **business action**; this ADR verifies technical/legal fit only.

## Rejected

- Shipping ODA-linked object code inside the open Core repository (violates ODA standalone prohibition + C-2.2).
- Relying on Commercial membership for production GTM (100-seat cap).
- Re-opening libredwg/GPL path (ADR-0018).

## Consequences

- Phase 3+ `interop_core` spec may define **open** DXF/LandXML/IFC paths only; DWG spec references this ADR and stays closed.
- CI license scan (Phase 2) must fail if ODA SDK artifacts appear under `libs/`.
- `project_state.json` open question “ODA terms verification” may close; budget line for Sustaining membership should appear in business planning.
