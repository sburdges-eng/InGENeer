// Phase 8 prerequisite spike — Metal side of risk R-11 (render path). Headless (no window,
// no app bundle): MTLDevice + compute kernel compiled at runtime from source.
//
// Proves, over the C++-owned page-aligned arena (cpp/metal_arena.{h,cpp}):
//   [1/3] zero-copy `makeBuffer(bytesNoCopy:)`: buffer.contents() == arena pointer; an
//         engine (CPU) write made AFTER buffer creation is GPU-visible with no blit/copy.
//   [2/3] H-27 invalidation: after a realloc, the stale generation-stamped handle is
//         refused at the encode API layer — every time.
//   [3/3] H-27 realloc-under-render: a command buffer made deterministically in-flight via
//         MTLSharedEvent survives an arena grow (old page quarantined, checksum correct),
//         and the old page is released only after the completion handler fires, via the
//         no-op-notify bytesNoCopy deallocator.
import Foundation
import Metal

func die(_ msg: String, _ code: Int32) -> Never {
    FileHandle.standardError.write(("FAIL: " + msg + "\n").data(using: .utf8)!)
    exit(code)
}

// ---------------------------------------------------------------- Metal setup (headless)

guard let device = MTLCreateSystemDefaultDevice() ?? MTLCopyAllDevices().first else {
    die("no Metal device", 10)
}
guard let queue = device.makeCommandQueue() else { die("no command queue", 10) }
guard let sharedEvent = device.makeSharedEvent() else { die("no MTLSharedEvent", 10) }

// Trivial MSL checksum kernel, compiled at runtime — each thread sums a strided slice into
// a partials buffer; the CPU folds the partials. Arena values are small integers, so float
// sums are exact and CPU == GPU comparison is by equality.
let msl = """
#include <metal_stdlib>
using namespace metal;
kernel void checksum_kernel(device const float* verts    [[buffer(0)]],
                            device float*       partials [[buffer(1)]],
                            constant uint&      n        [[buffer(2)]],
                            uint tid    [[thread_position_in_grid]],
                            uint tcount [[threads_per_grid]])
{
    float s = 0.0f;
    for (uint i = tid; i < n; i += tcount) { s += verts[i]; }
    partials[tid] = s;
}
"""

let pipeline: MTLComputePipelineState
do {
    let library = try device.makeLibrary(source: msl, options: nil)
    guard let fn = library.makeFunction(name: "checksum_kernel") else {
        die("kernel function missing", 10)
    }
    pipeline = try device.makeComputePipelineState(function: fn)
} catch {
    die("MSL compile/pipeline failed: \(error)", 10)
}

let gpuThreads = 256
guard let partialsBuf = device.makeBuffer(length: gpuThreads * 4, options: .storageModeShared)
else { die("partials buffer alloc failed", 10) }

// -------------------------------------------------------------------------- C++ arena

let pageSize = 16384
let baseFloats = 12_288

guard let arena = ing_marena_create(baseFloats) else { die("arena alloc failed", 11) }

/// Generation-stamped handle: the only way this spike encodes against arena memory (H-27).
struct EngineBufferHandle {
    let buffer: MTLBuffer
    let rawPage: UnsafeMutableRawPointer
    let generation: UInt64
    let floats: Int
}

/// Wrap the arena's CURRENT page in an MTLBuffer via bytesNoCopy. The deallocator is a
/// no-op (frees nothing) that notifies the arena (H-27); the arena decides when to free.
func makeArenaHandle() -> EngineBufferHandle {
    guard let p = ing_marena_data(arena) else { die("arena data nil", 11) }
    let raw = UnsafeMutableRawPointer(p)
    let len = ing_marena_bytes(arena)
    let gen = ing_marena_generation(arena)
    let n = ing_marena_float_count(arena)
    guard let buf = device.makeBuffer(
        bytesNoCopy: raw, length: len, options: [.storageModeShared],
        deallocator: { ptr, _ in _ = ing_marena_page_release(arena, ptr) })
    else { die("makeBuffer(bytesNoCopy:) returned nil", 11) }
    return EngineBufferHandle(buffer: buf, rawPage: raw, generation: gen, floats: n)
}

/// Encode the checksum kernel against a handle. REFUSES stale handles (generation check at
/// the API layer — H-27 invalidation): returns nil without touching the GPU.
func encodeChecksum(_ h: EngineBufferHandle,
                    waitFor eventValue: UInt64? = nil) -> MTLCommandBuffer? {
    guard ing_marena_handle_is_current(arena, h.generation) != 0 else { return nil }
    guard let cb = queue.makeCommandBuffer() else { die("no command buffer", 10) }
    if let v = eventValue { cb.encodeWaitForEvent(sharedEvent, value: v) }
    guard let enc = cb.makeComputeCommandEncoder() else { die("no encoder", 10) }
    enc.setComputePipelineState(pipeline)
    enc.setBuffer(h.buffer, offset: 0, index: 0)
    enc.setBuffer(partialsBuf, offset: 0, index: 1)
    var n = UInt32(h.floats)
    enc.setBytes(&n, length: MemoryLayout<UInt32>.size, index: 2)
    enc.dispatchThreads(MTLSize(width: gpuThreads, height: 1, depth: 1),
                        threadsPerThreadgroup: MTLSize(width: 64, height: 1, depth: 1))
    enc.endEncoding()
    return cb
}

func cpuChecksum(_ p: UnsafePointer<Float>, _ n: Int) -> Float {
    var s: Float = 0
    for i in 0..<n { s += p[i] }  // exact: small-integer values
    return s
}

func gpuChecksum() -> Float {
    let p = partialsBuf.contents().bindMemory(to: Float.self, capacity: gpuThreads)
    var s: Float = 0
    for i in 0..<gpuThreads { s += p[i] }
    return s
}

func percentileLine(_ xs: [Double]) -> String {
    let s = xs.sorted()
    func pct(_ p: Double) -> Double { s[min(s.count - 1, Int(Double(s.count) * p))] }
    return String(format: "min %.1f  p50 %.1f  p99 %.1f  max %.1f (µs)",
                  s.first! / 1000, pct(0.5) / 1000, pct(0.99) / 1000, s.last! / 1000)
}

// ------------------------------------------ Proof 1: zero-copy bytesNoCopy + GPU visibility

// Optional so teardown can DROP the final buffer before destroying the arena (H-27:
// arena outlives all MTLBuffers — so all buffers must die first).
//
// NOTE: this file is top-level Swift, whose implicit autorelease pool never drains. Every
// access to an MTLBuffer (even `h.buffer.contents()`) can autorelease a reference, so ALL
// buffer touches happen inside explicit `autoreleasepool` blocks — otherwise a phantom
// reference pins the buffer for the process lifetime and the bytesNoCopy deallocator never
// fires (this is exactly the ARC hazard class H-27 warns about).
var live: EngineBufferHandle? = makeArenaHandle()

autoreleasepool {
    let h = live!
    guard UInt(bitPattern: h.rawPage) % UInt(pageSize) == 0 else {
        die("arena page not 16KB-aligned", 2)
    }
    guard ing_marena_bytes(arena) % Int(pageSize) == 0 else {
        die("arena length not a page multiple", 2)
    }
    guard h.buffer.contents() == h.rawPage else {
        die("bytesNoCopy buffer is NOT zero-copy (contents != arena pointer)", 2)
    }

    // Engine write AFTER the MTLBuffer exists — must be GPU-visible with no blit/copy.
    ing_marena_fill(arena, 1)
    let expected0 = cpuChecksum(h.rawPage.assumingMemoryBound(to: Float.self), h.floats)
    guard let cb0 = encodeChecksum(h) else { die("fresh handle refused", 2) }
    cb0.commit()
    cb0.waitUntilCompleted()
    let got0 = gpuChecksum()
    guard got0 == expected0 else {
        die("GPU checksum \(got0) != CPU \(expected0) — CPU write not GPU-visible", 2)
    }
    print("PASS [1/3] zero-copy: buffer.contents == arena page (16KB-aligned, page-multiple"
        + " length); CPU write after buffer creation GPU-visible, no blit (checksum \(got0))")
}

// -------------------- Proofs 2+3: stale-handle refusal + realloc-under-render (H-27), looped

let iters = 1000
var staleRefusals = 0
var checksumOK = 0
var earlyReleases = 0
var createNs: [Double] = []
var growNs: [Double] = []
var gpuLatencyNs: [Double] = []
createNs.reserveCapacity(iters)
growNs.reserveCapacity(iters)
gpuLatencyNs.reserveCapacity(iters)

let loopStart = DispatchTime.now()

for i in 0..<iters {
    autoreleasepool {
        // Engine writes this frame's data; snapshot the expected checksum of the OLD page.
        ing_marena_fill(arena, UInt32(i + 2))
        let staleHandle = live!
        let expected = cpuChecksum(staleHandle.rawPage.assumingMemoryBound(to: Float.self),
                                   staleHandle.floats)
        let releasedBefore = ing_marena_pages_released(arena)

        // Commit a command buffer that is deterministically IN-FLIGHT: it waits on the
        // shared event, which the CPU signals only after the realloc below.
        let eventValue = UInt64(i + 1)
        guard let cb = encodeChecksum(staleHandle, waitFor: eventValue) else {
            die("current handle refused at iter \(i)", 3)
        }
        let sem = DispatchSemaphore(value: 0)
        cb.addCompletedHandler { _ in sem.signal() }
        cb.commit()

        // (3a) Realloc UNDER the in-flight render: old page must be quarantined, not freed.
        let t0 = DispatchTime.now()
        let newGen = ing_marena_grow(arena, baseFloats + ((i % 7) + 1) * 96)
        growNs.append(Double(DispatchTime.now().uptimeNanoseconds - t0.uptimeNanoseconds))
        guard newGen == staleHandle.generation + 1 else { die("generation not bumped", 3) }
        guard ing_marena_pages_quarantined(arena) == 1 else {
            die("old page not quarantined at iter \(i)", 3)
        }

        // (3b) The stale handle must be refused at the encode API layer.
        if encodeChecksum(staleHandle) == nil { staleRefusals += 1 }

        guard cb.status != .completed else { die("command buffer not in flight", 3) }
        // Release the GPU: realloc demonstrably happened before kernel execution.
        let t1 = DispatchTime.now()
        sharedEvent.signaledValue = eventValue
        sem.wait()
        gpuLatencyNs.append(Double(DispatchTime.now().uptimeNanoseconds
                                   - t1.uptimeNanoseconds))

        // (3a) In-flight command completed correctly against the quarantined OLD page.
        if gpuChecksum() == expected { checksumOK += 1 }
        // (3c) Page must still be alive — released only AFTER completion + buffer drop.
        if ing_marena_pages_released(arena) != releasedBefore { earlyReleases += 1 }
        guard ing_marena_pages_quarantined(arena) == 1 else {
            die("old page vanished before release notification at iter \(i)", 3)
        }

        // Rebind the live handle to the NEW page; the old MTLBuffer's last references die
        // when this autoreleasepool drains, firing the no-op-notify deallocator.
        let t2 = DispatchTime.now()
        live = makeArenaHandle()
        createNs.append(Double(DispatchTime.now().uptimeNanoseconds - t2.uptimeNanoseconds))
    }

    // Pool drained: the deallocator must notify the arena, which frees the old page NOW —
    // i.e. strictly after the completion handler fired.
    let target = UInt64(i + 1)
    let deadline = DispatchTime.now() + .seconds(2)
    while ing_marena_pages_released(arena) < target {
        if DispatchTime.now() > deadline {
            die("deallocator never notified arena (iter \(i))", 4)
        }
        usleep(50)
    }
    guard ing_marena_pages_quarantined(arena) == 0 else {
        die("quarantine not empty after release (iter \(i))", 4)
    }
}

let totalMs = Double(DispatchTime.now().uptimeNanoseconds
                     - loopStart.uptimeNanoseconds) / 1_000_000

guard staleRefusals == iters else {
    die("stale handle accepted: only \(staleRefusals)/\(iters) refused", 5)
}
print("PASS [2/3] H-27 invalidation: \(staleRefusals)/\(iters) stale encode attempts "
    + "refused at the API layer after realloc")

guard checksumOK == iters && earlyReleases == 0 else {
    die("realloc-under-render: \(checksumOK)/\(iters) checksums OK, "
        + "\(earlyReleases) early releases", 6)
}
print("PASS [3/3] H-27 realloc-under-render: \(checksumOK)/\(iters) in-flight command "
    + "buffers completed correctly against the quarantined page; \(earlyReleases) early "
    + "releases; old page freed only after completion handler + deallocator notify, "
    + "every iteration")

print("")
print("metal interop spike (Swift/Metal <- C++ page-aligned arena, headless):")
print("  device:                  \(device.name)")
print("  floats (base):           \(baseFloats)  (page-aligned, \(ing_marena_bytes(arena)) B live block)")
print("  iterations:              \(iters), failures: 0")
print("  makeBuffer(bytesNoCopy): \(percentileLine(createNs))")
print("  arena grow (realloc):    \(percentileLine(growNs))")
print("  signal->GPU-complete:    \(percentileLine(gpuLatencyNs))")
print(String(format: "  whole loop:              %.1f ms total, %.2f ms/iter "
    + "(incl. deallocator round-trip)", totalMs, totalMs / Double(iters)))

// Teardown honoring H-27: drop the last buffer, wait for its deallocator notification,
// and only then destroy the arena (the arena outlives all MTLBuffers).
let allNotifications = UInt64(iters + 1)  // every quarantined page + the final live page
autoreleasepool { live = nil }
let teardownDeadline = DispatchTime.now() + .seconds(2)
while ing_marena_release_notifications(arena) < allNotifications {
    if DispatchTime.now() > teardownDeadline {
        die("final live-page deallocator never fired before arena destroy", 7)
    }
    usleep(50)
}
ing_marena_destroy(arena)

print("PASS")
