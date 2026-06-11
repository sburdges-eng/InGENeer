# TOTaLi — Stage 2 Knowledge Extraction

**Status:** Extracted 2026-06-11 (Phase 2.5.1)  
**Source repo:** `~/Dev/TOTaLi` (reference oracle — keeps shipping; semantics frozen per MIGRATION_PLAN)  
**Maps to:** Entity Authority System (ADR-0003), surface oracle (plan §4.3), flywheel privacy (ADR-0017)

## Identity

Defensible spatial drafting pipeline. Motto: **AI classifies → algorithms measure → humans certify.**

## Seven invariants (subsumed into Entity Authority System)

Canonical source: `TOTaLi/AGENTIC_COMPLETION_PLAN.md` §1.

| # | Invariant | InGENeer mapping |
|---|-----------|------------------|
| 1 | No auto-certification (`auto_promote: false` hardcoded) | C-1.1 — AI never certifies; storage-layer promotion gate |
| 2 | `require_pls_signature: true` hardcoded | Human-attributed certification on deliverables |
| 3 | ML output non-authoritative; `TOTaLi-*-DRAFT` layers only | Authority split: AI advisory on boundary/survey geometry |
| 4 | `audit_logs/` append-only SHA-256 JSONL | `audit_core` product chain (C-1.2); InGENeer orchestrator `AuditLogger` already ports this |
| 5 | CAD writes only via `cad_shielding/` | Air-gap + host execution; no direct certified-layer writes |
| 6 | CRS change → rerun geodetic from scratch | `coordinate_core` invalidation rule (req R-4.4) |
| 7 | Deterministic geometry outputs (no fast-math, no nondeterministic parallelism) | C-4.2, C-4.6 |

**Enforcement tests (oracle repo):** `tests/test_config_invariants.py`, `tests/test_pipeline_e2e.py`, `tests/test_linting_auto_promote_guard.py`, `tests/test_segmentation_authoritative.py`, `tests/test_shield_layer_name_guard.py`.

## Pipeline surface (`PHASE_ORDER`)

Source: `totali/pipeline/orchestrator.py`, `config/pipeline.yaml`, `python3 -m totali.main`.

```
geodetic → segment → extract → shield → lint
```

| Phase | Module | Role | Authority |
|-------|--------|------|-----------|
| geodetic | `totali/geodetic/gatekeeper.py` | CRS/epoch/units gate; mixed-datum rejection | Deterministic gate |
| segment | `totali/segmentation/classifier.py` | ONNX point classification | **Non-authoritative** (S-7) |
| extract | `totali/extraction/extractor.py` | TIN, breaklines, contours, planimetrics | Deterministic measure |
| shield | `totali/cad_shielding/shield.py` | Geometry healing + DRAFT-layer CAD writes | Shielded export only |
| lint | `totali/linting/surveyor_lint.py` | Ghost overlay; accept/reject/defer; PLS signature | Human certify |

Each phase: `validate_inputs` → `run` (optional timeout) → merge into `PipelineContext` → audit events. First failure halts chain.

## Oracle / verification patterns

See [ORACLE_FIXTURE_PROCEDURE.md](ORACLE_FIXTURE_PROCEDURE.md).

| Pattern | Location | Use in InGENeer |
|---------|----------|-----------------|
| Survey corpus (500pt, seed=42) | `tests/fixtures/survey_corpus/` | `surface_core` determinism regression |
| Extractor corpus determinism | `tests/test_extractor_corpus_determinism.py` | Byte/count parity gates |
| DXF golden | `dwg-tool-parser/fixtures/sample.dxf` | `interop_core` DXF path smoke |
| Formula oracles (LS-2/LS-3) | `laser-suite/python/tests/unit/test_formula_oracle.py` | `survey_core` adjustment math |
| Shield determinism | `tests/test_shield_determinism.py` | Layer/geometry seq parity (UUID IDs differ) |

**Baseline:** ~607+ pytest cases documented (`Docs/TOTALI_MAPPING_TO_PRODUCTION_DESIGN.md`); run `pytest -q` in TOTaLi before any oracle fixture regeneration.

## InGENeer boundaries

- **Geometry kernel:** auracad (not TOTaLi) — see `research/auracad/`
- **Orchestrator wire contract:** legacy InGENeer Python — see `research/ingeneer-legacy/`
- **JEPA / scene ML:** `research/jepa/` track only; TOTaLi consumes ONNX via `totali/models/loader.py`

## Key source paths

| Topic | Path in TOTaLi |
|-------|----------------|
| Invariants | `AGENTIC_COMPLETION_PLAN.md` §1 |
| Production map | `Docs/TOTALI_MAPPING_TO_PRODUCTION_DESIGN.md` |
| Per-module agent docs | `totali/{geodetic,segmentation,extraction,cad_shielding,linting}/AGENTIC.md` |
| Audit logger | `totali/audit/logger.py` |
| Pipeline CLI | `totali/main.py` |
