# Phase 4 — Swift↔C++ interop spike (risk R-11)

Proves that Swift can call the C++23 engines and share a buffer **zero-copy** at viewport
rates, and exercises the generation-stamped buffer-lifetime contract (plan H-27).

## Run

```bash
./run.sh
```

Requires `clang++` (C++23) and `swiftc`. Builds the C++ arena, links it into a Swift
executable that consumes arena memory zero-copy, and prints measurements.

## What it shows

- **Pure FFI dispatch ≈ 0.7 ns/call** — the Swift→`extern "C"` boundary is effectively free
  at 60–120 Hz frame budgets.
- **Zero-copy**: Swift wraps the C++-owned pointer in an `UnsafeBufferPointer`; in-place
  engine mutations are visible without any copy or re-fetch.
- **Lifetime safety (H-27)**: a view captured before a step is correctly rejected as stale
  after the arena's generation advances.

## Layout

| Path | Role |
|---|---|
| `cpp/buffer_source.h` | narrow `extern "C"` ABI (opaque handle + POD view; no C++ types/exceptions — C-4.5) |
| `cpp/buffer_source.cpp` | rich C++ arena behind the ABI |
| `swift/main.swift` | zero-copy consumer + benchmark + spike assertions |
| `run.sh` | build + run |

## Outcome

Decision recorded in [ADR-0025](../../../docs/adr/ADR-0025-swift-cpp-interop-direct-c-abi.md):
adopt the direct C ABI (no Obj-C++ shim); A-6 holds; R-11 mitigated for the CPU buffer path.
Remaining: a Metal `bytesNoCopy` + realloc-under-render test closes the render path (Phase 8).

> Spike code — not production. `build/` is gitignored.
