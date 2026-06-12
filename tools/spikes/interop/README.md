# Phase 4 + Phase 8 prereq — Swift↔C++ interop spike (risk R-11)

Proves that Swift can call the C++23 engines and share a buffer **zero-copy** at viewport
rates (CPU path, Phase 4), and that the same C++-owned memory can back Metal
`makeBuffer(bytesNoCopy:)` buffers safely under realloc-while-rendering (render path,
Phase 8 prerequisite). Both exercise the generation-stamped buffer-lifetime contract
(plan H-27).

## Run

```bash
./run.sh          # CPU spike, then the Metal test via run_metal.sh
./run_metal.sh    # Metal test only (ASan gate + headless GPU run)
```

Requires `clang++` (C++23), `swiftc`, and (for the Metal part) a Metal device. The Metal
test is fully headless: `MTLDevice` + a compute kernel compiled at runtime — no window, no
app bundle.

## What it shows

CPU path (`swift/main.swift`, Phase 4):

- **Pure FFI dispatch ≈ 0.7 ns/call** — the Swift→`extern "C"` boundary is effectively free
  at 60–120 Hz frame budgets.
- **Zero-copy**: Swift wraps the C++-owned pointer in an `UnsafeBufferPointer`; in-place
  engine mutations are visible without any copy or re-fetch.
- **Lifetime safety (H-27)**: a view captured before a step is correctly rejected as stale
  after the arena's generation advances.

Render path (`swift/metal_main.swift` over `cpp/metal_arena.{h,cpp}`, Phase 8 prereq):

- **Zero-copy `bytesNoCopy`**: `MTLBuffer` over the 16KB-page-aligned C++-owned arena;
  `buffer.contents() == arena pointer`; an engine (CPU) write made *after* buffer creation
  is GPU-visible with no blit/copy (`.storageModeShared`).
- **Realloc-under-render is safe (H-27)**, proven 1000/1000 iterations with deterministic
  ordering via `MTLSharedEvent`: (a) an in-flight command buffer completes correctly
  against the **quarantined** old page; (b) the stale generation-stamped handle is refused
  at the encode API layer; (c) the old page is freed only after the completion handler +
  the no-op-notify `bytesNoCopy` deallocator fire.
- **ASan gate**: the quarantine/realloc-under-read arena logic runs clean under
  AddressSanitizer in a CPU-only harness (`cpp/metal_arena_asan_main.cpp`) — Metal+ASan is
  unreliable, so GPU ordering is proven separately by the live run.
- **ARC hazard demonstrated**: any un-pooled MTLBuffer access in top-level Swift pins the
  buffer via the never-draining implicit autorelease pool and silently blocks the
  deallocator — exactly the H-27 hazard class; the spike confines all buffer touches to
  explicit `autoreleasepool` blocks.

## Layout

| Path | Role |
|---|---|
| `cpp/buffer_source.h` | narrow `extern "C"` ABI (opaque handle + POD view; no C++ types/exceptions — C-4.5) |
| `cpp/buffer_source.cpp` | rich C++ arena behind the ABI (CPU spike) |
| `swift/main.swift` | zero-copy CPU consumer + benchmark + spike assertions |
| `cpp/metal_arena.h` | C ABI for the page-aligned arena with quarantine + release notify (H-27) |
| `cpp/metal_arena.cpp` | page-aligned arena: posix_memalign 16KB pages, generation stamps, quarantine |
| `cpp/metal_arena_asan_main.cpp` | CPU-only ASan exercise of the arena lifetime logic |
| `swift/metal_main.swift` | headless Metal test: bytesNoCopy + realloc-under-render proofs |
| `run.sh` | build + run everything |
| `run_metal.sh` | build + run the Metal test (invoked by `run.sh`) |

## Outcome

Decision recorded in [ADR-0025](../../../docs/adr/ADR-0025-swift-cpp-interop-direct-c-abi.md):
adopt the direct C ABI (no Obj-C++ shim); A-6 holds; R-11 mitigated for the CPU buffer path.
The Metal `bytesNoCopy` + realloc-under-render test (this spike, Phase 8 prereq) closed the
render path — see the ADR-0025 addendum; R-11 is **closed** in the risk register.

> Spike code — not production. `build/` is gitignored.
