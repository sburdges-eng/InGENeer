# surface_core — TIN perf baselines (Phase 6.5)

Recorded 2026-06-12 as the Phase 6.5 exit baseline ("perf benchmarks recorded as
baseline"). These are **baselines, not targets** — no optimization work was done in this
phase beyond removing one accidental quadratic (see Notes).

## Environment

| | |
|---|---|
| Hardware | Apple M4, 10 cores, 16 GiB |
| OS | macOS (Darwin 25.5.0) |
| Compiler | Apple clang 21.0.0 (`cc`) |
| Preset | `hardened` (RelWithDebInfo + OpenSSF flags; **never** a sanitizer build) |
| Commit | Phase 6.5 (`feat/surface_core` TIN fuzz + hardening) |

## Reproduce

```bash
cmake --preset hardened
cmake --build --preset hardened --target bench_tin
build/hardened/libs/surface_core/bench_tin            # all workloads
build/hardened/libs/surface_core/bench_tin random1m   # one workload
```

All workloads use a fixed-seed LCG (deterministic, C-4.6); 3 runs, **median** reported.
Runs 2–3 overlapped a background sanitizer CTest on the same machine (≤ 10 % noise on
the shorter workloads); the medians below are conservative.

## Insertion

| Workload | Points | Median total | Median rate | Result mesh |
|---|---:|---:|---:|---|
| `random10k` — uniform random | 10 000 | 0.031 s | 318 k pts/s | 19 976 tris, hull 22 |
| `random100k` — uniform random | 100 000 | 0.670 s | 149 k pts/s | 199 970 tris, hull 28 |
| `random1m` — uniform random | 1 000 000 | 68.2 s | 14.7 k pts/s | 1 999 968 tris, hull 30 |
| `lattice100k` — shuffled 317×317 integer lattice (cocircular-tie stress) | 100 489 | 0.788 s | 128 k pts/s | 199 712 tris, hull 1 264 |
| `roadway100k` — 13 lane offsets × 7 693 stations (collinear-heavy, survey insertion order) | 100 009 | 0.184 s | 544 k pts/s | 184 608 tris, hull 15 408 |

## Breaklines

| Workload | Median |
|---|---:|
| 1 000 two-point Split-policy breaklines into a 100 k random TIN | **222.9 µs / breakline** (all 1 000 ok; 1 028 constrained edges) |

## Contours / volumes

Out of scope for this baseline: `contour.cpp` / `volume.cpp` are owned by a parallel
Phase 6.3 work stream this session. Add their baselines when that stream lands.

## Notes (honest characterization)

* **Random insertion is super-linear** (149 k → 14.7 k pts/s from 100 k → 1 M): point
  location is a remembering walk from the previous insert, which is O(√n) expected per
  RANDOM query without spatial pre-sorting. Survey-ordered input (`roadway100k`) walks
  O(1) and runs 3.7× faster than random at the same size. Known future optimization:
  BRIO/Hilbert insertion order or a jump-and-walk seed — out of Phase 6.5 scope.
* During this phase an accidental quadratic was removed from `cavity_insert` (a per-call
  `std::vector<bool>(tris_.size())` re-initialization → epoch-stamped scratch array);
  the 1 M baseline above is post-fix.
* The per-breakline cost is dominated by `tri_with_vertex` (linear scan) and the
  recovery march; 222.9 µs/breakline ≈ 4.5 k breaklines/s into a 100 k TIN.
* Sanitizer lanes additionally run the KERNEL_DEBUG_ASSERT-tier full-mesh audit after
  every mutating op (`INGENEER_KERNEL_DEBUG_AUDIT`, Debug configs only); it is compiled
  out of this hardened build and has zero cost here.
