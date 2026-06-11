// Phase 4 interop spike — Swift consumer. Calls the C++-owned arena across the C ABI,
// wraps arena memory ZERO-COPY (no Array copy), measures per-call dispatch overhead, and
// demonstrates the generation-stamp invalidation contract (plan H-27, risk R-11).
import Foundation

let vertexCount = 100_000
let iters = 1_000_000

guard let arena = ing_arena_create(vertexCount) else {
    FileHandle.standardError.write("arena alloc failed\n".data(using: .utf8)!)
    exit(1)
}
defer { ing_arena_destroy(arena) }

// Zero-copy view: wrap the C++-owned pointer directly. No bytes are copied into Swift.
let view0 = ing_arena_view(arena)
let zeroCopy = UnsafeBufferPointer<Float>(start: view0.data, count: Int(view0.count))
let firstBefore = zeroCopy[0]

// Measure FFI dispatch overhead at "viewport rates": iters in-place engine steps.
let start = DispatchTime.now()
var gen: UInt64 = 0
for i in 0..<iters {
    gen = ing_arena_step(arena, UInt32(truncatingIfNeeded: i))
}
let elapsedNs = Double(DispatchTime.now().uptimeNanoseconds - start.uptimeNanoseconds)
let nsPerCall = elapsedNs / Double(iters)

// The zero-copy view reflects the mutation without any re-fetch or copy.
let firstAfter = zeroCopy[0]

// Pure FFI boundary cost: a trivial call (no per-call work) in a tight loop. This isolates
// the cross-language dispatch overhead from the engine-step memory traffic above.
var sink: UInt64 = 0
let ffiStart = DispatchTime.now()
for _ in 0..<iters {
    sink &+= ing_arena_generation(arena)
}
let ffiNs = Double(DispatchTime.now().uptimeNanoseconds - ffiStart.uptimeNanoseconds)
let ffiPerCall = ffiNs / Double(iters)
if sink == 0 { print("") }  // prevent dead-code elimination of the loop

// The pre-step view is now STALE (generation advanced): the lifetime contract holds.
let stillCurrent = ing_view_is_current(arena, view0) != 0

print("interop spike (Swift <- C++ zero-copy):")
print("  vertices:        \(vertexCount)")
print("  steps:           \(iters)")
print("  ns/step (work):  \(String(format: "%.2f", nsPerCall))  (300k float updates/step)")
print("  ns/call (pure FFI dispatch): \(String(format: "%.2f", ffiPerCall))")
print("  zero-copy[0]:    \(firstBefore) -> \(firstAfter)  (mutation visible, no copy)")
print("  stale view rejected: \(!stillCurrent)  (generation \(view0.generation) -> \(gen))")

// Spike assertions: fail loudly if the interop contract is violated.
guard firstAfter > firstBefore else {
    FileHandle.standardError.write("FAIL: zero-copy mutation not visible\n".data(using: .utf8)!)
    exit(2)
}
guard !stillCurrent else {
    FileHandle.standardError.write("FAIL: stale view not invalidated\n".data(using: .utf8)!)
    exit(3)
}
print("PASS")
