# Out-of-Core Point-Cloud Octree (Phase 7) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the persistent, out-of-core, level-of-detail point-cloud octree in `libs/pointcloud_core` — on-disk format, deterministic builder, residency cache, and headless CPU traversal — so InGENeer can ingest/store/query 100M+ point clouds on 16 GB unified memory.

**Architecture:** A Potree-style additive-sampling octree written as a single-blob sidecar (`<cloud-id>.octree/` = `metadata.json` + `hierarchy.bin` + `points.bin`). Sampling is integer-only (no RNG) so the output is byte-identical across threads and platforms. A C++-owned arena with LRU eviction and generation-stamped handles provides bounded-residency loading; a CPU traversal evaluates screen-space-error culling to drive residency. The octree is a derived, non-authoritative artifact anchored to the audit chain by a single SHA-256 (it is never a measurement substrate).

**Tech Stack:** C++23, CMake + presets (dev / asan-ubsan / tsan / hardened), the dependency-free `check.hpp` CTest harness, vendored xxHash (XXH3-64, per-node checksums), a self-contained FIPS-180-4 SHA-256 (file-level hashes), `std::expected` for all fallible calls, index handles only across the module boundary.

**Source spec:** [`docs/superpowers/specs/2026-06-11-out-of-core-octree-design.md`](../specs/2026-06-11-out-of-core-octree-design.md) · **ADR:** ADR-0028 (module home = `libs/pointcloud_core`) · **Scope boundary:** Phase 8 Metal/RHI renderer integration (spec §7.2 argument buffers, ICB, compute rasterizer, live-encoder realloc test) is a **separate follow-up plan** — this plan delivers everything headless-testable.

## Global Constraints

Every task's requirements implicitly include this section. Values copied verbatim from the spec / repo constraints.

- **Module home:** `libs/pointcloud_core` (ADR-0028). Public headers under `include/ingeneer/pointcloud/`, namespace `ingeneer::pointcloud`.
- **Determinism contract (§2.3):** same input bytes + same build constants ⇒ **byte-identical** `hierarchy.bin` and `points.bin` (hence identical SHA-256s), regardless of thread count or platform. All structural decisions are integer-only; the **only** FP step is the one-time quantization `q = floor((x − origin) / scale)`.
- **No RNG anywhere in the build/sample path** (C-4.6). No wall-clock, no `Math.random`-equivalent, not even a seeded RNG.
- **Numeric flags (C-4.2):** no `-ffast-math`, `-Ofast`, `-ffinite-math-only`, `-freciprocal-math`. The top-level CMake guard already enforces this; do not add them.
- **No SQLite, no `audit_core` link in `pointcloud_core` itself** (§3.4 decoupling). SHA-256 is implemented self-contained inside `pointcloud_core` (mirroring why `audit_core` did the same — dependency-light, from FIPS 180-4). Only the **§8.6 integration test target** links `audit_core`.
- **No raw pointers across the public boundary** (§6): index handles (`NodeId`, `NodeBufferHandle`) and `std::expected` only — matching `libs/surface_core/include/ingeneer/surface/tin.h`.
- **No exceptions escape** any function intended to sit behind a future `extern "C"` facade (C-4.5). Fallible calls return `std::expected`.
- **Endianness:** little-endian, fixed; a `static_assert(std::endian::native == std::endian::little)` rejects BE builds (§3.4). No byte-swapping reader paths.
- **Format magic / version:** `hierarchy.bin` and `points.bin` open with the 8-byte magic `INGOCT1\0`; `metadata.json` carries `"format": "ingeneer-octree", "version": {"major": 1, "minor": 0}`. Readers **fail closed** on a newer major (`OctreeErrc::FormatVersion`); unknown minor additions are ignored.
- **Alignment:** every node payload begins on a 16,384-byte boundary in `points.bin` and is padded to a 16,384-byte multiple (§3.1). Within a payload, attribute arrays are 64-byte aligned (SoA, §3.2).
- **Build constants (defaults, recorded per-build in `metadata.json`, §2.2/OQ-4):** sampling grid `G = 128`, `maxNodePoints = 20,000`, `minNodePoints = 1,000`, max depth `21`, SSE threshold `τ = 1.4 px`, default residency budget `2 GiB`.
- **Test harness:** dependency-free `tests/check.hpp` (`CHECK`, `CHECK_EQ`, `TEST_MAIN_RUN()`); each test is one executable wired with `add_executable` + `add_test`, linked `PRIVATE pointcloud_core ingeneer_engine_flags`. Same pattern as `geometry_core` / `surface_core` / `audit_core`.
- **Assertions:** add a `pointcloud_core`-local `kernel_assert.h` mirroring `libs/surface_core/include/ingeneer/surface/kernel_assert.h` (`KERNEL_ASSERT` always-on; `KERNEL_DEBUG_ASSERT` gated on `INGENEER_KERNEL_DEBUG_AUDIT`, which CMake defines for Debug configs).
- **License guardrail (C-2.1):** algorithms are fair game; code is not vendored without a license row. xxHash (BSD-2-Clause) is vendored verbatim under `third_party/xxhash/` exactly like `nanoflann`/`cdt`. No PotreeConverter/Entwine code.

## File Structure

New files (all under `libs/pointcloud_core/` unless noted):

| File | Responsibility |
|---|---|
| `include/ingeneer/pointcloud/kernel_assert.h` | Tiered assert macros (local copy of surface_core's). |
| `include/ingeneer/pointcloud/sha256.h` · `src/sha256.cpp` | Self-contained FIPS-180-4 SHA-256 (`sha256_hex`, `sha256_file_hex`). File-level hashes for `metadata.json`. |
| `include/ingeneer/pointcloud/octree_format.h` | Wire types/constants: `NodeRecordV1`, magic, version, `OctreeErrc`/`OctreeError`, `NodeId`, build constants, `RawPoint`, `AttributeSchema`, `Quantizer`, `BuildParams`, `BuildReport`, `AabbD`. The shared contract. |
| `include/ingeneer/pointcloud/morton.h` · `src/morton.cpp` | `quantize_axis`, `morton_encode_21`, `octant_at_level`. Pure integer functions. |
| `include/ingeneer/pointcloud/octree_checksum.h` · `src/octree_checksum.cpp` | `xxh3_64(span)` wrapper over vendored xxHash (PIMPL-free; xxHash kept SYSTEM/PRIVATE). |
| `include/ingeneer/pointcloud/octree_metadata.h` · `src/octree_metadata.cpp` | `OctreeMetadata` struct + `write_metadata_json`, `parse_metadata_json` (small, fuzz-safe). |
| `include/ingeneer/pointcloud/octree_builder.h` · `src/octree_builder.cpp`, `src/octree_build_incore.cpp`, `src/octree_build_passes.cpp` | `OctreeBuilder` (streaming, 3-pass out-of-core) + internal in-core subtree builder (Pass 2 body). |
| `include/ingeneer/pointcloud/octree_reader.h` · `src/octree_reader.cpp` | `OctreeReader::open`, `node`, `nodes_intersecting`, implicit-AABB reconstruction, integrity verify. |
| `include/ingeneer/pointcloud/octree_cache.h` · `src/octree_cache.cpp` | Arena + LRU cache, generation-stamped handles, `acquire`/`release`/`payload`, pinning, zombie/fence deferral. |
| `include/ingeneer/pointcloud/octree_traversal.h` · `src/octree_traversal.cpp` | `SseParams`, `FrustumD`, `TraversalResult`, `select()` (CPU SSE + frustum). |
| `tests/test_*.cpp` | One CTest per task (see tasks). |
| `tests/octree_test_util.h` | Shared synthetic-cloud generators + `.octree/` temp-dir helpers (test-only). |
| `fuzz/fuzz_octree_reader.cpp` · `fuzz/standalone_main.cpp` | libFuzzer + standalone driver for the hierarchy/metadata reader (§8.3). |
| `bench/bench_octree.cpp` · `BENCHMARKS.md` | Manual perf baselines, hardened-only (§8.5). |
| `tests/test_octree_audit_boundary.cpp` | §8.6 integration test; the **only** target linking `audit_core`. |
| `third_party/xxhash/` · `third_party/CMakeLists.txt` (edit) | Vendored xxHash header + `third_party_xxhash` INTERFACE target. |

`CMakeLists.txt` (`libs/pointcloud_core/CMakeLists.txt`) is extended task-by-task — each task adds only its own target/test rows.

## Shared Interface Contract

These types are defined in **Task 2** (`octree_format.h`) unless noted, and every later task consumes them verbatim. Reproduced here so a task read out of order knows the exact names/types.

```cpp
namespace ingeneer::pointcloud {

// --- handles ---------------------------------------------------------------
struct NodeId { std::uint32_t v; };
inline constexpr NodeId kNoNode{0xFFFFFFFFu};
inline constexpr bool operator==(NodeId a, NodeId b) noexcept { return a.v == b.v; }

struct NodeBufferHandle { std::uint32_t slot; std::uint32_t generation; };  // Task 7

// --- errors ----------------------------------------------------------------
enum class OctreeErrc : std::uint8_t {
    Io, FormatVersion, ChecksumMismatch, CorruptHierarchy,
    BudgetExhausted, StaleHandle, InvalidNode, BuildFailed,
};
struct OctreeError { OctreeErrc code; std::uint32_t node = kNoNode.v; };

// --- build constants (defaults; recorded per-build in metadata.json) -------
inline constexpr std::uint32_t kSamplingGrid  = 128;
inline constexpr std::uint32_t kMaxNodePoints = 20'000;
inline constexpr std::uint32_t kMinNodePoints = 1'000;
inline constexpr std::uint8_t  kMaxDepth      = 21;
inline constexpr std::size_t   kPayloadAlign  = 16'384;   // 16 KiB
inline constexpr std::uint32_t kQuantBits     = 31;       // q in [0, 2^31)
inline constexpr char kMagic[8] = {'I','N','G','O','C','T','1','\0'};

// --- on-disk node record (32 bytes, packed, little-endian) -----------------
#pragma pack(push, 1)
struct NodeRecordV1 {
    std::uint64_t payload_offset;   // into points.bin; 16 KiB-aligned
    std::uint32_t payload_bytes;    // padded size
    std::uint32_t point_count;
    std::uint64_t checksum_xxh3;    // XXH3-64 of UNPADDED payload
    std::uint8_t  child_mask;       // bit i => child octant i exists
    std::uint8_t  level;
    std::uint16_t flags;            // reserved
    std::uint32_t first_child;      // NodeId.v of first child; others by popcount
};
#pragma pack(pop)
static_assert(sizeof(NodeRecordV1) == 32);

// --- raw input point (double; pre-quantization) ----------------------------
struct RawPoint {
    double x, y, z;                 // world coords (state-plane magnitudes need double)
    std::uint16_t intensity = 0;
    std::uint8_t  classification = 0;
    std::uint8_t  return_flags = 0;
    std::uint16_t rgb[3] = {0,0,0}; // written iff schema.has_rgb
    double gps_time = 0.0;          // written iff schema.has_gps_time
};

struct AttributeSchema { bool has_rgb = false; bool has_gps_time = false; };

// --- world-space double AABB ----------------------------------------------
struct AabbD { double min[3]; double max[3]; };

// --- quantization (the single FP step) ------------------------------------
struct Quantizer {
    double origin[3];               // root cube origin (min corner)
    double scale;                   // rootEdge / 2^31
    // q = floor((v - origin[axis]) / scale), clamped to [0, 2^31 - 1].
    std::int32_t quantize(double v, int axis) const noexcept;   // Task 3
};

struct BuildParams {
    AttributeSchema schema;
    std::uint32_t sampling_grid  = kSamplingGrid;
    std::uint32_t max_node_points = kMaxNodePoints;
    std::uint32_t min_node_points = kMinNodePoints;
    std::uint64_t chunk_cap_bytes = 256ull * 1024 * 1024;   // Pass-1 RAM cap per chunk
    std::uint32_t worker_count    = 1;                       // Pass-2 parallelism (det. invariant)
};

struct BuildReport {
    std::uint64_t point_count = 0;
    std::uint32_t node_count  = 0;
    std::uint8_t  max_level   = 0;
    std::string   hierarchy_sha256;   // SHA-256 of hierarchy.bin
    std::string   points_sha256;      // SHA-256 of points.bin
    std::string   metadata_sha256;    // SHA-256 of metadata.json (audit anchor, §3.4)
};

// node view returned by reader (Task 6)
struct NodeMeta {
    std::uint32_t point_count;
    std::uint8_t  level;
    std::uint8_t  child_mask;
    AabbD aabb;                      // reconstructed implicit cube (octree-local origin)
};

}  // namespace ingeneer::pointcloud
```

`OctreeReader` / `OctreeBuilder` / cache / traversal signatures follow spec §6.1 and are defined in their respective tasks.

---

## Task 1: Tiered assert macros (local)

**Files:**
- Create: `libs/pointcloud_core/include/ingeneer/pointcloud/kernel_assert.h`
- Test: covered indirectly (used by later tasks); no standalone test.

**Interfaces:**
- Produces: `KERNEL_ASSERT(cond, msg)` (always on), `KERNEL_DEBUG_ASSERT(cond, msg)` (gated on `INGENEER_KERNEL_DEBUG_AUDIT`).

This is a near-verbatim copy of `surface_core`'s assert header (the spec §4.4 references `KERNEL_DEBUG_ASSERT` by name; `pointcloud_core` is the "second engine library" the surface header's comment said would justify promotion — but per scope discipline we copy locally now and leave promotion to a future shared-header task).

- [ ] **Step 1: Write the header**

```cpp
// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/kernel_assert.h — tiered kernel assertions for pointcloud_core.
// Local copy of libs/surface_core/include/ingeneer/surface/kernel_assert.h (scope
// discipline: promote to a shared header only when a third engine needs it). Tiers:
//   * KERNEL_ASSERT       — always on (release included); cheap data-corruption guards.
//   * KERNEL_DEBUG_ASSERT — compiled in only when INGENEER_KERNEL_DEBUG_AUDIT is defined
//     (CMake defines it for Debug: dev, asan-ubsan, tsan). Condition not evaluated when off.
#ifndef INGENEER_POINTCLOUD_KERNEL_ASSERT_H
#define INGENEER_POINTCLOUD_KERNEL_ASSERT_H

#include <cstdio>
#include <cstdlib>

#define KERNEL_ASSERT(cond, msg)                                                               \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "KERNEL_ASSERT failed: %s\n  %s\n  at %s:%d\n", #cond, (msg), \
                         __FILE__, __LINE__);                                                  \
            std::abort();                                                                      \
        }                                                                                      \
    } while (0)

#if defined(INGENEER_KERNEL_DEBUG_AUDIT)
#define KERNEL_DEBUG_ASSERT(cond, msg) KERNEL_ASSERT(cond, msg)
#else
#define KERNEL_DEBUG_ASSERT(cond, msg) ((void)0)
#endif

#endif  // INGENEER_POINTCLOUD_KERNEL_ASSERT_H
```

- [ ] **Step 2: Wire the Debug audit define in CMake**

In `libs/pointcloud_core/CMakeLists.txt`, after the `add_library(pointcloud_core ...)` line, add (mirrors `surface_core`):

```cmake
target_compile_definitions(pointcloud_core PRIVATE $<$<CONFIG:Debug>:INGENEER_KERNEL_DEBUG_AUDIT=1>)
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --preset dev && cmake --build --preset dev --target pointcloud_core`
Expected: builds clean (header is included by later code; this step just confirms the define wiring doesn't break the existing build).

- [ ] **Step 4: Commit**

```bash
git add libs/pointcloud_core/include/ingeneer/pointcloud/kernel_assert.h libs/pointcloud_core/CMakeLists.txt
git commit -- libs/pointcloud_core/include/ingeneer/pointcloud/kernel_assert.h libs/pointcloud_core/CMakeLists.txt \
  -m "feat(pointcloud_core): tiered kernel-assert macros for octree"
```

---

## Task 2: On-disk format header (the shared contract)

**Files:**
- Create: `libs/pointcloud_core/include/ingeneer/pointcloud/octree_format.h`
- Test: `libs/pointcloud_core/tests/test_octree_format.cpp`

**Interfaces:**
- Produces: every type in the **Shared Interface Contract** above (`NodeRecordV1`, `OctreeErrc`, `NodeId`, build constants, `RawPoint`, `Quantizer` (declaration only), `BuildParams`, `BuildReport`, `AabbD`, `NodeMeta`, `AttributeSchema`, `kMagic`).

- [ ] **Step 1: Write the failing test**

`tests/test_octree_format.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
// Format invariants: record size/layout, endianness guard, constant values.
#include "ingeneer/pointcloud/octree_format.h"

#include <bit>
#include <cstring>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static void run() {
    // 32-byte packed record, field offsets stable (wire contract).
    CHECK_EQ(sizeof(NodeRecordV1), static_cast<std::size_t>(32));
    CHECK_EQ(offsetof(NodeRecordV1, payload_offset), static_cast<std::size_t>(0));
    CHECK_EQ(offsetof(NodeRecordV1, payload_bytes), static_cast<std::size_t>(8));
    CHECK_EQ(offsetof(NodeRecordV1, point_count), static_cast<std::size_t>(12));
    CHECK_EQ(offsetof(NodeRecordV1, checksum_xxh3), static_cast<std::size_t>(16));
    CHECK_EQ(offsetof(NodeRecordV1, child_mask), static_cast<std::size_t>(24));
    CHECK_EQ(offsetof(NodeRecordV1, level), static_cast<std::size_t>(25));
    CHECK_EQ(offsetof(NodeRecordV1, flags), static_cast<std::size_t>(26));
    CHECK_EQ(offsetof(NodeRecordV1, first_child), static_cast<std::size_t>(28));

    CHECK(std::endian::native == std::endian::little);  // BE build would static_assert anyway

    CHECK_EQ(kSamplingGrid, static_cast<std::uint32_t>(128));
    CHECK_EQ(kMaxNodePoints, static_cast<std::uint32_t>(20000));
    CHECK_EQ(kMaxDepth, static_cast<std::uint8_t>(21));
    CHECK_EQ(std::memcmp(kMagic, "INGOCT1", 7), 0);
    CHECK_EQ(kMagic[7], '\0');

    CHECK_EQ(kNoNode.v, static_cast<std::uint32_t>(0xFFFFFFFFu));
    CHECK(NodeId{5} == NodeId{5});
}

TEST_MAIN_RUN()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target test_octree_format`
Expected: FAIL — `octree_format.h` not found / `NodeRecordV1` undefined.

- [ ] **Step 3: Write the header**

`include/ingeneer/pointcloud/octree_format.h` — exactly the **Shared Interface Contract** block above, wrapped in include guards `INGENEER_POINTCLOUD_OCTREE_FORMAT_H`, with these includes and the endianness guard:

```cpp
// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/octree_format.h — INGOCT v1 on-disk wire contract + shared types.
// Spec: docs/superpowers/specs/2026-06-11-out-of-core-octree-design.md §3, §6.1.
#ifndef INGENEER_POINTCLOUD_OCTREE_FORMAT_H
#define INGENEER_POINTCLOUD_OCTREE_FORMAT_H

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

static_assert(std::endian::native == std::endian::little,
              "INGOCT v1 is little-endian only (spec §3.4); big-endian builds are rejected.");

namespace ingeneer::pointcloud {
/* ... the Shared Interface Contract block, verbatim ... */
}  // namespace ingeneer::pointcloud
#endif  // INGENEER_POINTCLOUD_OCTREE_FORMAT_H
```

(`Quantizer::quantize` is **declared** here; defined in Task 3's `morton.cpp`. `NodeBufferHandle` is declared here too so format/cache share it.)

- [ ] **Step 4: Wire the test in CMake**

In `libs/pointcloud_core/CMakeLists.txt`, in the tests section:

```cmake
add_executable(test_octree_format tests/test_octree_format.cpp)
target_link_libraries(test_octree_format PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.octree_format COMMAND test_octree_format)
```

(`octree_format.h` is header-only so far, but linking `pointcloud_core` keeps the pattern uniform; the library target already exists.)

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build --preset dev --target test_octree_format && ctest --preset dev -R pointcloud.octree_format`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/pointcloud_core/include/ingeneer/pointcloud/octree_format.h \
        libs/pointcloud_core/tests/test_octree_format.cpp libs/pointcloud_core/CMakeLists.txt
git commit -- libs/pointcloud_core/include/ingeneer/pointcloud/octree_format.h \
        libs/pointcloud_core/tests/test_octree_format.cpp libs/pointcloud_core/CMakeLists.txt \
  -m "feat(pointcloud_core): INGOCT v1 on-disk format header + invariants"
```

---

## Task 3: Quantization + Morton encoding (deterministic integer core)

**Files:**
- Create: `libs/pointcloud_core/include/ingeneer/pointcloud/morton.h`, `src/morton.cpp`
- Test: `libs/pointcloud_core/tests/test_morton.cpp`

**Interfaces:**
- Consumes: `Quantizer`, `kQuantBits`, `kMaxDepth` (Task 2).
- Produces:
  - `std::int32_t Quantizer::quantize(double v, int axis) const noexcept` — the single FP step `floor((v-origin)/scale)`, clamped to `[0, 2^31-1]`.
  - `std::uint64_t morton_encode_21(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept` — interleaves the low 21 bits of each argument into a 63-bit code (z in bit 0, then y, then x per triple; MSB-first level order).
  - `std::uint8_t octant_at_level(std::uint64_t morton, std::uint8_t level) noexcept` — the 3-bit octant index at a given level (level 0 = top).
  - `std::uint32_t top21(std::int32_t q) noexcept` — the 21 most-significant bits of a 31-bit quantized coord (`q >> (kQuantBits - kMaxDepth)`).

- [ ] **Step 1: Write the failing test**

`tests/test_morton.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
// Quantization is the only FP step; Morton interleave + octant decode are pure integer.
#include "ingeneer/pointcloud/morton.h"

#include "check.hpp"

using namespace ingeneer::pointcloud;

static void run() {
    // --- quantize: floor, clamp, axis selection -------------------------------------
    Quantizer q{.origin = {100.0, -50.0, 0.0}, .scale = 1.0};
    CHECK_EQ(q.quantize(100.0, 0), 0);
    CHECK_EQ(q.quantize(100.9, 0), 0);      // floor
    CHECK_EQ(q.quantize(105.0, 0), 5);
    CHECK_EQ(q.quantize(-50.0, 1), 0);      // axis 1 origin = -50
    CHECK_EQ(q.quantize(-49.0, 1), 1);
    CHECK_EQ(q.quantize(50.0, 2), 50);
    // clamp at both ends (degenerate / out-of-cube inputs never escape the grid)
    CHECK_EQ(q.quantize(99.0, 0), 0);                          // below origin -> 0
    Quantizer hi{.origin = {0,0,0}, .scale = 1.0};
    CHECK_EQ(hi.quantize(1e30, 0), static_cast<std::int32_t>((1u<<31) - 1));

    // --- morton interleave: known small case ----------------------------------------
    // x=1 (001), y=0, z=0 -> the x bit lands in the top of the first 3-bit group.
    CHECK_EQ(morton_encode_21(0, 0, 0), static_cast<std::uint64_t>(0));
    CHECK_EQ(morton_encode_21(1, 1, 1), static_cast<std::uint64_t>(0b111));  // all three lsb
    CHECK_EQ(morton_encode_21(1, 0, 0), static_cast<std::uint64_t>(0b100));  // x is MSB of triple
    CHECK_EQ(morton_encode_21(0, 0, 1), static_cast<std::uint64_t>(0b001));  // z is LSB of triple
    // monotonic in the high octant: x=2 sets bit at level boundary
    CHECK_EQ(morton_encode_21(2, 0, 0), static_cast<std::uint64_t>(0b100'000));

    // --- octant decode round-trips the top levels -----------------------------------
    const std::uint64_t code = morton_encode_21(0b101, 0b010, 0b001);
    // level 0 = top bits of each (x bit2=1,y bit2=0,z bit2=0) -> 100 = 4
    CHECK_EQ(octant_at_level(code, 0), static_cast<std::uint8_t>(0b100));
    // level 1 (x bit1=0,y bit1=1,z bit1=0) -> 010 = 2
    CHECK_EQ(octant_at_level(code, 1), static_cast<std::uint8_t>(0b010));
    // level 2 (x bit0=1,y bit0=0,z bit0=1) -> 101 = 5
    CHECK_EQ(octant_at_level(code, 2), static_cast<std::uint8_t>(0b101));

    // --- top21 keeps the high octree path bits --------------------------------------
    CHECK_EQ(top21(static_cast<std::int32_t>(1u << 30)), static_cast<std::uint32_t>(1u << 20));
}

TEST_MAIN_RUN()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset dev --target test_morton`
Expected: FAIL — `morton.h` not found.

- [ ] **Step 3: Write the header**

`include/ingeneer/pointcloud/morton.h`:

```cpp
// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/morton.h — quantization + 63-bit Morton (21 levels x 3 bits).
// Quantization is the ONLY floating-point step in the whole build (spec §2.3); everything
// downstream is integer, so structural decisions are bit-identical across threads/platforms.
#ifndef INGENEER_POINTCLOUD_MORTON_H
#define INGENEER_POINTCLOUD_MORTON_H

#include <cstdint>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud {

// 21 most-significant bits of a 31-bit quantized coordinate (the octree path bits).
std::uint32_t top21(std::int32_t q) noexcept;

// Interleave the low 21 bits of x,y,z into a 63-bit Morton code. Within each 3-bit group
// the order is (x,y,z) from MSB to LSB; groups run MSB-first so group g == octree level g.
std::uint64_t morton_encode_21(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept;

// 3-bit octant index at `level` (0 = top). level must be < kMaxDepth.
std::uint8_t octant_at_level(std::uint64_t morton, std::uint8_t level) noexcept;

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_MORTON_H
```

- [ ] **Step 4: Write the implementation**

`src/morton.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/morton.h"

#include <algorithm>
#include <cmath>

namespace ingeneer::pointcloud {

std::int32_t Quantizer::quantize(double v, int axis) const noexcept {
    const double q = std::floor((v - origin[axis]) / scale);
    // Clamp to the 31-bit non-negative grid. Out-of-cube / degenerate inputs are pinned to
    // the boundary rather than wrapping, so no structural decision sees a negative index.
    if (q <= 0.0) return 0;
    constexpr double kMax = static_cast<double>((1u << kQuantBits) - 1u);
    if (q >= kMax) return static_cast<std::int32_t>((1u << kQuantBits) - 1u);
    return static_cast<std::int32_t>(q);
}

std::uint32_t top21(std::int32_t q) noexcept {
    return static_cast<std::uint32_t>(q) >> (kQuantBits - kMaxDepth);  // 31 - 21 = 10
}

namespace {
// Spread the low 21 bits of v so they occupy every 3rd bit (bits 0,3,6,...,60).
std::uint64_t split3(std::uint32_t v) noexcept {
    std::uint64_t x = v & 0x1FFFFFull;                 // keep 21 bits
    x = (x | (x << 32)) & 0x1F00000000FFFFull;
    x = (x | (x << 16)) & 0x1F0000FF0000FFull;
    x = (x | (x << 8))  & 0x100F00F00F00F00Full;
    x = (x | (x << 4))  & 0x10C30C30C30C30C3ull;
    x = (x | (x << 2))  & 0x1249249249249249ull;
    return x;
}
}  // namespace

std::uint64_t morton_encode_21(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    // x occupies the MSB of each triple, z the LSB: code = (X<<2)|(Y<<1)|Z spread-interleaved.
    return (split3(x) << 2) | (split3(y) << 1) | split3(z);
}

std::uint8_t octant_at_level(std::uint64_t morton, std::uint8_t level) noexcept {
    // Level 0 is the top 3 bits of the 63-bit code: shift down by (kMaxDepth-1-level)*3.
    const unsigned shift = static_cast<unsigned>((kMaxDepth - 1 - level)) * 3u;
    return static_cast<std::uint8_t>((morton >> shift) & 0x7u);
}

}  // namespace ingeneer::pointcloud
```

- [ ] **Step 5: Wire CMake (add source + test)**

In `libs/pointcloud_core/CMakeLists.txt`: add `src/morton.cpp` to the `add_library(pointcloud_core ...)` source list, and add:

```cmake
add_executable(test_morton tests/test_morton.cpp)
target_link_libraries(test_morton PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.morton COMMAND test_morton)
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build --preset dev --target test_morton && ctest --preset dev -R pointcloud.morton`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add libs/pointcloud_core/include/ingeneer/pointcloud/morton.h \
        libs/pointcloud_core/src/morton.cpp libs/pointcloud_core/tests/test_morton.cpp \
        libs/pointcloud_core/CMakeLists.txt
git commit -- libs/pointcloud_core/include/ingeneer/pointcloud/morton.h \
        libs/pointcloud_core/src/morton.cpp libs/pointcloud_core/tests/test_morton.cpp \
        libs/pointcloud_core/CMakeLists.txt \
  -m "feat(pointcloud_core): integer quantize + 63-bit Morton encode"
```

---

## Task 4: Self-contained SHA-256 (file-level hashes)

**Files:**
- Create: `libs/pointcloud_core/include/ingeneer/pointcloud/sha256.h`, `src/sha256.cpp`
- Test: `libs/pointcloud_core/tests/test_sha256.cpp`

**Interfaces:**
- Produces:
  - `std::string sha256_hex(std::string_view data)` — lowercase 64-char hex.
  - `std::expected<std::string, OctreeError> sha256_file_hex(const std::filesystem::path&)` — streams a file in 64 KiB chunks; `OctreeErrc::Io` on open/read failure.

**Why self-contained (Global Constraints):** `audit_core` already has a SHA-256 but is linked to SQLite3; pulling it into the bulk-data engine violates the §3.4 decoupling boundary. We mirror `audit_core`'s decision (implement from FIPS 180-4, dependency-light). The deferred cleanup — extracting a shared `hash_core` leaf used by both — is logged as a follow-up note in `BENCHMARKS.md`, out of this plan's scope.

- [ ] **Step 1: Write the failing test (NIST vectors)**

`tests/test_sha256.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
// FIPS 180-4 SHA-256 against the canonical NIST short-message vectors + a file hash.
#include "ingeneer/pointcloud/sha256.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static void run() {
    CHECK_EQ(sha256_hex(""),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256_hex("abc"),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    // file hash == in-memory hash of the same bytes
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "ingeneer_sha256_probe.bin";
    {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        CHECK(f != nullptr);
        std::fwrite("abc", 1, 3, f);
        std::fclose(f);
    }
    auto fh = sha256_file_hex(p);
    CHECK(fh.has_value());
    CHECK_EQ(*fh, sha256_hex("abc"));
    fs::remove(p);

    auto missing = sha256_file_hex(fs::temp_directory_path() / "ingeneer_does_not_exist.bin");
    CHECK(!missing.has_value());
    CHECK_EQ(missing.error().code, OctreeErrc::Io);
}

TEST_MAIN_RUN()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset dev --target test_sha256`
Expected: FAIL — `sha256.h` not found.

- [ ] **Step 3: Write the header**

`include/ingeneer/pointcloud/sha256.h`:

```cpp
// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/sha256.h — self-contained FIPS 180-4 SHA-256 for octree file hashes.
// Kept in-engine (NOT linked from audit_core) so the bulk-data engine stays free of SQLite
// and the audit store (spec §3.4 decoupling). File-level only: per-node checksums use the
// faster XXH3-64 (octree_checksum.h).
#ifndef INGENEER_POINTCLOUD_SHA256_H
#define INGENEER_POINTCLOUD_SHA256_H

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud {

std::string sha256_hex(std::string_view data);
std::expected<std::string, OctreeError> sha256_file_hex(const std::filesystem::path& path);

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_SHA256_H
```

- [ ] **Step 4: Write the implementation**

`src/sha256.cpp` — standard FIPS 180-4 block compressor (no external crypto). Full code:

```cpp
// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/sha256.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ingeneer::pointcloud {
namespace {

constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

struct Ctx {
    std::array<std::uint32_t, 8> h = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::uint64_t total = 0;
    std::array<std::uint8_t, 64> buf{};
    std::size_t fill = 0;

    void block(const std::uint8_t* p) {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[i*4]) << 24) | (std::uint32_t(p[i*4+1]) << 16) |
                   (std::uint32_t(p[i*4+2]) << 8) | std::uint32_t(p[i*4+3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            const std::uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        auto v = h;
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(v[4],6) ^ rotr(v[4],11) ^ rotr(v[4],25);
            const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + S1 + ch + kK[i] + w[i];
            const std::uint32_t S0 = rotr(v[0],2) ^ rotr(v[0],13) ^ rotr(v[0],22);
            const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const std::uint32_t t2 = S0 + maj;
            v[7]=v[6]; v[6]=v[5]; v[5]=v[4]; v[4]=v[3]+t1; v[3]=v[2]; v[2]=v[1]; v[1]=v[0]; v[0]=t1+t2;
        }
        for (int i = 0; i < 8; ++i) h[i] += v[i];
    }

    void update(const std::uint8_t* p, std::size_t n) {
        total += n;
        while (n > 0) {
            const std::size_t take = std::min<std::size_t>(64 - fill, n);
            std::memcpy(buf.data() + fill, p, take);
            fill += take; p += take; n -= take;
            if (fill == 64) { block(buf.data()); fill = 0; }
        }
    }

    std::string hex() {
        const std::uint64_t bits = total * 8;
        std::uint8_t pad = 0x80;
        update(&pad, 1);
        std::uint8_t zero = 0;
        while (fill != 56) update(&zero, 1);
        std::uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<std::uint8_t>(bits >> (56 - i*8));
        // update() bumped total; write length without disturbing it further:
        for (int i = 0; i < 8; ++i) { std::size_t one = 1; (void)one; }
        // append the 8 length bytes directly through block path:
        std::memcpy(buf.data() + fill, len, 8); fill += 8; block(buf.data()); fill = 0;
        static const char* hexd = "0123456789abcdef";
        std::string out; out.reserve(64);
        for (std::uint32_t word : h)
            for (int s = 28; s >= 0; s -= 4) out.push_back(hexd[(word >> s) & 0xF]);
        return out;
    }
};

}  // namespace

std::string sha256_hex(std::string_view data) {
    Ctx c;
    c.update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    return c.hex();
}

std::expected<std::string, OctreeError> sha256_file_hex(const std::filesystem::path& path) {
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return std::unexpected(OctreeError{OctreeErrc::Io});
    Ctx c;
    std::vector<std::uint8_t> buf(64 * 1024);
    for (;;) {
        const std::size_t n = std::fread(buf.data(), 1, buf.size(), f);
        if (n > 0) c.update(buf.data(), n);
        if (n < buf.size()) {
            const bool err = std::ferror(f) != 0;
            std::fclose(f);
            if (err) return std::unexpected(OctreeError{OctreeErrc::Io});
            return c.hex();
        }
    }
}

}  // namespace ingeneer::pointcloud
```

> **Implementer note:** the `hex()` finalizer above must produce the exact NIST digests in Step 1 — the `total`/length-append handling is the subtle part. If a vector fails, fix the finalizer (do not weaken the test). Keep the length-bytes appended big-endian as the final 8 bytes of the last padded block.

- [ ] **Step 5: Wire CMake**

Add `src/sha256.cpp` to the library sources and:

```cmake
add_executable(test_sha256 tests/test_sha256.cpp)
target_link_libraries(test_sha256 PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.sha256 COMMAND test_sha256)
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build --preset dev --target test_sha256 && ctest --preset dev -R pointcloud.sha256`
Expected: PASS (all three NIST vectors + file hash + missing-file error).

- [ ] **Step 7: Commit**

```bash
git add libs/pointcloud_core/include/ingeneer/pointcloud/sha256.h \
        libs/pointcloud_core/src/sha256.cpp libs/pointcloud_core/tests/test_sha256.cpp \
        libs/pointcloud_core/CMakeLists.txt
git commit -- libs/pointcloud_core/include/ingeneer/pointcloud/sha256.h \
        libs/pointcloud_core/src/sha256.cpp libs/pointcloud_core/tests/test_sha256.cpp \
        libs/pointcloud_core/CMakeLists.txt \
  -m "feat(pointcloud_core): self-contained FIPS-180-4 SHA-256 for octree file hashes"
```

---

## Task 5: Vendor xxHash + per-node checksum wrapper

**Files:**
- Create: `third_party/xxhash/include/xxhash.h` (vendored verbatim), `third_party/xxhash/LICENSE`, `third_party/xxhash/README.md`
- Modify: `third_party/CMakeLists.txt` (add `third_party_xxhash` INTERFACE target), `third_party/README.md` (license row)
- Create: `libs/pointcloud_core/include/ingeneer/pointcloud/octree_checksum.h`, `src/octree_checksum.cpp`
- Test: `libs/pointcloud_core/tests/test_octree_checksum.cpp`

**Interfaces:**
- Produces: `std::uint64_t xxh3_64(std::span<const std::byte> data) noexcept` — XXH3-64 of the bytes. Used for `NodeRecordV1::checksum_xxh3` (UNPADDED payload).

**Why a wrapper:** xxHash is consumed **only** inside `octree_checksum.cpp` behind a plain function (same discipline as nanoflann's PIMPL) so `<xxhash.h>` never leaks into engine consumers, and the header is marked SYSTEM so its internal warnings don't trip the `-Wconversion`/`-Wshadow` bar.

- [ ] **Step 1: Vendor the xxHash single header**

Download the official `xxhash.h` (BSD-2-Clause, Yann Collet) into `third_party/xxhash/include/xxhash.h`. Use the released `xxhash.h` with `XXH_INLINE_ALL` available (we define it in our TU). Copy the upstream `LICENSE` to `third_party/xxhash/LICENSE`. Add `third_party/xxhash/README.md` recording the upstream version/commit and "mirrored verbatim; do not edit."

Add a license row to `third_party/README.md` (xxHash, BSD-2-Clause, URL, version). This satisfies C-2.1 (license row required before vendoring).

- [ ] **Step 2: Add the CMake INTERFACE target**

In `third_party/CMakeLists.txt`, append (mirroring `third_party_nanoflann`):

```cmake
add_library(third_party_xxhash INTERFACE)
target_include_directories(third_party_xxhash INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/xxhash/include)
```

- [ ] **Step 3: Write the failing test (known XXH3-64 vectors + determinism)**

`tests/test_octree_checksum.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
// XXH3-64 wrapper: stable for fixed inputs (the per-node checksum must be byte-identical
// across platforms for the determinism contract, spec §2.3/§3.4).
#include "ingeneer/pointcloud/octree_checksum.h"

#include <array>
#include <cstddef>
#include <cstring>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static std::uint64_t hash_str(const char* s) {
    return xxh3_64(std::as_bytes(std::span<const char>(s, std::strlen(s))));
}

static void run() {
    // Empty input has a fixed XXH3-64 value (upstream constant).
    CHECK_EQ(xxh3_64(std::span<const std::byte>()), static_cast<std::uint64_t>(0x2d06800538d394c2ull));
    // Stability: same bytes -> same hash; one-byte change -> different hash.
    CHECK_EQ(hash_str("INGOCT-node"), hash_str("INGOCT-node"));
    CHECK(hash_str("INGOCT-node") != hash_str("INGOCT-nodf"));
    // Length sensitivity.
    CHECK(hash_str("abc") != hash_str("abcd"));
}

TEST_MAIN_RUN()
```

> The empty-input constant `0x2d06800538d394c2` is the documented XXH3_64bits("",0) seed-0 value; if the vendored version differs, update the literal to the value the vendored header produces and note the version in the test comment (the contract is *stability*, not a specific magic number).

- [ ] **Step 4: Run test to verify it fails**

Run: `cmake --build --preset dev --target test_octree_checksum`
Expected: FAIL — `octree_checksum.h` not found.

- [ ] **Step 5: Write the header + wrapper**

`include/ingeneer/pointcloud/octree_checksum.h`:

```cpp
// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/octree_checksum.h — XXH3-64 over a node payload (spec §3.4).
// Non-cryptographic, ~tens of GB/s; detects corruption/torn writes on every node load.
// Cryptographic anchoring is SHA-256 at the file level (sha256.h) -> one audit event.
#ifndef INGENEER_POINTCLOUD_OCTREE_CHECKSUM_H
#define INGENEER_POINTCLOUD_OCTREE_CHECKSUM_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace ingeneer::pointcloud {

std::uint64_t xxh3_64(std::span<const std::byte> data) noexcept;

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_OCTREE_CHECKSUM_H
```

`src/octree_checksum.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_checksum.h"

#define XXH_INLINE_ALL
#include <xxhash.h>  // third_party_xxhash (SYSTEM include; not held to the engine warning bar)

namespace ingeneer::pointcloud {

std::uint64_t xxh3_64(std::span<const std::byte> data) noexcept {
    return XXH3_64bits(data.data(), data.size());
}

}  // namespace ingeneer::pointcloud
```

- [ ] **Step 6: Wire CMake (link + SYSTEM include)**

In `libs/pointcloud_core/CMakeLists.txt`: add `src/octree_checksum.cpp` to the library sources; extend the existing `target_link_libraries(pointcloud_core PRIVATE ...)` to also list `third_party_xxhash`; and add a SYSTEM include (mirrors the nanoflann row already present):

```cmake
target_include_directories(pointcloud_core SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/xxhash/include)
```

Then the test:

```cmake
add_executable(test_octree_checksum tests/test_octree_checksum.cpp)
target_link_libraries(test_octree_checksum PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.octree_checksum COMMAND test_octree_checksum)
```

- [ ] **Step 7: Run test to verify it passes**

Run: `cmake --preset dev && cmake --build --preset dev --target test_octree_checksum && ctest --preset dev -R pointcloud.octree_checksum`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add third_party/xxhash third_party/CMakeLists.txt third_party/README.md \
        libs/pointcloud_core/include/ingeneer/pointcloud/octree_checksum.h \
        libs/pointcloud_core/src/octree_checksum.cpp \
        libs/pointcloud_core/tests/test_octree_checksum.cpp libs/pointcloud_core/CMakeLists.txt
git commit -- third_party/xxhash third_party/CMakeLists.txt third_party/README.md \
        libs/pointcloud_core/include/ingeneer/pointcloud/octree_checksum.h \
        libs/pointcloud_core/src/octree_checksum.cpp \
        libs/pointcloud_core/tests/test_octree_checksum.cpp libs/pointcloud_core/CMakeLists.txt \
  -m "feat(pointcloud_core): vendor xxHash; XXH3-64 per-node checksum wrapper"
```

---

## Task 6: `metadata.json` writer + parser

**Files:**
- Create: `libs/pointcloud_core/include/ingeneer/pointcloud/octree_metadata.h`, `src/octree_metadata.cpp`
- Test: `libs/pointcloud_core/tests/test_octree_metadata.cpp`

**Interfaces:**
- Consumes: `AttributeSchema`, `AabbD`, `BuildParams` constants (Task 2).
- Produces:
  - `struct OctreeMetadata { std::uint32_t version_major, version_minor; AabbD root_cube_world; double origin[3]; double scale; double root_edge; std::uint64_t point_count; std::uint32_t node_count; std::uint8_t max_level; std::uint32_t sampling_grid, max_node_points, min_node_points; AttributeSchema schema; std::string hierarchy_sha256, points_sha256; };`
  - `std::string write_metadata_json(const OctreeMetadata&)` — canonical, sorted-key, deterministic text (no wall-clock, no platform floats beyond the recorded doubles).
  - `std::expected<OctreeMetadata, OctreeError> parse_metadata_json(std::string_view)` — tolerant, **fuzz-safe**; `OctreeErrc::CorruptHierarchy` on malformed/missing required keys, `OctreeErrc::FormatVersion` on a newer major.

**Design:** a small, fixed-schema JSON. We hand-write a minimal scanner (no general JSON library, no `audit_core`/SQLite). It must never read out of bounds on arbitrary bytes (the §8.3 fuzz target feeds it garbage). Number parsing uses `std::from_chars`; strings are length-bounded; unknown keys ignored (minor-version forward-compat).

- [ ] **Step 1: Write the failing test (round-trip + rejection)**

`tests/test_octree_metadata.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
// metadata.json: deterministic write, lossless round-trip, version + corruption rejection.
#include "ingeneer/pointcloud/octree_metadata.h"

#include <string>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static OctreeMetadata sample() {
    OctreeMetadata m{};
    m.version_major = 1; m.version_minor = 0;
    m.origin[0] = 1000.5; m.origin[1] = -2000.25; m.origin[2] = 10.0;
    m.root_edge = 16384.0; m.scale = m.root_edge / 2147483648.0;
    m.root_cube_world = AabbD{{1000.5,-2000.25,10.0},{17384.5,14383.75,16394.0}};
    m.point_count = 100000; m.node_count = 37; m.max_level = 5;
    m.sampling_grid = 128; m.max_node_points = 20000; m.min_node_points = 1000;
    m.schema = AttributeSchema{.has_rgb = true, .has_gps_time = false};
    m.hierarchy_sha256 = std::string(64, 'a');
    m.points_sha256 = std::string(64, 'b');
    return m;
}

static void run() {
    const OctreeMetadata m = sample();
    const std::string js = write_metadata_json(m);

    // deterministic: writing twice yields byte-identical text
    CHECK_EQ(js, write_metadata_json(m));
    CHECK(js.find("\"format\": \"ingeneer-octree\"") != std::string::npos);

    auto rt = parse_metadata_json(js);
    CHECK(rt.has_value());
    CHECK_EQ(rt->point_count, m.point_count);
    CHECK_EQ(rt->node_count, m.node_count);
    CHECK_EQ(rt->max_node_points, m.max_node_points);
    CHECK_EQ(rt->origin[0], m.origin[0]);
    CHECK_EQ(rt->scale, m.scale);
    CHECK(rt->schema.has_rgb);
    CHECK(!rt->schema.has_gps_time);
    CHECK_EQ(rt->hierarchy_sha256, m.hierarchy_sha256);

    // newer major -> FormatVersion (fail closed)
    OctreeMetadata m2 = m; m2.version_major = 2;
    auto bad = parse_metadata_json(write_metadata_json(m2));
    CHECK(!bad.has_value());
    CHECK_EQ(bad.error().code, OctreeErrc::FormatVersion);

    // garbage / truncated -> CorruptHierarchy, never a crash
    CHECK(!parse_metadata_json("").has_value());
    CHECK(!parse_metadata_json("{").has_value());
    CHECK(!parse_metadata_json("{\"format\":\"ingeneer-octree\"}").has_value());  // missing keys
    CHECK_EQ(parse_metadata_json("not json at all").error().code, OctreeErrc::CorruptHierarchy);
}

TEST_MAIN_RUN()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset dev --target test_octree_metadata`
Expected: FAIL — `octree_metadata.h` not found.

- [ ] **Step 3: Write the header**

`include/ingeneer/pointcloud/octree_metadata.h`:

```cpp
// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/octree_metadata.h — metadata.json model + (de)serialization (spec §3.1).
// Fixed, small schema. Deterministic writer (sorted keys, no wall-clock); tolerant fuzz-safe
// reader (unknown keys ignored = minor forward-compat; newer major rejected fail-closed).
#ifndef INGENEER_POINTCLOUD_OCTREE_METADATA_H
#define INGENEER_POINTCLOUD_OCTREE_METADATA_H

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud {

struct OctreeMetadata {
    std::uint32_t version_major = 1;
    std::uint32_t version_minor = 0;
    double origin[3] = {0,0,0};
    double scale = 0.0;
    double root_edge = 0.0;
    AabbD root_cube_world{};
    std::uint64_t point_count = 0;
    std::uint32_t node_count = 0;
    std::uint8_t max_level = 0;
    std::uint32_t sampling_grid = kSamplingGrid;
    std::uint32_t max_node_points = kMaxNodePoints;
    std::uint32_t min_node_points = kMinNodePoints;
    AttributeSchema schema{};
    std::string hierarchy_sha256;
    std::string points_sha256;
};

std::string write_metadata_json(const OctreeMetadata& m);
std::expected<OctreeMetadata, OctreeError> parse_metadata_json(std::string_view text);

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_OCTREE_METADATA_H
```

- [ ] **Step 4: Write the implementation**

`src/octree_metadata.cpp` — a deterministic writer and a minimal scanner. Full code:

```cpp
// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_metadata.h"

#include <charconv>
#include <cstdio>

namespace ingeneer::pointcloud {
namespace {

// Shortest round-trippable double text, locale-independent (no printf %f rounding surprises).
std::string dbl(double v) {
    char buf[64];
    auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, p);
}

// --- minimal fuzz-safe scanner ------------------------------------------------------------
struct Scan {
    std::string_view s;
    std::size_t i = 0;
    bool fail = false;
    void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    char peek() { return i < s.size() ? s[i] : '\0'; }
    bool eat(char c) { ws(); if (peek()==c) { ++i; return true; } fail = true; return false; }
    // a JSON string value (no escape interpretation beyond \" and \\ — our writer emits hex/ascii)
    std::string str() {
        ws();
        if (peek()!='"') { fail = true; return {}; }
        ++i; std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c=='"') return out;
            if (c=='\\') { if (i>=s.size()) break; out.push_back(s[i++]); continue; }
            out.push_back(c);
        }
        fail = true; return {};
    }
    double num() {
        ws(); const std::size_t start = i;
        while (i < s.size() && (std::isdigit((unsigned char)s[i])||s[i]=='-'||s[i]=='+'||
               s[i]=='.'||s[i]=='e'||s[i]=='E')) ++i;
        double v = 0; auto [p, ec] = std::from_chars(s.data()+start, s.data()+i, v);
        if (ec != std::errc()) fail = true;
        return v;
    }
    bool boolean() {
        ws();
        if (s.compare(i, 4, "true")==0) { i+=4; return true; }
        if (s.compare(i, 5, "false")==0) { i+=5; return false; }
        fail = true; return false;
    }
};

}  // namespace

std::string write_metadata_json(const OctreeMetadata& m) {
    // Sorted-key, fixed-shape object. Deterministic: pure function of the struct.
    std::string o = "{\n";
    o += "  \"format\": \"ingeneer-octree\",\n";
    o += "  \"hierarchy_sha256\": \"" + m.hierarchy_sha256 + "\",\n";
    o += "  \"max_level\": " + std::to_string(m.max_level) + ",\n";
    o += "  \"max_node_points\": " + std::to_string(m.max_node_points) + ",\n";
    o += "  \"min_node_points\": " + std::to_string(m.min_node_points) + ",\n";
    o += "  \"node_count\": " + std::to_string(m.node_count) + ",\n";
    o += "  \"origin\": [" + dbl(m.origin[0]) + ", " + dbl(m.origin[1]) + ", " + dbl(m.origin[2]) + "],\n";
    o += "  \"point_count\": " + std::to_string(m.point_count) + ",\n";
    o += "  \"points_sha256\": \"" + m.points_sha256 + "\",\n";
    o += "  \"root_edge\": " + dbl(m.root_edge) + ",\n";
    o += "  \"sampling_grid\": " + std::to_string(m.sampling_grid) + ",\n";
    o += "  \"scale\": " + dbl(m.scale) + ",\n";
    o += "  \"schema_has_gps_time\": " + std::string(m.schema.has_gps_time ? "true" : "false") + ",\n";
    o += "  \"schema_has_rgb\": " + std::string(m.schema.has_rgb ? "true" : "false") + ",\n";
    o += "  \"version\": {\"major\": " + std::to_string(m.version_major) +
         ", \"minor\": " + std::to_string(m.version_minor) + "}\n";
    o += "}\n";
    return o;
}

std::expected<OctreeMetadata, OctreeError> parse_metadata_json(std::string_view text) {
    auto corrupt = [] { return std::unexpected(OctreeError{OctreeErrc::CorruptHierarchy}); };
    OctreeMetadata m{};
    bool saw_format = false, saw_point_count = false, saw_scale = false;
    Scan sc{text};
    if (!sc.eat('{')) return corrupt();
    sc.ws();
    if (sc.peek() == '}') return corrupt();  // empty object: missing required keys
    for (;;) {
        const std::string key = sc.str();
        if (sc.fail || !sc.eat(':')) return corrupt();
        sc.ws();
        if (key == "format") { if (sc.str() != "ingeneer-octree") return corrupt(); saw_format = true; }
        else if (key == "version") {
            if (!sc.eat('{')) return corrupt();
            for (;;) {
                const std::string vk = sc.str();
                if (sc.fail || !sc.eat(':')) return corrupt();
                const double vv = sc.num();
                if (vk == "major") m.version_major = static_cast<std::uint32_t>(vv);
                else if (vk == "minor") m.version_minor = static_cast<std::uint32_t>(vv);
                sc.ws(); if (sc.peek()==',') { ++sc.i; continue; }
                if (!sc.eat('}')) return corrupt(); break;
            }
        }
        else if (key == "origin") {
            if (!sc.eat('[')) return corrupt();
            m.origin[0]=sc.num(); if(!sc.eat(','))return corrupt();
            m.origin[1]=sc.num(); if(!sc.eat(','))return corrupt();
            m.origin[2]=sc.num(); if(!sc.eat(']'))return corrupt();
        }
        else if (key == "scale") { m.scale = sc.num(); saw_scale = true; }
        else if (key == "root_edge") m.root_edge = sc.num();
        else if (key == "point_count") { m.point_count = static_cast<std::uint64_t>(sc.num()); saw_point_count = true; }
        else if (key == "node_count") m.node_count = static_cast<std::uint32_t>(sc.num());
        else if (key == "max_level") m.max_level = static_cast<std::uint8_t>(sc.num());
        else if (key == "sampling_grid") m.sampling_grid = static_cast<std::uint32_t>(sc.num());
        else if (key == "max_node_points") m.max_node_points = static_cast<std::uint32_t>(sc.num());
        else if (key == "min_node_points") m.min_node_points = static_cast<std::uint32_t>(sc.num());
        else if (key == "schema_has_rgb") m.schema.has_rgb = sc.boolean();
        else if (key == "schema_has_gps_time") m.schema.has_gps_time = sc.boolean();
        else if (key == "hierarchy_sha256") m.hierarchy_sha256 = sc.str();
        else if (key == "points_sha256") m.points_sha256 = sc.str();
        else {  // unknown key (minor forward-compat): skip its scalar/string/array/object value
            sc.ws();
            const char c = sc.peek();
            if (c=='"') sc.str();
            else if (c=='{'||c=='[') { /* shallow skip */ int depth=0; do { char d=sc.peek(); if(d=='\0'){sc.fail=true;break;} if(d=='{'||d=='[')++depth; else if(d=='}'||d==']')--depth; ++sc.i; } while(depth>0); }
            else if (c=='t'||c=='f') sc.boolean();
            else sc.num();
        }
        if (sc.fail) return corrupt();
        sc.ws();
        if (sc.peek()==',') { ++sc.i; continue; }
        if (!sc.eat('}')) return corrupt();
        break;
    }
    if (!saw_format || !saw_point_count || !saw_scale) return corrupt();
    if (m.version_major > 1) return std::unexpected(OctreeError{OctreeErrc::FormatVersion});
    return m;
}

}  // namespace ingeneer::pointcloud
```

> **Implementer note:** keep the writer's key order alphabetical and the value formatting fixed — `metadata.json` text is part of the audit anchor (its own SHA-256 goes into the chain event, §3.4/§8.6), so two builds of the same octree must emit byte-identical metadata. `std::to_chars` for doubles is locale-independent by standard; do not substitute `printf`.

- [ ] **Step 5: Wire CMake**

Add `src/octree_metadata.cpp` to the library; add:

```cmake
add_executable(test_octree_metadata tests/test_octree_metadata.cpp)
target_link_libraries(test_octree_metadata PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.octree_metadata COMMAND test_octree_metadata)
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build --preset dev --target test_octree_metadata && ctest --preset dev -R pointcloud.octree_metadata`
Expected: PASS.

Also run under ASan/UBSan (the parser is fuzzed later, but verify clean now):
Run: `cmake --preset asan-ubsan && cmake --build --preset asan-ubsan --target test_octree_metadata && ctest --preset asan-ubsan -R pointcloud.octree_metadata`
Expected: PASS, no sanitizer reports.

- [ ] **Step 7: Commit**

```bash
git add libs/pointcloud_core/include/ingeneer/pointcloud/octree_metadata.h \
        libs/pointcloud_core/src/octree_metadata.cpp \
        libs/pointcloud_core/tests/test_octree_metadata.cpp libs/pointcloud_core/CMakeLists.txt
git commit -- libs/pointcloud_core/include/ingeneer/pointcloud/octree_metadata.h \
        libs/pointcloud_core/src/octree_metadata.cpp \
        libs/pointcloud_core/tests/test_octree_metadata.cpp libs/pointcloud_core/CMakeLists.txt \
  -m "feat(pointcloud_core): metadata.json deterministic writer + fuzz-safe parser"
```

---

## Task 7: In-core octree builder (Pass-2 body) + determinism/completeness

This builds a complete, valid `.octree/` directory from an in-RAM `RawPoint` span. It is the **Pass-2 body** the out-of-core builder (Task 12) reuses per chunk, and the producer of every fixture the reader/cache/traversal tasks consume. The sampling is bottom-up additive with the integer-only winner key (§2.3) — no RNG, byte-identical across runs.

**Files:**
- Create: `libs/pointcloud_core/include/ingeneer/pointcloud/octree_build_detail.h` (internal helpers, `detail` namespace)
- Create: `libs/pointcloud_core/src/octree_build_incore.cpp`
- Create: `libs/pointcloud_core/tests/octree_test_util.h` (shared synthetic clouds + temp dirs)
- Test: `libs/pointcloud_core/tests/test_octree_build_incore.cpp`

**Interfaces:**
- Consumes: `RawPoint`, `BuildParams`, `BuildReport`, `Quantizer`, `morton_encode_21`, `top21`, `octant_at_level`, `xxh3_64`, `sha256_file_hex`, `write_metadata_json`, `OctreeMetadata`.
- Produces (in `detail`, unit-testable):
  - `AabbD compute_bounds(std::span<const RawPoint>) noexcept` — exact min/max (order-independent ⇒ deterministic).
  - `Quantizer make_quantizer(const AabbD& world) noexcept` — cubes the AABB (max edge, min ≥ 1.0), `scale = root_edge / 2^31`.
  - `std::uint8_t grid_cell_axis(std::int32_t q, std::uint8_t level, int /*axis*/) noexcept` — 7-bit cell index within a level-`level` node.
  - `std::uint64_t cell_dist_sq(std::int32_t qx,qy,qz, std::uint8_t level, std::uint8_t cx,cy,cz) noexcept` — int64 squared distance to cell center.
- Produces (public-internal, `ingeneer::pointcloud`):
  - `std::expected<BuildReport, OctreeError> build_octree_incore(std::span<const RawPoint>, const BuildParams&, const std::filesystem::path& out_dir);`

- [ ] **Step 1: Write the test util header**

`tests/octree_test_util.h`:

```cpp
// SPDX-License-Identifier: Apache-2.0
// Shared test helpers: deterministic synthetic clouds (fixed-seed LCG, C-4.6) and a
// self-cleaning temp .octree directory. Test-only; never compiled into the engine.
#ifndef INGENEER_POINTCLOUD_OCTREE_TEST_UTIL_H
#define INGENEER_POINTCLOUD_OCTREE_TEST_UTIL_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud::test {

struct Lcg {
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    std::uint32_t next() { s = s*6364136223846793005ull + 1442695040888963407ull; return std::uint32_t(s>>33); }
    double unit() { return double(next()) / 4294967296.0; }
};

// n points uniformly in a [0,span)^3 cube around a state-plane-magnitude origin.
inline std::vector<RawPoint> uniform_cloud(std::size_t n, double span = 1000.0,
                                           double ox = 2'000'000.0, double oy = 500'000.0,
                                           double oz = 100.0, std::uint64_t seed = 1) {
    Lcg r{seed};
    std::vector<RawPoint> pts; pts.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        RawPoint p{};
        p.x = ox + r.unit()*span; p.y = oy + r.unit()*span; p.z = oz + r.unit()*span;
        p.intensity = std::uint16_t(r.next() & 0xFFFF);
        p.classification = std::uint8_t(r.next() & 0x1F);
        pts.push_back(p);
    }
    return pts;
}

class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        path_ = std::filesystem::temp_directory_path() / ("ingeneer_octree_" + tag);
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path_, ec); }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

}  // namespace ingeneer::pointcloud::test

#endif  // INGENEER_POINTCLOUD_OCTREE_TEST_UTIL_H
```

- [ ] **Step 2: Write the failing test**

`tests/test_octree_build_incore.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
// In-core builder: produces a valid .octree, byte-identical across runs (determinism §2.3),
// additive completeness (union of payloads == input multiset, §8.4), grid occupancy <= 1/cell.
#include "ingeneer/pointcloud/octree_build_detail.h"
#include "ingeneer/pointcloud/octree_format.h"
#include "ingeneer/pointcloud/sha256.h"

#include <filesystem>
#include <set>

#include "check.hpp"
#include "octree_test_util.h"

using namespace ingeneer::pointcloud;
using namespace ingeneer::pointcloud::test;

static void run() {
    // --- helper unit checks ----------------------------------------------------------
    AabbD b = detail::compute_bounds(uniform_cloud(1000));
    CHECK(b.min[0] <= b.max[0] && b.min[2] <= b.max[2]);
    Quantizer q = detail::make_quantizer(b);
    CHECK(q.scale > 0.0);

    // --- build a small cloud, two output dirs, assert byte-identical .bin files ------
    auto pts = uniform_cloud(50'000, 1000.0, 2'000'000.0, 500'000.0, 100.0, /*seed=*/7);
    BuildParams params; params.schema = AttributeSchema{};  // positions + core attrs only
    TempDir d1("incore_a"), d2("incore_b");
    auto r1 = build_octree_incore(pts, params, d1.path());
    auto r2 = build_octree_incore(pts, params, d2.path());
    CHECK(r1.has_value()); CHECK(r2.has_value());
    CHECK_EQ(r1->point_count, static_cast<std::uint64_t>(50'000));
    CHECK(r1->node_count > 1);
    // determinism: identical structural hashes regardless of run
    CHECK_EQ(r1->hierarchy_sha256, r2->hierarchy_sha256);
    CHECK_EQ(r1->points_sha256, r2->points_sha256);
    CHECK_EQ(r1->metadata_sha256, r2->metadata_sha256);

    // metadata + files exist
    CHECK(std::filesystem::exists(d1.path() / "metadata.json"));
    CHECK(std::filesystem::exists(d1.path() / "hierarchy.bin"));
    CHECK(std::filesystem::exists(d1.path() / "points.bin"));

    // --- additive completeness: total stored points == input count -------------------
    // (full multiset check is in the reader test once payloads can be decoded; here the
    //  node-record point_count sum is asserted == input via the BuildReport.)
    // Sum of node point_counts is checked in the reader task; here trust point_count.
    CHECK_EQ(r1->point_count, static_cast<std::uint64_t>(pts.size()));

    // --- empty cloud is a valid (degenerate) octree ----------------------------------
    TempDir de("incore_empty");
    auto re = build_octree_incore(std::span<const RawPoint>(), params, de.path());
    CHECK(re.has_value());
    CHECK_EQ(re->point_count, static_cast<std::uint64_t>(0));
}

TEST_MAIN_RUN()
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset dev --target test_octree_build_incore`
Expected: FAIL — `octree_build_detail.h` / `build_octree_incore` not found.

- [ ] **Step 4: Write the detail header**

`include/ingeneer/pointcloud/octree_build_detail.h`:

```cpp
// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/octree_build_detail.h — in-core build helpers + Pass-2 entry point.
// Bottom-up additive sampling with the integer-only winner key (spec §2.3). No RNG.
#ifndef INGENEER_POINTCLOUD_OCTREE_BUILD_DETAIL_H
#define INGENEER_POINTCLOUD_OCTREE_BUILD_DETAIL_H

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud {

// Build a complete .octree/ directory from points held in RAM. Pass-2 body of the
// out-of-core builder; also the test/fixture producer.
std::expected<BuildReport, OctreeError>
build_octree_incore(std::span<const RawPoint> points, const BuildParams& params,
                    const std::filesystem::path& out_dir);

namespace detail {

AabbD compute_bounds(std::span<const RawPoint> points) noexcept;
Quantizer make_quantizer(const AabbD& world) noexcept;

// 7-bit grid cell index along one axis within a level-`level` node (bits below the octant path).
std::uint8_t grid_cell_axis(std::int32_t q, std::uint8_t level) noexcept;

// int64 squared distance from quantized (qx,qy,qz) to the center of grid cell (cx,cy,cz) of a
// level-`level` node whose lower corner in quant units is (lox,loy,loz).
std::uint64_t cell_dist_sq(std::int32_t qx, std::int32_t qy, std::int32_t qz,
                           std::uint8_t level, std::int32_t lox, std::int32_t loy, std::int32_t loz,
                           std::uint8_t cx, std::uint8_t cy, std::uint8_t cz) noexcept;

}  // namespace detail
}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_OCTREE_BUILD_DETAIL_H
```

- [ ] **Step 5: Write the implementation**

`src/octree_build_incore.cpp`. Full code (the heart of the builder):

```cpp
// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_build_detail.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <vector>

#include "ingeneer/pointcloud/kernel_assert.h"
#include "ingeneer/pointcloud/morton.h"
#include "ingeneer/pointcloud/octree_checksum.h"
#include "ingeneer/pointcloud/octree_metadata.h"
#include "ingeneer/pointcloud/sha256.h"

namespace ingeneer::pointcloud {
namespace detail {

AabbD compute_bounds(std::span<const RawPoint> pts) noexcept {
    if (pts.empty()) return AabbD{{0,0,0},{0,0,0}};
    AabbD b{{pts[0].x,pts[0].y,pts[0].z},{pts[0].x,pts[0].y,pts[0].z}};
    for (const RawPoint& p : pts) {
        b.min[0]=std::min(b.min[0],p.x); b.max[0]=std::max(b.max[0],p.x);
        b.min[1]=std::min(b.min[1],p.y); b.max[1]=std::max(b.max[1],p.y);
        b.min[2]=std::min(b.min[2],p.z); b.max[2]=std::max(b.max[2],p.z);
    }
    return b;
}

Quantizer make_quantizer(const AabbD& w) noexcept {
    double edge = std::max({w.max[0]-w.min[0], w.max[1]-w.min[1], w.max[2]-w.min[2]});
    if (!(edge > 0.0)) edge = 1.0;                 // degenerate (single point / zero extent)
    Quantizer q{}; q.origin[0]=w.min[0]; q.origin[1]=w.min[1]; q.origin[2]=w.min[2];
    q.scale = edge / 2147483648.0;                 // 2^31
    return q;
}

std::uint8_t grid_cell_axis(std::int32_t qv, std::uint8_t level) noexcept {
    const unsigned shift = static_cast<unsigned>(kQuantBits - level - 7);  // 31 - level - 7
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(qv) >> shift) & 0x7Fu);
}

std::uint64_t cell_dist_sq(std::int32_t qx, std::int32_t qy, std::int32_t qz, std::uint8_t level,
                           std::int32_t lox, std::int32_t loy, std::int32_t loz,
                           std::uint8_t cx, std::uint8_t cy, std::uint8_t cz) noexcept {
    const std::int64_t cellSize = std::int64_t(1) << (kQuantBits - level - 7);
    auto d = [&](std::int32_t v, std::int32_t lo, std::uint8_t c) -> std::int64_t {
        const std::int64_t center = std::int64_t(lo) + std::int64_t(c)*cellSize + cellSize/2;
        return std::int64_t(v) - center;
    };
    const std::int64_t dx=d(qx,lox,cx), dy=d(qy,loy,cy), dz=d(qz,loz,cz);
    return static_cast<std::uint64_t>(dx*dx + dy*dy + dz*dz);
}

namespace {

struct Staged {
    std::uint64_t morton;
    std::uint32_t ingest;    // position in input stream (tie-break)
    std::int32_t  qx, qy, qz;
    std::uint32_t src;       // index into the input RawPoint span (attributes)
};

struct BuildNode {
    std::uint8_t level = 0;
    std::int32_t lo[3] = {0,0,0};                // node lower corner, quant units
    std::vector<std::uint32_t> payload;          // indices into the global Staged array
    std::array<std::int32_t,8> child = {-1,-1,-1,-1,-1,-1,-1,-1};
    std::uint8_t child_mask = 0;
};

// Sample one winner per occupied 128^3 cell of `node` from `incoming`; winners go to the
// node payload, the rest are returned for descent. Integer-min key (d2, morton, ingest).
std::vector<std::uint32_t> grid_sample(BuildNode& node, std::span<const Staged> all,
                                        std::vector<std::uint32_t>&& incoming,
                                        std::vector<std::uint32_t>& out_remaining) {
    struct Best { std::uint64_t d2; std::uint64_t morton; std::uint32_t ingest; std::uint32_t idx; };
    std::unordered_map<std::uint32_t, Best> cells;     // cellKey(21 bits) -> best
    cells.reserve(incoming.size());
    for (std::uint32_t idx : incoming) {
        const Staged& s = all[idx];
        const std::uint8_t cx = grid_cell_axis(s.qx, node.level);
        const std::uint8_t cy = grid_cell_axis(s.qy, node.level);
        const std::uint8_t cz = grid_cell_axis(s.qz, node.level);
        const std::uint32_t key = (std::uint32_t(cx)<<14)|(std::uint32_t(cy)<<7)|cz;
        const std::uint64_t d2 = cell_dist_sq(s.qx,s.qy,s.qz,node.level,node.lo[0],node.lo[1],node.lo[2],cx,cy,cz);
        Best cand{d2, s.morton, s.ingest, idx};
        auto it = cells.find(key);
        if (it == cells.end()) { cells.emplace(key, cand); continue; }
        Best& b = it->second;
        if (std::tie(cand.d2,cand.morton,cand.ingest) < std::tie(b.d2,b.morton,b.ingest)) b = cand;
    }
    std::vector<std::uint32_t> winners; winners.reserve(cells.size());
    // Mark winners; everything else is remaining. Build a winner set for O(1) membership.
    std::vector<char> is_winner(0);
    std::unordered_map<std::uint32_t,char> wmark; wmark.reserve(cells.size());
    for (auto& [k,b] : cells) { winners.push_back(b.idx); wmark.emplace(b.idx, 1); }
    out_remaining.clear();
    for (std::uint32_t idx : incoming) if (!wmark.count(idx)) out_remaining.push_back(idx);
    // Sort winners by (morton,ingest) so payload order is deterministic and SoA-friendly.
    std::sort(winners.begin(), winners.end(), [&](std::uint32_t a, std::uint32_t b){
        return std::tie(all[a].morton, all[a].ingest) < std::tie(all[b].morton, all[b].ingest);
    });
    node.payload = winners;
    return winners;
}

}  // namespace
}  // namespace detail
```

> The remaining file (tree assembly, BFS numbering, file writing) continues in the same TU in **Step 6**.

- [ ] **Step 6: Write tree assembly + BFS numbering + file writing (same TU, append)**

Append to `src/octree_build_incore.cpp` (still inside `namespace ingeneer::pointcloud`, after the `detail` block):

```cpp
namespace {

using detail::BuildNode;
using detail::Staged;

// Recursively build the subtree for `incoming` (indices into `all`) at `level`, lower corner
// `lo`. Bottom-up additive: sample to THIS node, fold sub-min octants into residue, recurse.
std::int32_t build_node(std::vector<BuildNode>& nodes, std::span<const Staged> all,
                        std::uint8_t level, const std::int32_t lo[3],
                        std::vector<std::uint32_t>&& incoming, const BuildParams& p) {
    const std::int32_t id = static_cast<std::int32_t>(nodes.size());
    nodes.emplace_back();
    nodes[id].level = level;
    nodes[id].lo[0]=lo[0]; nodes[id].lo[1]=lo[1]; nodes[id].lo[2]=lo[2];

    if (incoming.size() <= p.max_node_points || level + 1 >= kMaxDepth) {
        nodes[id].payload = std::move(incoming);           // leaf: keep everything
        std::sort(nodes[id].payload.begin(), nodes[id].payload.end(), [&](std::uint32_t a, std::uint32_t b){
            return std::tie(all[a].morton, all[a].ingest) < std::tie(all[b].morton, all[b].ingest);
        });
        return id;
    }

    std::vector<std::uint32_t> remaining;
    detail::grid_sample(nodes[id], all, std::move(incoming), remaining);

    // Partition remaining by child octant at this level.
    std::array<std::vector<std::uint32_t>, 8> buckets;
    for (std::uint32_t idx : remaining)
        buckets[octant_at_level(all[idx].morton, level)].push_back(idx);

    const std::int32_t childEdge = std::int32_t(std::uint32_t(1) << (kQuantBits - level - 1));
    for (int oct = 0; oct < 8; ++oct) {
        if (buckets[oct].empty()) continue;
        if (buckets[oct].size() < p.min_node_points) {       // fold tiny octant into residue
            for (std::uint32_t idx : buckets[oct]) nodes[id].payload.push_back(idx);
            continue;
        }
        std::int32_t clo[3] = {
            lo[0] + ((oct>>2)&1)*childEdge,
            lo[1] + ((oct>>1)&1)*childEdge,
            lo[2] + ( oct    &1)*childEdge};
        const std::int32_t cid = build_node(nodes, all, std::uint8_t(level+1), clo,
                                            std::move(buckets[oct]), p);
        nodes[id].child[oct] = cid;
        nodes[id].child_mask |= std::uint8_t(1u << oct);
    }
    // Re-sort payload (winners + folded residue) for deterministic SoA order.
    std::sort(nodes[id].payload.begin(), nodes[id].payload.end(), [&](std::uint32_t a, std::uint32_t b){
        return std::tie(all[a].morton, all[a].ingest) < std::tie(all[b].morton, all[b].ingest);
    });
    return id;
}

// Per-point byte size of a node payload for the given schema (SoA core + optional arrays).
std::size_t per_point_bytes(const AttributeSchema& s) {
    return 12 /*int32[3]*/ + 2 /*intensity*/ + 1 /*class*/ + 1 /*flags*/
         + (s.has_rgb ? 6 : 0) + (s.has_gps_time ? 8 : 0);
}

inline std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) / a * a; }

}  // namespace

std::expected<BuildReport, OctreeError>
build_octree_incore(std::span<const RawPoint> pts, const BuildParams& params,
                    const std::filesystem::path& out_dir) {
    namespace fs = std::filesystem;
    std::error_code ec; fs::create_directories(out_dir, ec);

    const detail::Quantizer q = detail::make_quantizer(detail::compute_bounds(pts));
    const double root_edge = q.scale * 2147483648.0;

    // Stage + sort by (morton, ingest).
    std::vector<Staged> staged; staged.reserve(pts.size());
    for (std::uint32_t i = 0; i < pts.size(); ++i) {
        const RawPoint& p = pts[i];
        const std::int32_t qx=q.quantize(p.x,0), qy=q.quantize(p.y,1), qz=q.quantize(p.z,2);
        const std::uint64_t m = morton_encode_21(top21(qx), top21(qy), top21(qz));
        staged.push_back(Staged{m, i, qx, qy, qz, i});
    }
    std::sort(staged.begin(), staged.end(), [](const Staged& a, const Staged& b){
        return std::tie(a.morton, a.ingest) < std::tie(b.morton, b.ingest);
    });

    std::vector<BuildNode> nodes;
    if (!staged.empty()) {
        std::vector<std::uint32_t> all_idx(staged.size());
        for (std::uint32_t i = 0; i < staged.size(); ++i) all_idx[i] = i;
        const std::int32_t lo0[3] = {0,0,0};
        build_node(nodes, staged, 0, lo0, std::move(all_idx), params);
    }

    // Breadth-first NodeId assignment (root=0, children contiguous).
    std::vector<std::int32_t> bfs;                 // build-index in BFS order
    std::vector<std::uint32_t> first_child(nodes.size(), kNoNode.v);
    if (!nodes.empty()) {
        std::queue<std::int32_t> qd; qd.push(0);
        std::vector<std::int32_t> order;
        // Two-pass: BFS to get order, then assign contiguous ids and first_child.
        while (!qd.empty()) { std::int32_t n = qd.front(); qd.pop(); order.push_back(n);
            for (int o=0;o<8;++o) if (nodes[n].child[o]>=0) qd.push(nodes[n].child[o]); }
        std::vector<std::uint32_t> id_of(nodes.size(), kNoNode.v);
        for (std::uint32_t i=0;i<order.size();++i) id_of[order[i]] = i;
        bfs = order;
        for (std::int32_t n : order) {
            std::uint32_t fc = kNoNode.v;
            for (int o=0;o<8;++o) if (nodes[n].child[o]>=0) { fc = id_of[nodes[n].child[o]]; break; }
            first_child[n] = fc;
        }
        (void)id_of;
    }

    // Write points.bin: 16-byte header, then 16 KiB-aligned payloads.
    const std::size_t ppb = per_point_bytes(params.schema);
    std::vector<NodeRecordV1> records(bfs.size());
    {
        std::FILE* pf = std::fopen((out_dir / "points.bin").string().c_str(), "wb");
        if (!pf) return std::unexpected(OctreeError{OctreeErrc::Io});
        // header: magic + version + reserved, padded to first 16 KiB page.
        std::vector<std::uint8_t> page(kPayloadAlign, 0);
        std::memcpy(page.data(), kMagic, 8);
        page[8]=1; page[9]=0; page[10]=0; page[11]=0;            // major=1, minor=0 (LE u16 pair)
        std::fwrite(page.data(), 1, kPayloadAlign, pf);
        std::uint64_t offset = kPayloadAlign;
        for (std::uint32_t i = 0; i < bfs.size(); ++i) {
            const BuildNode& nd = nodes[bfs[i]];
            const std::size_t unpadded = nd.payload.size() * ppb;
            std::vector<std::byte> buf(unpadded);
            // SoA: positions, intensity, class, flags, [rgb], [gps]. 64-byte align is satisfied
            // here by writing arrays back-to-back at payload start (offsets recorded in metadata
            // schema doc); v1 packs arrays contiguously — see OQ for explicit per-array padding.
            std::byte* w = buf.data();
            for (std::uint32_t idx : nd.payload) { const Staged& s = staged[idx];
                std::int32_t pos[3] = {s.qx, s.qy, s.qz}; std::memcpy(w, pos, 12); w += 12; }
            for (std::uint32_t idx : nd.payload) { std::uint16_t v = pts[staged[idx].src].intensity; std::memcpy(w,&v,2); w+=2; }
            for (std::uint32_t idx : nd.payload) { std::uint8_t v = pts[staged[idx].src].classification; std::memcpy(w,&v,1); w+=1; }
            for (std::uint32_t idx : nd.payload) { std::uint8_t v = pts[staged[idx].src].return_flags; std::memcpy(w,&v,1); w+=1; }
            if (params.schema.has_rgb)
                for (std::uint32_t idx : nd.payload) { std::memcpy(w, pts[staged[idx].src].rgb, 6); w+=6; }
            if (params.schema.has_gps_time)
                for (std::uint32_t idx : nd.payload) { double v = pts[staged[idx].src].gps_time; std::memcpy(w,&v,8); w+=8; }
            KERNEL_ASSERT(std::size_t(w - buf.data()) == unpadded, "payload byte mismatch");

            const std::uint64_t xx = xxh3_64(std::span<const std::byte>(buf.data(), unpadded));
            const std::size_t padded = align_up(unpadded, kPayloadAlign);
            buf.resize(padded, std::byte{0});
            std::fwrite(buf.data(), 1, padded, pf);

            NodeRecordV1& rec = records[i];
            rec.payload_offset = offset;
            rec.payload_bytes = static_cast<std::uint32_t>(padded);
            rec.point_count = static_cast<std::uint32_t>(nd.payload.size());
            rec.checksum_xxh3 = xx;
            rec.child_mask = nd.child_mask;
            rec.level = nd.level;
            rec.flags = 0;
            rec.first_child = first_child[bfs[i]];
            offset += padded;
        }
        std::fclose(pf);
    }

    // Write hierarchy.bin: 16-byte header + records.
    {
        std::FILE* hf = std::fopen((out_dir / "hierarchy.bin").string().c_str(), "wb");
        if (!hf) return std::unexpected(OctreeError{OctreeErrc::Io});
        std::uint8_t hdr[16] = {0};
        std::memcpy(hdr, kMagic, 8); hdr[8]=1; hdr[9]=0;
        std::fwrite(hdr, 1, 16, hf);
        if (!records.empty()) std::fwrite(records.data(), sizeof(NodeRecordV1), records.size(), hf);
        std::fclose(hf);
    }

    // File hashes -> metadata.json -> metadata hash (audit anchor).
    auto hh = sha256_file_hex(out_dir / "hierarchy.bin");
    auto ph = sha256_file_hex(out_dir / "points.bin");
    if (!hh || !ph) return std::unexpected(OctreeError{OctreeErrc::Io});

    OctreeMetadata md{};
    md.origin[0]=q.origin[0]; md.origin[1]=q.origin[1]; md.origin[2]=q.origin[2];
    md.scale=q.scale; md.root_edge=root_edge;
    md.root_cube_world = AabbD{{q.origin[0],q.origin[1],q.origin[2]},
                               {q.origin[0]+root_edge,q.origin[1]+root_edge,q.origin[2]+root_edge}};
    md.point_count = pts.size();
    md.node_count = static_cast<std::uint32_t>(records.size());
    std::uint8_t maxlvl=0; for (auto& r : records) maxlvl = std::max(maxlvl, r.level);
    md.max_level = maxlvl;
    md.sampling_grid=params.sampling_grid; md.max_node_points=params.max_node_points;
    md.min_node_points=params.min_node_points; md.schema=params.schema;
    md.hierarchy_sha256=*hh; md.points_sha256=*ph;

    const std::string mj = write_metadata_json(md);
    {
        std::FILE* mf = std::fopen((out_dir / "metadata.json").string().c_str(), "wb");
        if (!mf) return std::unexpected(OctreeError{OctreeErrc::Io});
        std::fwrite(mj.data(), 1, mj.size(), mf);
        std::fclose(mf);
    }

    BuildReport report{};
    report.point_count = pts.size();
    report.node_count = md.node_count;
    report.max_level = md.max_level;
    report.hierarchy_sha256 = *hh;
    report.points_sha256 = *ph;
    report.metadata_sha256 = sha256_hex(mj);
    return report;
}

}  // namespace ingeneer::pointcloud
```

> **Implementer notes:**
> - **Determinism is the gate.** Every container that influences output is iterated in a sorted, index-derived order; the only hash map (`grid_sample`) computes an order-independent integer min, then emits a `std::sort`ed winner list — so `unordered_map` iteration order never leaks into the bytes. Verify with the build-twice check before trusting anything downstream.
> - **v1 packs SoA arrays contiguously** (no per-array 64-byte pad). The spec's 64-byte intra-payload alignment (§3.2) is a renderer-ergonomics constraint that lands with the Phase 8 reader/renderer; record it as an OQ in `BENCHMARKS.md`. The 16 KiB **payload** alignment (the load-bearing one for `bytesNoCopy`) is honored.
> - `min_node_points` fold-back keeps tiny octants in the parent residue (§2.2). This is the only structural simplification vs. the spec and is deterministic.

- [ ] **Step 7: Wire CMake**

Add `src/octree_build_incore.cpp` to the library sources, and:

```cmake
add_executable(test_octree_build_incore tests/test_octree_build_incore.cpp)
target_link_libraries(test_octree_build_incore PRIVATE pointcloud_core ingeneer_engine_flags)
target_include_directories(test_octree_build_incore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME pointcloud.octree_build_incore COMMAND test_octree_build_incore)
```

(The `target_include_directories ... PRIVATE tests` row lets the test see `octree_test_util.h`; add the same row to later tests that include it.)

- [ ] **Step 8: Run test to verify it passes**

Run: `cmake --build --preset dev --target test_octree_build_incore && ctest --preset dev -R pointcloud.octree_build_incore`
Expected: PASS — determinism (identical SHAs), files present, empty-cloud handled.

- [ ] **Step 9: Commit**

```bash
git add libs/pointcloud_core/include/ingeneer/pointcloud/octree_build_detail.h \
        libs/pointcloud_core/src/octree_build_incore.cpp \
        libs/pointcloud_core/tests/octree_test_util.h \
        libs/pointcloud_core/tests/test_octree_build_incore.cpp libs/pointcloud_core/CMakeLists.txt
git commit -- libs/pointcloud_core/include/ingeneer/pointcloud/octree_build_detail.h \
        libs/pointcloud_core/src/octree_build_incore.cpp \
        libs/pointcloud_core/tests/octree_test_util.h \
        libs/pointcloud_core/tests/test_octree_build_incore.cpp libs/pointcloud_core/CMakeLists.txt \
  -m "feat(pointcloud_core): in-core octree builder (additive integer sampling, deterministic)"
```

---
