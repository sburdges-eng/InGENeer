# ADR-0028: Out-of-Core Point-Cloud Octree — Additive Sampling, Single-Blob Sidecar, Arena Residency; Module Home `pointcloud_core`

**Status:** Accepted
**Date:** 2026-06-12
**Deciders:** Owner-delegated ruling (delegation granted 2026-06-12)
**Related:** R-7.3 (100M+ points on 16 GB), risk R-9 (out-of-core mitigation), C-5.3 (design-first), C-1.2/C-1.3 (audit boundary, derived data), C-4.4/C-4.6, plan §4.2(5)/§5/§8 H-22/H-27, ADR-0012 (custom TIN), ADR-0015 (Metal-first rendering), ADR-0025 (interop C ABI), octree design spec (`docs/superpowers/specs/2026-06-11-out-of-core-octree-design.md`)

## Context

`docs/architecture/REQUIREMENTS.md` R-7.3 requires point-cloud handling to "scale to
100M+ points on 16 GB unified memory via streaming/out-of-core design," and
`docs/architecture/RISK_REGISTER.md` risk R-9 names "out-of-core design upfront" as a
mitigation for the 100M+ scale leg of the custom-TIN hard 20%. Phase 6.4 of the agentic
plan delivered that design as a spec — design-first per C-5.3, authorizing no code —
which passed review and merged on PR #24
(`docs/superpowers/specs/2026-06-11-out-of-core-octree-design.md`).

The spec proposed this ADR (its §9) to ratify its load-bearing decisions, and flagged one
question for the owner: **OQ-1, the module home** — the plan §4.2 lists the octree under
"Core algorithms (surface_core)" while ARCHITECTURE.md §4 assigns PDAL-backed ingestion
and point-cloud handling to `pointcloud_core`. This ADR ratifies the spec and rules OQ-1.

## Decision

The four load-bearing choices of the design spec are **ratified as decided**:

1. **Additive Potree-style nested-sampling octree** (spec §2.1). Each node stores a
   spacing-bounded subsample of the points in its cube; the renderable set at LOD *l* is
   the union of node payloads from root to *l*. Every point is stored exactly once — no
   LOD duplication overhead.
2. **Integer-only deterministic sampling** (spec §2.3). No RNG anywhere — not even
   seeded. Each grid cell's winner is the point minimizing the integer tuple
   `(d², morton63, ingestIndex)`; integer min is associative and commutative, so builds
   are **bit-identical across thread counts and platforms** (byte-identical
   `hierarchy.bin`/`points.bin`, hence identical SHA-256s). This exceeds H-22's
   tolerance-bounded cross-platform floor because no FP comparison participates in any
   structural decision (C-4.6/R-4.4 honored by construction).
3. **Single-blob sidecar with 16 KiB-aligned node payloads + arena-owned residency**
   (spec §3.1, §4). One `<cloud-id>.octree/` directory (`metadata.json`,
   `hierarchy.bin`, `points.bin`); every node payload starts on and pads to a 16 KiB
   boundary, so a `pread` into a 16 KiB-aligned arena slab is itself a valid
   `makeBuffer(bytesNoCopy:)` region (H-27-compatible, plan §5.1). Residency is a
   budget-bounded LRU node cache over C++-owned arena slabs with generation-stamped
   handles (H-27): a use-after-evict is an error return, never a dangling pointer.
   **mmap-direct is rejected for v1** (spec §4.5) — the byte budget would become
   advisory and the H-27 lifetime contract harder; the aligned format keeps that door
   open (OQ-5).
4. **Point clouds are bulk measurement data, NOT audit-chain entities** (spec §1.4,
   §3.4). The audit chain records a single ingest/build event whose payload carries the
   SHA-256 of `metadata.json`, which in turn carries the SHA-256s of `hierarchy.bin` and
   `points.bin` — a two-level Merkle anchor securing the whole structure with one chain
   hash. The octree is **derived and non-authoritative** (rebuildable; C-1.3 analog);
   certified measurement paths (R-4.3/R-4.4) never read LOD-sampled or quantized data —
   they read source records, which the octree may only help locate.

**OQ-1 ruling: the module home is `libs/pointcloud_core`.** The octree is a point-cloud
spatial structure: its producer is Phase 7's PDAL ingestion (`pointcloud_core` per the
plan's Phase 7 and ARCHITECTURE.md §4) and its consumer is the Phase 8 Metal renderer.
`surface_core` remains the TIN engine (ADR-0012); the plan §4.2's listing of the octree
under surface_core's algorithm list is **superseded on this point** — it is read as
Phase-6 sequencing, not module assignment, exactly as the spec proposed. Recorded as an
owner-delegated ruling (delegation granted 2026-06-12).

## Consequences

- Phase 7 implements the builder and reader in `libs/pointcloud_core` (a skeleton
  today), including the nanoflann delegation boundary (spec §6.2: the octree answers
  coarse node-selection only; exact k-NN/radius is a bulk-built nanoflann index over
  pinned payloads — one k-NN engine in the codebase).
- The Phase 8 renderer is bound by the spec §7 contract behind the RHI seam (C-4.4):
  node metadata in argument buffers, GPU frustum + screen-space-error culling into an
  ICB, int32 octree-local positions with a per-frame double-precision camera-relative
  transform, and the H-27 fence-deferred page-reuse handshake.
- The spec's testing obligations (§8) become Phase 7/8 exit criteria: the bit-identical
  determinism test, eviction/H-27 tests under ASan (TSan once threaded), fuzz targets
  for the sidecar readers, sampling-quality regressions, recorded perf baselines, and
  the single-audit-event boundary test.
- Remaining open questions **OQ-2 through OQ-5** (compression codec, classification-edit
  overlay, build-constant finalization, mmap-direct revisit) stay with the spec and are
  decided against Phase 7/8 baselines — none are ratified or foreclosed here.
- Numbering: this ADR takes 0028, the next free number after ADR-0027. The plugin SDK
  ABI spec's proposed ADR (`docs/superpowers/specs/2026-06-11-plugin-sdk-abi-spec.md`
  §9), which had also been pointed at 0028, is renumbered to **proposed ADR-0029**.

## Rejected alternatives

- **`surface_core` as module home:** couples the bulk LOD data lifecycle (gigabyte
  sidecars, residency budgets, renderer feeding) to the survey-deliverable TIN engine;
  the only textual support was a plan-§4.2 phase-sequencing artifact.
- **Standalone `lod_core` module:** no second consumer of the LOD machinery exists; a
  module with one client is a placeholder boundary against C-5.4's no-placeholder
  discipline.
- **Structural alternatives** (replacing-LOD octree, modifiable/dynamic octree,
  out-of-core KD-tree, flat Morton grid, file-per-node layout, mmap-direct v1) were
  evaluated and rejected with rationale in the spec (§2.1, §3.1, §4.5); those rejections
  are ratified along with the decisions they accompany.
