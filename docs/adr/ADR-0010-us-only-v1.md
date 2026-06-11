# ADR-0010 — US-Only V1 Jurisdiction

**Status:** Accepted (D11).

## Decision
V1 targets US practice: state plane coordinate systems, ALTA/NSPS standards, PLSS and metes-and-bounds legal descriptions, county/DOT submission conventions. International support is architected-for (PROJ-based CRS abstraction, no US hardcoding in coordinate_core) but not built.

## Rejected
- US+Canada and international-from-start: scope multipliers on every AI layer (legal language, cadastral diversity) before product-market fit.

## Consequences
legal_ai and survey standards corpus scoped to US; expansion is a data/standards effort, not a re-architecture.
