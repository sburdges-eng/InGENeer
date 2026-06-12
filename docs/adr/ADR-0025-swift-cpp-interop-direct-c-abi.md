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

## Addendum (2026-06-11) — Render path (Phase 8 prereq): bytesNoCopy + realloc-under-render results

The deferred GPU step was built and run headless (no window/app bundle) at
`tools/spikes/interop/` (`run_metal.sh`, invoked by `run.sh`): a 16KB-page-aligned
C++-owned arena (`cpp/metal_arena.{h,cpp}`, `posix_memalign`, page-multiple lengths) wrapped
in `makeBuffer(bytesNoCopy:length:options:.storageModeShared,deallocator:)` and read by a
runtime-compiled MSL compute kernel (`swift/metal_main.swift`), with `MTLSharedEvent` used
to make the realloc-under-render ordering deterministic.

### Measured result (Apple M4 / Metal 4, Apple clang 21 / Swift 6.3, `-O`, 1000 iterations, 0 failures)

| Metric | Value |
|---|---|
| Zero-copy: `buffer.contents()` == arena pointer (16KB-aligned, page-multiple length) | yes |
| CPU (engine) write after buffer creation GPU-visible, no blit/copy | yes (checksum-exact) |
| In-flight command buffer completes correctly against the quarantined old page after realloc | 1000/1000 |
| Stale generation-stamped handle refused at the encode API layer after realloc | 1000/1000 |
| Old page freed only after completion handler + no-op-notify deallocator | 1000/1000 (0 early releases) |
| `makeBuffer(bytesNoCopy:)` cost | ~3 µs p50 (~10 µs p99) |
| Arena grow/realloc (new page + copy + quarantine + generation bump) | ~1.3 µs p50 |
| Signal→GPU-complete round trip (compute dispatch) | ~210 µs p50, ~400 µs p99 |
| ASan (CPU-only harness of the quarantine/realloc-under-read arena logic) | clean |

The H-27 lifetime contract behaved exactly as specified: the arena outlives all MTLBuffers;
grow quarantines (never frees) the old page so in-flight encoders stay valid; the
`bytesNoCopy` deallocator is a no-op that notifies the arena, which alone decides when to
free. Metal + ASan being unreliable, the sanitizer gate runs the same arena logic in a
CPU-only harness (`cpp/metal_arena_asan_main.cpp`); GPU ordering is proven by the live run.

One hazard worth recording (it is the H-27 ARC class verbatim): in top-level Swift code the
implicit autorelease pool never drains, so any un-pooled MTLBuffer access pins the buffer
for the process lifetime and silently prevents the deallocator from ever firing. The spike
confines every buffer touch to explicit `autoreleasepool` blocks; production renderer code
(Phase 8) must respect the same discipline around per-frame buffer handles.

**Conclusion: A-6 still holds on the render path.** Direct C ABI + `bytesNoCopy` zero-copy
sharing is safe at viewport rates under the H-27 contract; no Obj-C++ shim and no GPU-side
copies are needed. R-11 is **closed** in the risk register; the Phase 8 RHI seam can build
on this pattern as-is.
