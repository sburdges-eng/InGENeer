# ADR-0002 — Clean-Sheet Architecture, Knowledge-First Migration

**Status:** Accepted (D3, D22, R9 rule).

## Context
Existing repos contain valuable ideas and working code, but the stack (OCCT-primary, Qt6, C++20, cross-platform-first) was not designed for the Apple-first, open-core, authority-centric platform. Old repos often contain valuable ideas wrapped in unwanted architecture.

## Decision
Design the ideal architecture without sunk-cost constraints. Migrate via strangler pattern with the rule:

> **Migrate knowledge first. Migrate code second. Rewrite foundations when uncertain.**

- Knowledge (requirements, data models, experiments, lessons) is extracted into ADRs/research — always.
- Code is promoted only if tested, documented, and architecturally compatible; otherwise rewritten.
- Foundational layers (core CAD, geometry, storage, AI runtime, data model) default to rewrite.
- TOTaLi's working pipeline serves as the reference oracle for new engines until parity.
- Temporary adapters bridge old components; adapters are deleted, old repos archived (ADR-0019).

## Rejected
- Ratify existing stack: locks in Qt/OCCT-primary decisions contrary to platform goals.
- Freeze-and-rebuild: long dark period with nothing shippable.
- Parallel evolution of old and new: maximal drift risk.

## Consequences
auracad assets expected to port: exact/adaptive predicates, audit-chain design, COGO math, ECS concepts, OCCT wrapper (relocated to interop satellite). Qt UI and OCCT-as-primary do not survive. Rewrite risk tracked as R-1 in the risk register.
