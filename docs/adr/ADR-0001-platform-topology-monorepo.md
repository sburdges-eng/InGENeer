# ADR-0001 — Platform Topology: Single Monorepo

**Status:** Accepted (2026-06-11). Supersedes Round 1 decision D2 ("one platform, three repos").

## Context
TOTaLi (Python pipeline, 676 tests), auracad (C++20 CAD, ~32K LOC), and InGENeer (orchestrator + C# bridge) evolved as siblings with informal contracts. Round 1 chose "one platform, 3 repos"; the clean-sheet decision (ADR-0002) made cross-repo contract maintenance the dominant cost.

## Decision
One platform in one monorepo. The InGENeer repo becomes the monorepo root with exactly five top-level directories: `apps/ libs/ research/ docs/ tools/`. TOTaLi and auracad become knowledge/migration sources and eventually archives (ADR-0019). Note: a new `~/Dev/ingeneer` directory is impossible on case-insensitive APFS alongside `InGENeer`; the existing repo is the root.

## Rejected
- Three coordinated repos (D2 original): contract drift risk across a clean-sheet rebuild.
- Independent products: forfeits the platform thesis.

## Consequences
Single build graph and CI; the open/closed seam (ADR-0006) must be expressed as module boundaries within one repo; legacy repos remain read-only references until archived.
