# third_party — vendored dependencies

Owner-approved top-level directory (ruling 2026-06-11, amends C-5.5). Governance for every
vendored package:

| Required file | Content |
|---|---|
| `LICENSE` | upstream license text, verbatim |
| `NOTICE` | attribution + InGENeer vendoring policy for the package |
| `UPSTREAM_COMMIT` | repository URL, exact commit, commit date, vendor date, license id |

Rules:

- **Mirror upstream cleanly.** No local modifications, no forks, no in-place patches.
  Behavior changes belong in wrappers under `libs/`.
- Vendor only what is consumed (library headers/sources), omit upstream tests/docs/build
  scaffolding; the omission is noted in `NOTICE`.
- Updates re-vendor a newer commit and update `UPSTREAM_COMMIT` — history shows exactly
  which upstream bytes shipped when (ADR-0027 freeze discipline applies to baselines that
  include vendored code).
- License allowlist (C-2.1, plan H-8): permissive (BSD/MIT/Apache-2.0/zlib) or MPL-2.0
  consumed unmodified. **No GPL/LGPL/AGPL, no non-commercial restrictions** (e.g.
  Triangle's license — excluded by plan §4.1).

Current packages:

| Package | License | Consumed by |
|---|---|---|
| `cdt/` | MPL-2.0 | `libs/geometry_core` (constrained Delaunay, Phase 5.3) |
| `nanoflann/` | BSD | `libs/geometry_core` / `libs/pointcloud_core` (KD-tree) |

Note: owned geometry-correctness code (e.g. Shewchuk-style predicates ported from auracad)
lives in `libs/geometry_core/`, **not** here — it is infrastructure we own and evolve, not
a mirrored dependency.
