# Out-of-Core Octree for 100M+ Point Clouds — Design Spec

> **Status:** Draft (design-first per C-5.3 — no implementation accompanies this document)
> **Date:** 2026-06-11 · **Phase:** 6.4 · **Requirement:** R-7.3 · **Risk:** risk R-9 (100M+ scale leg)
> **Module (proposed):** `libs/pointcloud_core` (see §1.3 and Open Question OQ-1)
> **Grounding:** `docs/architecture/REQUIREMENTS.md` (R-7.3, R-3.*, R-4.3/4.4) ·
> `docs/architecture/CONSTRAINTS.md` (C-1.2/1.3, C-4.1–4.6, C-5.3) ·
> `docs/architecture/RISK_REGISTER.md` (risk R-9) ·
> `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` §4.2(5), §5, §8 (H-22, H-27), Phase 6.4/7/8 ·
> `research/auracad/numeric-policy.md` · `docs/adr/ADR-0012-custom-tin-engine.md` · `docs/adr/ADR-0015-metal-first-rendering.md` ·
> `docs/superpowers/specs/2026-06-11-audit-core-storage-schema-spec.md` (boundary statement, §3.4 below)

This spec defines the out-of-core, level-of-detail octree that lets InGENeer ingest,
store, query, and render 100M+ point clouds on 16 GB unified memory. It is the Phase 6.4
deliverable of the agentic plan ("design doc first — C-5.3: rewrite foundations
deliberately", `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md`
Phase 6.4). **This document authorizes no code.** Implementation lands in Phase 7
(builder, PDAL ingestion) and Phase 8 (renderer consumption) after owner review.

---

## 0. Scope

In scope:
- The persistent octree/LOD structure for bulk point-cloud data: structure choice,
  sampling rule, on-disk format, integrity model.
- The in-process memory model: arena, node cache, eviction, generation-stamped handles
  (H-27 contract).
- The out-of-core build pipeline fed by PDAL streams (Phase 7 consumer of this design).
- The query API surface and the LOD/culling contract with the Phase 8 Metal renderer.
- Testing/validation obligations and perf baselines to record.

Out of scope (see §10 Non-Goals for rationale):
- LAS/LAZ **writing**/export; compression codec selection; classification-edit overlay
  format; incremental (modifiable) octree updates; network streaming; sync semantics.

## 1. Problem statement & requirements traceback

### 1.1 The requirement

`docs/architecture/REQUIREMENTS.md` R-7.3 (verbatim):

> "Point-cloud handling MUST scale to 100M+ points on 16 GB unified memory via
> streaming/out-of-core design."

A 100M-point cloud at a realistic in-memory record size (~32 B/point, §3.2) is ~3.2 GB —
loadable, but only barely, and 1B points (32 GB) categorically is not. The structure must
therefore be **out-of-core by construction**: bounded resident set regardless of total
cloud size, with disk as the backing tier and LOD selection deciding what is resident.

### 1.2 Risk traceback

`docs/architecture/RISK_REGISTER.md` risk R-9 names "100M+ pt scale" as part of the
custom-TIN "hard 20%" and lists "out-of-core design upfront" as a mitigation. The plan
(`docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` §4.2 item 5)
operationalizes that: "Out-of-core/LOD: octree with Potree-style nested sampling (feeds
renderer §5; also the R-9-risk 100M+ point mitigation — out-of-core design upfront)."
This spec is that design.

### 1.3 Producer and consumer phases

- **Producer — Phase 7 (`pointcloud_core` + `coordinate_core`):** "PDAL ingestion (fuzz
  LAS/LAZ), nanoflann indexing" (plan Phase 7). The octree builder (§5) consumes a PDAL
  point stream and emits the on-disk structure (§3).
- **Consumer — Phase 8 (rendering spine):** plan §5.2 — "octree node metadata in argument
  buffers …; compute kernel does frustum + screen-space-error LOD culling and encodes
  draws into an ICB; … CPU per-frame cost O(1) in node count. This is the Potree-on-Metal
  architecture for 100M+ points." §7 below fixes that contract.
- **Consumer — surface workflows (Phase 6/7):** TIN seeding, ground filtering, and QA need
  k-NN/radius queries. Those are **delegated** to the nanoflann in-core index (plan §4.2
  item 5: "nanoflann … bulk rebuild, not incremental" is the separate in-core k-NN
  structure); the octree only provides coarse spatial selection (§6.2).

**Module home (proposed):** `libs/pointcloud_core` — ARCHITECTURE.md §4 assigns
"PDAL-backed ingestion" and point-cloud handling there, and `libs/pointcloud_core/` exists
today as a skeleton (`.gitkeep` only). Note the plan lists the octree under §4.2 "Core
algorithms (surface_core)"; this spec treats that as a phase-numbering artifact, not a
module assignment — flagged as OQ-1 in §9 rather than edited (scope rule).

### 1.4 Authority posture (binding boundary)

The octree is a **derived, non-authoritative, rebuildable artifact** — the same posture
the plan gives smoothed contours ("tagged derived/non-authoritative (C-1.3 analog)", plan
§4.2 item 3). Certified measurement paths (R-4.3/R-4.4) never read LOD-sampled or
quantized data as the basis of a certified output; they read the source records in the
project container. The octree may *accelerate* finding those records (coarse selection,
§6.2) but is never itself the measurement substrate. See §3.4 for the audit-chain
boundary.

## 2. Structure choice

### 2.1 Decision: Potree-style nested/additive-sampling octree

Named directly by the plan (§4.2 item 5). Each octree node stores a **spacing-bounded
subsample** of the points in its cube; descendants store progressively denser samples.
"Additive" means the renderable set at LOD *l* is the **union** of node payloads from the
root down to level *l* — every point is stored exactly **once**, in exactly one node, so
the structure has no LOD duplication overhead (total stored points = input points).

Rejected alternatives:

| Alternative | Why rejected |
|---|---|
| Replacing-LOD octree (each level re-stores coarser copies, mesh-LOD style) | 30–35% storage duplication across levels; no benefit for points, where union-of-samples renders correctly |
| Modifiable/dynamic octree (incremental insert/delete, MNO-style) | Survey clouds are write-once measurement evidence; edits are attribute changes (classification), not geometry. Dynamic balancing adds nondeterminism (insertion-order-dependent topology) against C-4.6/H-22 and large complexity against C-5.3. Rebuild-on-change is acceptable: a full 100M rebuild is minutes (§5.5) |
| Out-of-core KD-tree | No natural cubes → no implicit AABBs, poor fit for GPU frustum/SSE culling per node (plan §5.2); octree node = cube is what makes hierarchy records 32 bytes (§3.3) |
| Flat Morton-tiled grid (no hierarchy) | No multi-resolution → distant views would touch every tile; defeats SSE-driven residency on 16 GB |

### 2.2 Geometry parameters

- **Root cube:** the input AABB expanded to a cube (max edge), with origin/edge recorded
  in metadata (§3.1). Cubing keeps every child cell a cube at all levels.
- **Spacing:** `s(l) = rootEdge / (G · 2^l)` with sampling-grid size `G = 128` (tunable
  build constant, recorded in metadata). A node at level *l* holds at most one point per
  cell of its 128³ virtual grid → max ~2.1M theoretical, in practice ≤ `maxNodePoints`.
- **Node capacity:** `maxNodePoints = 20,000` (build constant). A leaf exceeding it
  splits; children with < `minNodePoints = 1,000` may be merged into the parent's residue
  payload at finalize. 20K × 32 B = 640 KB payload → tens of microseconds to load from
  NVMe, small enough for fine-grained eviction, large enough that hierarchy stays tiny
  (§3.3 arithmetic).
- **Depth bound:** Morton codes are 63-bit (21 levels × 3 bits, §5.2) → max depth 21.
  At G=128 a depth-21 node on a 10 km site has spacing ≈ 10⁴ m / (128·2²¹) ≈ 37 µm — far
  below any survey instrument noise floor; the bound is unreachable in practice and is a
  hard, checked invariant.

### 2.3 Deterministic sampling (H-22 / C-4.6 / R-4.4)

C-4.6 forbids RNG in measurement paths; R-4.4 requires identical inputs → identical
outputs; H-22 (plan §8) warns about cross-platform FP divergence. The classic Potree
Poisson-disk subsample uses randomized selection — **rejected here**. Instead:

- Sampling is **selection, not randomness**: within each occupied grid cell of a node,
  exactly one candidate point is promoted into the node. There is no RNG anywhere — not
  even a seeded one — so there is nothing to seed and nothing to diverge.
- The selection key is computed **entirely in integer arithmetic** over the quantized
  coordinates (§3.2): the cell's winner is the point minimizing the tuple
  `(d², morton63, ingestIndex)` where `d²` is the squared int64 distance from the point's
  quantized position to the cell center (visual quality: near-center points sample
  evenly), `morton63` breaks ties, and the global `ingestIndex` (position in the
  concatenated input stream) breaks exact-duplicate ties. Integer min is associative and
  commutative → the result is **independent of thread scheduling and chunk processing
  order**, and bit-identical across macOS arm64 and Linux CI (stronger than the
  tolerance-bounded cross-platform determinism H-22 settles for, because no FP comparison
  participates in any structural decision — mirroring the TIN's "no floating-point
  comparisons of its own" discipline in `libs/surface_core/include/ingeneer/surface/tin.h`).
- The only FP step is the one-time quantization `q = floor((x − origin) / scale)` (§3.2),
  a single defined binary64 expression; the H-4/H-22 compile gates (`-ffp-contract=off`,
  no fast-math — `docs/architecture/CONSTRAINTS.md` C-4.2,
  `research/auracad/numeric-policy.md`) make it platform-stable.

**Determinism contract:** same input bytes + same build constants ⇒ byte-identical
`hierarchy.bin` and `points.bin` (hence identical SHA-256s, §3.4), regardless of thread
count or platform. This is a tested invariant (§8.1).

## 3. On-disk format

### 3.1 Layout: single-blob + index (not file-per-node)

One **directory sidecar** inside the project container (ARCHITECTURE.md §8: "SQLite +
binary sidecars for point clouds/surfaces"):

```
<cloud-id>.octree/
  metadata.json     # format version, build params, bbox/origin/scale, counts,
                    # attribute schema, SHA-256 of hierarchy.bin and points.bin
  hierarchy.bin     # fixed 32-byte node records (§3.3)
  points.bin        # node payloads, each 16 KiB-aligned (§3.2)
```

Rejected: **file-per-node** (Potree 1.x style). 1B points ≈ 10⁵ nodes (§3.3) → 10⁵ small
files per cloud: inode pressure, slow open/stat storms, and — decisive here — per-file
mmap regions defeat the single-arena `bytesNoCopy` story below. Single-blob + index is
the Potree 2.0 lesson, adopted.

**Alignment is load-bearing:** every node payload begins on a 16,384-byte boundary and is
padded to a 16,384-byte multiple. Plan §5.1 requires "page-aligned (16KB) memory owned by
the C++ engine via `makeBuffer(bytesNoCopy:)`" — 16 KiB is the Apple Silicon page size and
a multiple of Linux's 4 KiB, so a node payload `pread` into a 16 KiB-aligned arena slab
(§4) is *itself* a valid `bytesNoCopy` region with zero further copies, and the same
layout is mmap-able if a future revision chooses file-backed mapping (§4.5). Padding cost
at 20K-point nodes: ≤ 16 KiB on ~640 KiB ≈ 2.5% worst case, typically ~1%.

### 3.2 Node payload (SoA, render-grade quantized)

Per plan §5.5, engines "expose renderer-friendly layouts (SoA, page-aligned) as a design
constraint from day one." Within a node payload, attributes are **structure-of-arrays**,
each array 64-byte aligned within the payload:

| Array | Type | Bytes/pt | Notes |
|---|---|---|---|
| position | `int32[3]` | 12 | quantized: `q = floor((x − origin)/scale)`, `scale = rootEdge / 2^31` |
| intensity | `uint16` | 2 | |
| classification | `uint8` | 1 | ASPRS codes |
| return/flags | `uint8` | 1 | return number, scan flags |
| rgb (optional) | `uint16[3]` | 6 | present iff schema flag set |
| gps time (optional) | `float64` | 8 | present iff schema flag set |

Core record = 16 B/pt; with rgb+time ≈ 30 B; this spec budgets **32 B/pt** throughout.
Quantization resolution at `rootEdge = 10 km`: 10⁴ m / 2³¹ ≈ **4.7 µm** — orders of
magnitude below scanner noise; positions are *render-grade and selection-grade*, while
certified measurement reads source records per §1.4 (R-4.3 binary64 discipline is not
violated because the octree is not a measurement path). The attribute schema is declared
in `metadata.json` (which arrays are present, their order/offsets) so the format does not
hard-code a point type — Phase 7's PDAL dimension mapping fills it.

### 3.3 Hierarchy index

Fixed 32-byte little-endian records, breadth-first, children of a node stored
contiguously:

```cpp
// hierarchy.bin record, format INGOCT v1 — 32 bytes, packed, little-endian
struct NodeRecordV1 {
    std::uint64_t payload_offset;   // into points.bin; 16 KiB-aligned
    std::uint32_t payload_bytes;    // padded size (max node ≈ 640 KiB « 4 GiB)
    std::uint32_t point_count;
    std::uint64_t checksum_xxh3;    // XXH3-64 of the UNPADDED payload
    std::uint8_t  child_mask;       // bit i ⇒ child octant i exists
    std::uint8_t  level;
    std::uint16_t flags;            // reserved (leaf/residue markers)
    std::uint32_t first_child;      // NodeId of first child; others follow by popcount
};
static_assert(sizeof(NodeRecordV1) == 32);
```

Node AABBs are **implicit** (cube subdivision from root + octant path) and are
reconstructed during hierarchy load — no coordinates stored per node.

**Arithmetic — the hierarchy is always fully resident.** Average occupancy ~10–20K
pts/node ⇒ 100M pts ≈ 5–10K nodes ≈ **160–320 KB**; 1B pts ≈ 50–100K nodes ≈ **1.6–3.2
MB**. Potree-style chunked/lazy hierarchies exist to serve web streaming of multi-TB
clouds; at our R-7.3 scale they are complexity without payoff — **rejected**
(simplification justified by the numbers above; revisit only if R-7.3's scale target
changes).

### 3.4 Endianness, versioning, integrity — and the audit_core boundary

- **Endianness:** little-endian, fixed. Both mandated platforms (macOS Apple Silicon,
  R-3.1; Linux CI x86-64/aarch64, R-3.5) are LE; a `static_assert` rejects BE builds. No
  byte-swapping reader paths (dead code per C-5.4's no-placeholder spirit).
- **Versioning:** `metadata.json` carries `"format": "ingeneer-octree", "version":
  {"major": 1, "minor": 0}`; `hierarchy.bin`/`points.bin` each open with magic
  `"INGOCT1\0"` + the same version words. Readers reject a newer **major** (fail closed,
  `OctreeErrc::FormatVersion`); minor additions must be ignorable.
- **Per-node integrity:** `checksum_xxh3` (XXH3-64) verified on every node load.
  Non-cryptographic by intent — it detects corruption/torn writes at ~tens of GB/s, i.e.
  negligible against a 640 KB `pread`. SHA-256 per node-load would burn ~0.5–1 GB/s of
  CPU on the hot path for no additional *legal* value, because:
- **Audit boundary (explicit non-tie-in):** **point clouds are bulk measurement data, NOT
  audit-chain entities.** The audit chain (C-1.2;
  `docs/superpowers/specs/2026-06-11-audit-core-storage-schema-spec.md`) records a single
  ingest/build **event** whose payload carries the SHA-256 of `metadata.json` — and
  `metadata.json` carries the SHA-256 of `hierarchy.bin` and `points.bin`. One hash in
  the chain therefore cryptographically anchors the entire derived structure
  (Merkle-style, two levels) without putting bulk data, or per-node hashes, anywhere near
  `audit_core`'s event store. Entities **derived from** clouds (surfaces, breaklines)
  carry authority metadata as usual; the cloud itself is evidence referenced by hash.
  Rebuilding the octree (new build constants, new format version) is a new event with new
  hashes — append-only, never a mutation of a prior record.

## 4. Memory management (arena, cache, eviction, H-27)

### 4.1 Arena

A C++-owned arena of **16 KiB-aligned slabs** allocated in large reservations (e.g.,
64 MiB chunks), per plan §5.1's "page-aligned C++-owned arenas." Each resident node
occupies an integral number of 16 KiB pages inside one slab region. One `MTLBuffer` is
created per arena reservation via `bytesNoCopy` (renderer side, behind the RHI seam —
C-4.4); node payloads are *offsets* into that buffer, so node residency changes never
create or destroy `MTLBuffer`s on the hot path.

### 4.2 Node cache & budget-driven eviction

- Cache key: `NodeId` → `{slot, generation, bytes, pin_count, lru_link, last_fence}`.
- **Budget:** a byte ceiling set by the caller (app policy). Sizing rationale for R-7.3's
  16 GB target: a 4K viewport is ~8.3M px; at ~1 pt/px target density the *visible* set
  is ≈ 8M pts ≈ 256 MB. A default budget of **2 GiB** (~64M resident points at 32 B)
  gives 8× headroom for prefetch and camera motion while leaving 14 GB to the TIN, app,
  and OS. The budget is a parameter, not a constant.
- **Eviction:** strict LRU over unpinned nodes when an `acquire` would exceed budget.
  Eviction increments the slot's **generation**; any outstanding handle becomes stale.
- **Admission:** residency requests come from the traversal (§6.1/§7.4) in priority order
  (coarse levels first), so under thrash the coarse representation stays resident and the
  view degrades gracefully instead of blanking.

### 4.3 Generation-stamped handles (H-27 contract)

Plan §8 H-27 (verbatim mitigation): "Buffer lifetime contract: C++ arena outlives all
MTLBuffers referencing it; generation-stamped handles invalidate in-flight encoders on
realloc; bytesNoCopy deallocator is a no-op that notifies the arena; … Phase 4 spike must
include a realloc-under-render test." This design adopts it wholesale:

```cpp
struct NodeBufferHandle {           // value type; never a pointer across the boundary
    std::uint32_t slot;             // arena slot index
    std::uint32_t generation;       // must match slot's current generation
};
```

- `payload(handle)` fails with `OctreeErrc::StaleHandle` if generations mismatch — a
  use-after-evict is an *error return*, never a dangling pointer.
- **Realloc-under-render:** arena reservations are never `realloc`ed in place; growth
  adds reservations. Eviction of a node whose pages were referenced by a frame still in
  flight defers the pages to a **zombie list** keyed by the frame's `MTLSharedEvent`
  fence value (plan §5.1 hazards); pages are reusable only after the fence signals. The
  arena thus *provably outlives* every GPU reference (first clause of H-27).
- Renderer-side argument-buffer records carry `(slot, generation)` (§7.2); the culling
  kernel skips records whose generation a per-frame CPU patch has marked stale — the GPU
  never chases freed memory even one frame late.

### 4.4 Pinning

CPU consumers (nanoflann index build, §6.2) `acquire`/`release` pins; pinned nodes are
eviction-exempt and counted against the budget. Pin leaks are a debug-build
`KERNEL_DEBUG_ASSERT` (plan §3.4 assertion tiers) on cache teardown.

### 4.5 Considered alternative: mmap-direct (rejected for v1)

Mapping `points.bin` and handing file-backed pages straight to `bytesNoCopy` would skip
the `pread` too. Rejected for v1: (a) the budget accountant cannot see page-cache
eviction, so the 2 GiB contract becomes advisory; (b) the H-27 lifetime contract would
couple `munmap` to GPU fences across a file mapping, a harder invariant than
arena-owns-pages; (c) `pread` of 640 KB nodes on NVMe is ~0.1–0.3 ms — not the
bottleneck. The 16 KiB-aligned format keeps the door open; revisit post-Phase 8 profiling
(OQ-5).

## 5. Build pipeline (out-of-core construction)

### 5.1 Inputs and ordering

Input is a PDAL streaming pipeline (Phase 7; LAS/LAZ readers are fuzzed per plan §3.8 /
Phase 7 — fuzzing the *octree's own* index reader is §8.3 here). Points arrive in
arbitrary order; `ingestIndex` (64-bit position in the concatenated, declared-order input
stream) is assigned on arrival and participates in the deterministic tie-break (§2.3), so
"identical inputs" (R-4.4) means identical bytes in identical declared file order.

### 5.2 Three passes (Schütz-style chunking, deterministic variant)

**Pass 0 — bounds (streaming, O(n) read):** compute the exact AABB (min/max are
order-independent ⇒ deterministic) and total count; fix `origin`, `rootEdge`, `scale`.
LAS headers advertise bounds but are untrusted input — always recomputed.

**Pass 1 — chunking (counting sort on Morton prefix):** stream again; quantize each point
(§3.2), compute its 63-bit Morton code (21 levels × 3 bits); **counting-sort** points
into chunk spill files by the top 3–4 octree levels of the Morton prefix (512–4,096
buckets), splitting any bucket that exceeds the chunk RAM cap (≤ 256 MiB) by extending
its prefix. Records spilled are the fixed quantized form + `ingestIndex` (24 B/pt).

**Pass 2 — per-chunk indexing (in-core, parallel):** each chunk is one subtree and fits
in RAM by construction. Radix-sort the chunk by full Morton code; build its leaf nodes;
then sample **bottom-up**: for each node from deepest level upward, bin its surviving
points into the 128³ grid and promote each cell's winner by the integer key of §2.3 to
the parent. Chunks are independent ⇒ embarrassingly parallel; determinism holds because
every selection is an associative integer min (§2.3).

**Pass 3 — merge & finalize:** sample the chunk roots upward through the top levels
(small, in-core), assign breadth-first NodeIds, concatenate node payloads into
`points.bin` (16 KiB-aligned, XXH3 per node), emit `hierarchy.bin` and `metadata.json`
(with file SHA-256s), fsync, then record the ingest/build audit event (§3.4).

Peak RSS is bounded by `chunkCap × workers + I/O buffers` ≈ 256 MiB × 4 + ~256 MiB ≈
**~1.3 GiB regardless of cloud size** — the out-of-core property R-7.3 demands.

### 5.3 Disk budget (order of magnitude, arithmetic shown)

| Scale | Final `points.bin` (32 B/pt + ~2% pad) | Pass-1 spill (24 B/pt) | Peak disk |
|---|---|---|---|
| 100M pts | 100×10⁶ × 32 B ≈ 3.2 GB → ~3.3 GB | ≈ 2.4 GB | ≈ **5.7 GB** (~1.8× final) |
| 1B pts | ≈ 32 GB → ~33 GB | ≈ 24 GB | ≈ **57 GB** (~1.8× final) |

Spill files are deleted as each chunk finalizes, so peak ≈ final + spill, not 2× spill.

### 5.4 CPU/time budget (order of magnitude, arithmetic shown)

Per-point costs (Apple-Silicon-class core): quantize+Morton ≈ 30–50 ns; radix sort
(8×8-bit passes over 63-bit keys) ≈ 15–20 ns amortized; grid-binning + integer-min
sampling, ~1.3 point-visits total (additive structure) ≈ 30 ns.

- **100M:** CPU ≈ 100×10⁶ × ~100 ns ≈ 10 s single-core ⇒ **~2–4 s on 6–8 cores** + I/O:
  Pass 0 read 3 GB + Pass 1 read 3 GB/write 2.4 GB + Pass 2 read 2.4 GB + Pass 3 write
  3.3 GB ≈ 14 GB traffic ÷ ~2 GB/s NVMe ≈ 7 s ⇒ **target: < 60 s wall** (generous 3–4×
  margin over the ~15 s floor for PDAL decode overhead, LAZ decompression, allocator
  noise).
- **1B:** ×10 ⇒ ~140 GB traffic ≈ 70 s I/O floor, ~100 s/8-core CPU ⇒ **target: < 15 min
  wall**. LAZ-compressed inputs decode at ~1–3M pts/s/core and will dominate; that cost
  belongs to Phase 7's reader, not this structure.

These are recorded as perf baselines, not gates, at Phase 7 exit (§8.5; plan Phase 6 exit
convention "perf benchmarks recorded as baseline").

## 6. Query API sketch

C++23, UI-free, Apple-framework-free (C-4.1); `std::expected` for all fallible calls and
**index handles only** across the module boundary — no raw pointers, matching plan §3.2/
§3.3 and the existing `geometry_core`/`surface_core` style (`libs/surface_core/include/
ingeneer/surface/tin.h`). No exceptions escape any future `extern "C"` facade (C-4.5).

### 6.1 Sketch (signatures only — Phase 7 implements)

```cpp
namespace ingeneer::pointcloud {

struct NodeId { std::uint32_t v; };                       // hierarchy index handle
inline constexpr NodeId kNoNode{0xFFFFFFFFu};

enum class OctreeErrc {
    Io, FormatVersion, ChecksumMismatch, CorruptHierarchy,
    BudgetExhausted, StaleHandle, InvalidNode,
};
struct OctreeError { OctreeErrc code; std::uint32_t node; /* + detail */ };

struct SseParams { float viewport_height_px; float fovy_rad; float pixel_threshold; };
struct TraversalResult {
    std::vector<NodeId> renderable;     // resident, pass SSE+frustum (feeds §7)
    std::vector<NodeId> wanted;         // pass SSE+frustum, NOT resident (residency queue)
};

class OctreeReader {
public:
    static std::expected<OctreeReader, OctreeError>
        open(const std::filesystem::path& dir);           // verifies magic/version/hashes

    // -- residency (pins count against the budget; release to unpin) --
    std::expected<NodeBufferHandle, OctreeError> acquire(NodeId) noexcept;
    void release(NodeId) noexcept;
    // Stale after eviction/generation bump — error, never a dangling view (H-27).
    std::expected<std::span<const std::byte>, OctreeError>
        payload(NodeBufferHandle) const noexcept;

    // -- renderer traversal (double-precision camera; §7.4) --
    std::expected<TraversalResult, OctreeError>
        select(const FrustumD& frustum, const SseParams&,
               std::uint64_t byte_budget) const;

    // -- coarse spatial selection (candidate NODES only; see §6.2 boundary) --
    std::expected<std::vector<NodeId>, OctreeError>
        nodes_intersecting(const AabbD& world_box) const;

    const NodeMeta& node(NodeId) const noexcept;          // count/level/aabb/children
};

class OctreeBuilder {                                      // Phase 7; PDAL feeds append()
public:
    static std::expected<OctreeBuilder, OctreeError>
        create(const BuildParams&, const std::filesystem::path& out_dir);
    std::expected<void, OctreeError> append(std::span<const RawPoint>) noexcept;
    std::expected<BuildReport, OctreeError> finalize();    // passes 1–3, hashes, fsync
};

}  // namespace ingeneer::pointcloud
```

### 6.2 k-NN / radius delegation boundary (binding)

Plan §4.2 item 5 assigns exact neighbor queries to **nanoflann** ("L2_Simple_Adaptor over
SoA float[3]; bulk rebuild, not incremental"). Division of labor:

- The **octree** answers only *coarse* questions: which nodes intersect this box/frustum;
  give me their payloads. It never returns per-point neighbor sets.
- Exact **k-NN/radius** = caller selects nodes via `nodes_intersecting`, pins them,
  **bulk-builds** a nanoflann index over the pinned SoA payloads, queries, releases.
  This keeps one k-NN implementation in the codebase, keeps the octree API small, and
  matches "bulk rebuild, not incremental." A convenience wrapper for this select→pin→
  index dance may live in `pointcloud_core`, but the underlying index is nanoflann —
  the octree does not grow a competing neighbor engine.

## 7. LOD/culling contract with the Phase 8 Metal renderer

All GPU specifics below live **behind the RHI seam** (C-4.4; R-3.4 — a Vulkan backend
must be able to satisfy the same contract with buffer-device-address + indirect draws).
The Metal mapping follows plan §5.1–5.3 / ADR-0015.

### 7.1 Who owns what

| Concern | Owner |
|---|---|
| Residency (load/evict/pin), budget | `pointcloud_core` octree cache (CPU, async) |
| Per-node render metadata table | renderer, **patched** from `TraversalResult` deltas |
| Frustum + SSE test per node, draw encoding | **GPU compute kernel** → ICB (plan §5.2) |
| Wanted-list (what to load next) | CPU traversal `select()` (§7.4) |
| Point rasterization ≥ ~10M visible pts | compute rasterizer, 64-bit atomic depth\|color (plan §5.3) |

### 7.2 Node metadata in argument buffers

One GPU-resident array (argument buffer tier 2 / bindless, `useHeap` once — plan §5.2) of
fixed records, indexed by a dense renderer slot:

```
// GPU-side node record (one per resident node) — 48 bytes
float3   aabb_min_local;     // octree-local space (int32 grid → float; see §7.3)
float3   aabb_max_local;
uint     point_count;
uint     payload_slot;       // arena slot → buffer offset (bindless)
uint     generation;         // must equal CPU-patched current generation (H-27)
float    spacing;            // s(level), octree-local units
uint     flags;
```

CPU work per frame = patch the records whose residency/generation changed since last
frame + update one camera uniform — **O(changed nodes), O(1) in total node count**,
which is the plan §5.2 claim made precise: steady camera ⇒ zero per-node CPU work; the
GPU kernel (one thread per resident node, ≤ ~10⁴ threads — trivial) does all per-node
math and encodes draws into the ICB via `executeCommandsInBuffer`.

### 7.3 Precision (state-plane magnitudes)

World coordinates in state-plane CRS (R-1.4) are ~10⁶–10⁷ ft — raw `float` positions
would jitter. Contract: node payloads stay **int32 octree-local**; the vertex path
reconstructs `local = float(q) * scale_local`; the camera-relative transform
(octree-origin → eye) is computed **per frame in double on the CPU** and uploaded as a
single float 4×4. No double math on the GPU; no world-space floats ever materialized.

### 7.4 SSE definition (computed twice, one formula)

Projected spacing in pixels: `sse(node) = s(level) · h / (2 · tan(fovy/2) · d)` with `h` =
viewport height (px) and `d` = camera distance to the node's AABB (clamped to near
plane). Node is *renderable* when its **parent's** sse exceeds `pixel_threshold`
(default τ = 1.4 px, tunable) — i.e., the parent alone is too coarse.

- **GPU** evaluates it per resident node every frame for draw selection (§7.2).
- **CPU** evaluates the identical formula (in double) inside `select()` over the
  ~10⁴–10⁵-record resident+frontier hierarchy — microseconds — on camera-moved events
  (throttled, not per-frame) to produce `renderable`/`wanted`; `wanted` drives async
  loads. Rendering is not a measurement path, so C-4.6 determinism does not bind frame
  composition; nothing rendered feeds back into measurement (§1.4).

### 7.5 Lifetime handshake (H-27, renderer side)

Frame N's encoder samples generation values patched before encoding; the octree cache
defers page reuse for anything referenced by frames whose `MTLSharedEvent` fence has not
signaled (§4.3). The Phase 4 interop spike's realloc-under-render test (plan H-27,
ADR-0025) is a prerequisite the Phase 8 integration test re-runs against this cache
(§8.2).

## 8. Testing & validation plan

Per req R-9 quality gates (`docs/architecture/REQUIREMENTS.md` §R-9) and plan §3.6/§3.8.

1. **Determinism (§2.3 contract):** build the same synthetic 10M-pt cloud (a) twice,
   (b) with 1 vs 8 worker threads, (c) on macOS arm64 and Linux CI — assert byte-identical
   SHA-256 of `hierarchy.bin` and `points.bin` in all cases. Bit-identical cross-platform
   is intentionally stronger than H-22's floor and is achievable because structural
   decisions are integer-only.
2. **Eviction under load + H-27:** budget 64 MiB, randomized camera walk over a 100M-pt
   fixture; assert (a) resident bytes never exceed budget, (b) every stale access returns
   `StaleHandle` (never UB — ASan lane must stay green), (c) zombie pages are not reused
   before their fence signals (mock fence), (d) pin leaks trip `KERNEL_DEBUG_ASSERT`.
   Runs in the asan-ubsan preset; added to the TSan lane when the loader goes
   multi-threaded (plan §3.6: TSan "required once threaded point-cloud pipelines exist").
3. **Fuzz targets (plan §3.8 "every parser"):** (a) `hierarchy.bin` + `metadata.json`
   reader — libFuzzer, structured via `FuzzedDataProvider` (malformed offsets, overlapping
   payload ranges, child-mask/first-child contradictions, version skew); (b) payload
   checksum path (truncated/torn nodes). LAS/LAZ fuzzing is Phase 7's reader obligation,
   distinct from these. Sidecar bytes are untrusted input even though they normally come
   from our builder (sync/copy corruption, hostile containers).
4. **Sampling-quality regression:** per-node grid occupancy bounds (≤ 1 pt/cell), additive
   completeness (union of all node payloads == input multiset, checked via sorted-hash at
   10M scale), spacing property (min pairwise distance within a node ≥ ~s(l)·(1−ε) on
   random probes).
5. **Perf baselines to record (not gates):** build wall-time + peak RSS + peak disk at
   1M/10M/100M (and 1B nightly, Linux lane); node `acquire` latency cold/warm; `select()`
   latency at 10⁵ nodes; renderer integration: CPU ms/frame at steady camera (must be
   flat in node count — the §7.2 claim), draw-selection kernel time. Baselines land with
   Phase 7 exit per the plan's Phase 6 convention ("perf benchmarks recorded as
   baseline").
6. **Audit-boundary test:** ingest records exactly one chain event; event payload's
   SHA-256 matches recomputed `metadata.json` hash; rebuilding appends a new event and
   never mutates the prior one (C-1.2).

## 9. Open questions & proposed ADR

**Proposed ADR-0028 — "Out-of-core point-cloud octree: additive sampling, single-blob
sidecar, arena residency" (status: proposed).** Next free number per
`docs/adr/README.md` (index ends at ADR-0027). It should ratify the four load-bearing
decisions: additive Potree-style structure with integer-deterministic sampling (§2),
single-blob 16 KiB-aligned sidecar + non-cryptographic per-node checksums anchored by one
audit event (§3), arena/LRU residency with generation-stamped handles under H-27 (§4),
and the nanoflann delegation boundary (§6.2). This spec proposes; the ADR (owner-approved)
disposes — consistent with C-5.1 (implementation hold) and the no-new-ADR scope of this
task.

Open questions for the owner (none block review of this design):

- **OQ-1 (module home / plan placement) — RESOLVED 2026-06-12 by ADR-0028:** plan §4.2
  lists the octree under "Core algorithms (surface_core)" while ARCHITECTURE.md §4
  assigns point-cloud handling to `pointcloud_core` (a skeleton today). This spec
  proposed `libs/pointcloud_core` as the implementation home, reading the plan's
  placement as Phase-6 sequencing, not module assignment.
  [ADR-0028](../../adr/ADR-0028-out-of-core-pointcloud-octree.md) ruled exactly that
  (owner-delegated): module home is `libs/pointcloud_core`; the plan §4.2 listing is
  superseded on this point.
- **OQ-2 (compression codec):** deferred (see §10). v1 is uncompressed to keep the
  pread→bytesNoCopy path copy-free; a per-node LZ4/LAZ-chunk codec would force a
  decompress-into-arena step (still one copy — acceptable, but a different perf
  contract). Decide after Phase 7 baselines quantify the disk-size pressure.
- **OQ-3 (classification edits):** survey workflows reclassify points (ground/veg). The
  immutable structure suggests an attribute **overlay sidecar** (NodeId → classification
  array delta) rather than rebuilds; format deferred to a Phase 7 spec.
- **OQ-4 (build constants):** `G = 128`, `maxNodePoints = 20,000`, τ = 1.4 px, default
  budget 2 GiB are engineering defaults; final values fixed against Phase 7/8 baselines
  and recorded in `metadata.json` per build (so the format does not hard-code them).
- **OQ-5 (mmap-direct revisit):** §4.5 rejection holds for v1; revisit with Phase 8
  profiling data.
- **Doc inconsistencies noticed while grounding this spec (flagged, not fixed, per
  scope):**
  (a) plan §0's condensed C-5.5 row says the top level is "exactly `apps/ libs/ research/
  docs/ tools/`" but `docs/architecture/CONSTRAINTS.md` C-5.5 was amended by owner ruling
  2026-06-11 to include `third_party/` — the plan's table predates the ruling;
  (b) the req-R-9 vs risk-R-9 ID collision the plan §0 already disambiguates also bites
  this spec — herein "R-7.3"/"req R-9" are REQUIREMENTS.md sections and "risk R-9" is the
  register row.

## 10. Non-goals (explicit)

| Non-goal | Why deferred / excluded | Where it lands if ever |
|---|---|---|
| LAS/LAZ **writing** / export | Interop concern, not LOD-structure concern (R-8.1 formats live in the open Core but in format modules) | Phase 7 / `interop_core` boundary spec |
| Compression codec choice | OQ-2 — needs Phase 7 size/throughput baselines; v1 uncompressed preserves the zero-copy contract | ADR-0028 addendum |
| Incremental/modifiable octree (point insert/delete) | Rejected §2.1 — write-once evidence; rebuild is minutes at 100M (§5.4); determinism (C-4.6) favors rebuild | Reopen only if a workflow demands sub-rebuild-latency geometry edits |
| Classification-edit overlay format | OQ-3 — needs Phase 7 workflow detail | Phase 7 spec |
| Network streaming / multi-user sync of clouds | R-7.2 sync is deferred platform-wide (risk R-5; plan H-18); the sidecar + single-event hash anchor is sync-friendly by construction | Phase 10 sync spec |
| Vulkan backend specifics | RHI seam (C-4.4/R-3.4) keeps §7 backend-neutral; argument-buffer/ICB details are Metal-backend internals | Phase 8 RHI spec |
| Normals/eye-dome/derived render attributes | Renderer-side derivation; not part of the persistent structure | Phase 8 |
| GPL-adjacent octree libraries (PotreeConverter is free software with copyleft history; Entwine is Apache but server-shaped) | C-2.1 licensing guardrail + plan §4.1 discipline: algorithms are fair game, code is not vendored without a license row | n/a — in-house per ADR-0012 precedent |

## Sources

- `docs/architecture/REQUIREMENTS.md` — R-7.3 (verbatim §1.1), R-1.4, R-3.1/3.4/3.5,
  R-4.3/4.4, R-8.1, §R-9 quality gates
- `docs/architecture/RISK_REGISTER.md` — risk R-9 (octree named as 100M+ mitigation), risk R-5
- `docs/architecture/CONSTRAINTS.md` — C-1.2/1.3, C-2.1, C-4.1–4.6, C-5.1/5.3/5.4/5.5
- `docs/architecture/ARCHITECTURE.md` — §4 module map (`pointcloud_core`), §7 unified
  memory, §8 binary sidecars
- `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` — §3.2–3.4 (handles,
  expected, asserts), §3.6/3.8 (sanitizers, fuzzing), §4.2(5) (octree + nanoflann), §5
  (bytesNoCopy, argument buffers, ICB, SSE, compute rasterization), §8 H-22/H-27, Phases
  6.4/7/8
- `docs/superpowers/specs/2026-06-11-audit-core-storage-schema-spec.md` — chain event
  model the §3.4 boundary is defined against
- `docs/adr/README.md` (next free ADR = 0028) · `docs/adr/ADR-0012-custom-tin-engine.md` ·
  `docs/adr/ADR-0015-metal-first-rendering.md` · `docs/adr/ADR-0025-swift-cpp-interop-direct-c-abi.md`
- `research/auracad/numeric-policy.md` — FP rules backing §2.3/H-22 posture
- `libs/surface_core/include/ingeneer/surface/tin.h` — index-handle + integer-decision
  precedent · `libs/pointcloud_core/` — current skeleton state
- External (algorithms, not code): Schütz et al., *Fast Out-of-Core Construction of Potree
  LOD Structures* (2020); Schütz et al., *Rendering Point Clouds with Compute Shaders*
  (2021); Potree 2.0 format notes
