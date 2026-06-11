# ADR-0025: Swift↔C++ interop via a direct C ABI with zero-copy buffers

**Status:** Accepted
**Date:** 2026-06-11
**Deciders:** Implementation session (Phase 4 spike)
**Related:** RISK_REGISTER R-11 (Swift interop), ASSUMPTIONS A-6, plan §4 / §8 H-7 / H-27, ADR-0014 (languages), ADR-0015 (Metal-first rendering)

## Context

R-11 asks whether Swift can drive the C++23 engines at viewport rates without an
unacceptable interop tax, and whether a zero-copy buffer can be shared from a C++-owned
arena into Swift (and onward to Metal) safely. A-6 assumed direct interop is viable; H-7
pre-approved an Objective-C++ shim fallback if not.

A runnable spike was built at `tools/spikes/interop/`:
- C++ side (`cpp/buffer_source.{h,cpp}`): a `std::vector<float>` vertex arena exposed across a
  narrow `extern "C"` ABI — opaque handle, POD `ing_view` (pointer + count + generation), no
  C++ types or exceptions cross the boundary (C-4.5). Generation-stamped views implement the
  H-27 lifetime/invalidation contract.
- Swift side (`swift/main.swift`): imports the C header, wraps arena memory in an
  `UnsafeBufferPointer` **zero-copy** (no `Array` copy), runs 1,000,000 engine steps, and
  separately measures pure FFI dispatch cost with a trivial accessor in a tight loop.

## Measured result (Apple clang 21 / Swift 6.3, arm64, `-O`)

| Metric | Value |
|---|---|
| Pure FFI dispatch (Swift → C ABI, trivial call) | **~0.7 ns/call** |
| Engine step (300k float updates, work-bound) | ~16 µs/step (memory bandwidth, not interop) |
| Zero-copy mutation visible in Swift without re-fetch/copy | yes |
| Stale view rejected after generation bump (H-27) | yes |

At 0.7 ns/call the boundary crossing is negligible relative to a 60–120 Hz frame budget
(8–16 ms); even thousands of calls per frame cost microseconds.

## Decision

1. **Adopt the direct C ABI ("hourglass") as the Swift↔C++ interop mechanism.** No
   Objective-C++ shim is required for the buffer-sharing / engine-call path. H-7's shim
   fallback is **not invoked**; it remains on the shelf only for Apple-API surfaces that have
   no C representation.
2. **Zero-copy buffer sharing is confirmed (A-6 holds).** Engine-owned arenas are exposed to
   Swift/Metal as pointer+count+generation views; Swift never copies vertex data to read it.
3. **The generation-stamp lifetime contract (H-27) is the required pattern** for any shared
   buffer: the C++ arena outlives all views; a realloc advances the generation and
   invalidates outstanding views; consumers must check currency before use.

## Consequences

- `interop_core` (Phase 4/10) seam is specified as a C ABI; this ADR + the spike are its
  reference. The Plugin SDK ABI spec (`docs/superpowers/specs/2026-06-11-plugin-sdk-abi-spec.md`)
  uses the same hourglass shape.
- **Spike caveats / follow-ups before locking the seam:**
  - The spike links a static object, so the trivial call may be partially inlined; the true
    cost across a real `.dylib` boundary is a few ns — still negligible, but should be
    re-measured once engines ship as a library.
  - No GPU step yet: a Metal `MTLBuffer(bytesNoCopy:)` over the arena, plus a
    **realloc-under-render** test (H-27), is required to fully close R-11 on the render path
    (Phase 8 dependency).
- Risk register: R-11 downgraded from open to **mitigated** (direct interop proven for the
  CPU-side buffer path; render-path confirmation tracked into Phase 8).

## Rejected alternatives

- **Objective-C++ shim (H-7 fallback):** unnecessary given measured direct-interop cost; adds
  a translation layer and ARC bridging complexity for no benefit on this path.
- **Copy-in/copy-out buffers:** defeats zero-copy at viewport rates; rejected.
