# coordinate_core

PROJ-backed coordinate reference system (CRS) layer for the InGENeer C++ engine (Phase 7).

## What it provides

- **`Crs`** — a cheap, copyable, comparable value type identifying a coordinate reference
  system. Built via `make_crs(definition)` from **any** PROJ-acceptable string: an
  authority code (`"EPSG:6340"`), WKT2, or a PROJ string. Equality is CRS identity
  (PROJ-normalized definitions compare equal).
- **`Transform`** — RAII owner of a PROJ context + transformation object; `forward()` for a
  single `Coordinate` and a batched `std::span`. All fallible calls return
  `std::expected<T, CrsError>` (C-4.5).
- **`CrsStamped<T>` / `stamp()`** — the **R-4.4 recompute invariant** ("CRS changes MUST
  force recomputation"; carried TOTaLi invariant). A derived/cached result is bound to the
  CRS it was produced under; `needs_recompute(currentCrs)` is true after a CRS change and
  the CRS-checked accessor `value_for(currentCrs)` returns `std::nullopt` for a stale CRS,
  so a downstream consumer cannot silently reuse a result computed under a different CRS.
- **`proj_version()`** — PROJ runtime version string, for audit/oracle provenance pinning.

## No US hardcoding (ADR-0010)

The v1 *product* scope is US-only, but this *layer* bakes in zero US assumptions: arbitrary
CRS flow through the same generic PROJ entry points. The test suite exercises both a US
projected CRS (EPSG:6340) and a European one (EPSG:25832) to prove the property. Widening
the product later is configuration, not a rewrite.

## Thread-safety

PROJ contexts are **not** thread-safe, so each `Transform` owns its own `PJ_CONTEXT` and
`PJ`. A single `Transform` must not be shared across threads without external
synchronization; distinct `Transform` objects on distinct threads are fully independent.
`Crs` values are plain strings and are freely copyable/comparable across threads.

## Determinism

Transform math is PROJ's own IEEE-754 floating point — this module is **not** exact-predicate
territory and does not route through `geometry_core`. PROJ is deterministic for a fixed
version; `proj_version()` is exposed so certified outputs can pin the exact PROJ used.

## Dependency: PROJ (system, not vendored)

This module links **system PROJ** (Homebrew: `/opt/homebrew/opt/proj`, found via
`find_package(PROJ CONFIG REQUIRED)` → `PROJ::proj`). PROJ is **MIT/X11-licensed**,
compatible with the Apache-2.0 open core (ADR-0021). It is a system dependency and is
**not** vendored into `third_party/`, so `third_party/` governance and the
license-allowlist scan do not apply to it. PROJ headers/types never appear in the public
API (opaque `void*` handles owned by RAII inside `src/crs.cpp`), so `PROJ::proj` is a
PRIVATE link dependency and is not transitively exposed to consumers.

If CMake cannot locate PROJ, pass a hint, e.g.:

```
cmake --preset dev -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/proj
```
