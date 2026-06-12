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

### Bulk insertion — T11 BRIO/Hilbert `insert_many` (recorded 2026-06-12)

`Tin::insert_many` reorders the batch (BRIO rounds, Hilbert order within each round —
deterministic, no RNG) and feeds the UNCHANGED single-point `insert()` machinery, turning
the remembering walk's O(√n)-per-insert locate cost into O(1) expected. This implements
the "BRIO/Hilbert insertion order" future-work note below; insertion semantics are
untouched. Methodology: same environment and fixed-seed LCG clouds as above, `hardened`
preset, median of 3 runs on an otherwise idle machine; timed region includes the
ordering/sort work. Result meshes (tris/hull) are identical to the sequential rows.

| Workload | Points | Median total | Median rate | Sequential median | Speedup |
|---|---:|---:|---:|---:|---:|
| `bulk10k` — uniform random | 10 000 | 0.016 s | 620 k pts/s | 0.031 s | 1.9× |
| `bulk100k` — uniform random | 100 000 | 0.154 s | 650 k pts/s | 0.670 s | 4.4× |
| `bulk1m` — uniform random | 1 000 000 | 1.580 s | 633 k pts/s | 68.2 s recorded / 57.1 s re-measured¹ | **43× / 36×** |
| `bulklattice` — shuffled lattice | 100 489 | 0.176 s | 570 k pts/s | 0.788 s | 4.5× |
| `bulkroadway` — survey order | 100 009 | 0.193 s | 517 k pts/s | 0.184 s | 0.95×² |

¹ The recorded 68.2 s baseline overlapped a background sanitizer CTest (see Reproduce
  note above); `random1m` re-measured at this commit on an idle machine is 57.102 s.
  The honest same-session speedup is 36×; the bulk rate is now essentially flat in n
  (620–650 k pts/s from 10 k → 1 M), i.e. the super-linear behavior is gone.
² Survey-ordered input already walks O(1) sequentially; the bulk path's sort/round
  shuffle buys nothing there and costs ~5 %. Use plain `insert()` loops for data that
  is already spatially ordered; use `insert_many` for everything else.

## Breaklines

| Workload | Median |
|---|---:|
| 1 000 two-point Split-policy breaklines into a 100 k random TIN | **222.9 µs / breakline** (all 1 000 ok; 1 028 constrained edges) |

## Notes (honest characterization)

* **Sequential random insertion is super-linear** (149 k → 14.7 k pts/s from 100 k →
  1 M): point location is a remembering walk from the previous insert, which is O(√n)
  expected per RANDOM query without spatial pre-sorting. Survey-ordered input
  (`roadway100k`) walks O(1) and runs 3.7× faster than random at the same size.
  **Resolved for batch workloads by `insert_many`** (BRIO/Hilbert bulk insertion — see
  the bulk rows above: flat ~620–650 k pts/s from 10 k → 1 M); a jump-and-walk seed
  for scattered SINGLE inserts remains future work.
* During this phase an accidental quadratic was removed from `cavity_insert` (a per-call
  `std::vector<bool>(tris_.size())` re-initialization → epoch-stamped scratch array);
  the 1 M baseline above is post-fix.
* The per-breakline cost is dominated by `tri_with_vertex` (linear scan) and the
  recovery march; 222.9 µs/breakline ≈ 4.5 k breaklines/s into a 100 k TIN.
* Sanitizer lanes additionally run the KERNEL_DEBUG_ASSERT-tier full-mesh audit after
  every mutating op (`INGENEER_KERNEL_DEBUG_AUDIT`, Debug configs only); it is compiled
  out of this hardened build and has zero cost here.

## Contours & volumes (Phase 6 exit)

Recorded 2026-06-12 at merge commit `b5bfb03` (post Phase 6.3/6.5 landing) on the same
machine/preset as the Environment table above (Apple M4, `hardened` preset, Apple
clang 21.0.0). Baselines, not targets. Same methodology: fixed-seed LCG synthetic
surfaces (deterministic, C-4.6); 3 runs, **median** reported. TIN construction is NOT
included in any measured time.

### Reproduce

```bash
cmake --preset hardened
cmake --build --preset hardened --target bench_quantities
build/hardened/libs/surface_core/bench_quantities             # all workloads
build/hardened/libs/surface_core/bench_quantities contours    # one workload
# workloads: contours | volplane | volshared | overlay10k | overlay50k
```

Surfaces are smooth synthetic height fields (long-wavelength sinusoids + tilt, z ~ 10–95 m)
over LCG-random xy in [0, 10000]²; see `bench/bench_quantities.cpp` for the exact fields.

### Contour extraction

| Workload | Median total | Median / level | Output |
|---|---:|---:|---|
| `contours` — 100 k-pt TIN (199 970 tris), 20 evenly spaced levels | 0.024 s | 1.20 ms/level | 28 polylines, 19 249 segments |
| `chaikin x2` — Chaikin smoothing (2 iterations) of all 28 contours, timed separately | 0.352 ms | 0.018 ms/level | 77 052 smoothed points |

Extraction is a full finite-triangle scan per level plus combinatorial chaining — cost is
O(T) per level and essentially independent of how many segments a level produces. The
smooth field yields few but long polylines (~1.4 polylines, ~960 segments per level);
the derived Chaikin pass is ~70× cheaper than extraction and is cosmetic-only (C-1.3).

### Volumes

| Workload | Median | Result sanity |
|---|---:|---|
| `volplane` — volume_to_plane, 100 k-pt TIN, plane at mid elevation | 0.003 s | area = TIN footprint (25.0 km²) |
| `volshared` — volume_between, shared-support fast path: two 100 k-pt TINs, identical xy stream (identical triangle sets), two z-fields | 0.028 s | area = footprint (identical hulls) |
| `overlay10k` — volume_between, general overlay: two INDEPENDENT 10 k-pt triangulations (different xy seeds, same region) | 0.271 s | cut/fill within 0.3 % of `volshared`'s densest answer |
| `overlay50k` — same, 50 k vs 50 k | 6.146 s | hull-intersection area converges to footprint |

### Notes (honest characterization)

* **The general overlay is super-linear**: 10 k→50 k per surface (5×) costs 22.7×
  (~n^1.9 observed). The documented non-asymptotic part is the sort-by-bbox prefilter
  pairing (`volume.cpp`); candidate pairs grow faster than the O(n) truly-overlapping
  pairs. The header already notes a plane-sweep/DCEL overlay as future work — out of
  scope here.
* The shared-support fast path is ~10× the cost of `volume_to_plane` on the same point
  count (it walks both meshes and verifies support identity) and ~220× cheaper than the
  50 k general overlay — detecting shared support is very much worth it.
* `volshared` deliberately uses two TINs built from the SAME xy stream; random points
  have no cocircular ties, so both Delaunay triangulations are identical and the fast
  path is taken (verified: identical `area` to `volplane`'s footprint).
* Cut and fill are individually exact per the mixed-triangle d = 0 split (volume.h);
  the overlay rows agree with the shared-support answer to <0.3 % at 10 k and <0.05 %
  at 50 k, as expected for independent samplings of the same pair of smooth fields.
