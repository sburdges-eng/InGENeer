# TOTaLi Oracle Fixture Extraction Procedure

**Status:** Procedure defined 2026-06-11 (Phase 2.5.1)  
**Governs:** `surface_core` / `geometry_core` validation (plan §4.3, req R-9, H-10)  
**ADR:** [ADR-0023](../../docs/adr/ADR-0023-totali-oracle-discipline.md)

## Rule

TOTaLi semantics are **frozen** for oracle purposes. Any TOTaLi change during migration requires a **drift report** before regenerating fixtures.

## Fixture metadata (required per fixture)

Each golden fixture committed under `libs/surface_core/tests/fixtures/oracle/` (future) MUST embed:

```yaml
oracle_fixture:
  id: "totali-extract-corpus-500pt-v1"
  source_repo: TOTaLi
  source_git_sha: "<full sha at extraction>"
  extraction_script: "tools/oracle/extract_from_totali.py"  # create at Phase 6
  input_hash: "sha256:<hex of inputs>"
  extracted_at: "2026-06-11"
  tolerances:
    elevation_m: 1.0e-4
    area_m2: 1.0e-2
    volume_m3: 1.0e-1
  quantities:
    - triangle_count
    - contour_vertex_count
    - volume_cut_fill
```

## Extraction sources (priority order)

1. **Survey corpus** — `TOTaLi/tests/fixtures/survey_corpus/synthetic_500pt.npy` + pinned SHA in `test_extractor_corpus_determinism.py`
2. **Extractor double-run** — `tests/test_extractor_determinism.py` pattern (same labels → same outputs)
3. **DXF round-trip** — `dwg-tool-parser/fixtures/sample.dxf` for planimetric smoke only (not TIN oracle)

## CI gate (Phase 6 target)

On every PR touching `libs/surface_core/` or `libs/geometry_core/`:

1. Run engine against fixture inputs.
2. Diff triangle sets, contours, volumes against oracle within stated tolerances.
3. Fail if `source_git_sha` or `input_hash` mismatch without accompanying drift report.

## Regeneration workflow

1. File drift report if TOTaLi semantics intentionally change.
2. Re-run extraction script against pinned TOTaLi SHA documented in report.
3. Update fixture metadata + commit oracle outputs together.
4. Human sign-off on tolerance changes.
