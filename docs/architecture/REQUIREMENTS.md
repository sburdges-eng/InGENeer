# Requirements — Baseline V1

**Status:** Approved 2026-06-11. RFC-2119 keywords. Traceability: D-numbers → `../adr/`.

## R-1 Product

- R-1.1 The platform MUST operate standalone, with no dependency on Autodesk, Bentley, Carlson, Civil3D, OpenRoads, or proprietary CAD kernels (ODA codec exception per D19, isolated in a closed module).
- R-1.2 V1 MUST deliver the topo field-to-finish pipeline: field data import → surface generation → linework → annotated topo sheet (D5).
- R-1.3 V1 MUST deliver a legal-description engine (parcel → metes-and-bounds text and reverse) as a separately usable capability (D5).
- R-1.4 V1 scope is US jurisdictions only: state plane CRS, ALTA/NSPS standards, PLSS and metes-and-bounds descriptions (D11). CRS architecture MUST NOT preclude international expansion.
- R-1.5 Target automation: ≥90% of the field-to-finish workflow measured as human-touch reduction vs conventional drafting.

## R-2 Authority & Defensibility (non-negotiable)

- R-2.1 Every entity MUST carry authority metadata: `EntityID, AuthorityClass, SourceAgent, CreatedBy, CreatedAt, ApprovedBy, ApprovedAt, Confidence, VerificationState` (D20).
- R-2.2 Promotion MUST follow `AI_PROPOSED → REVIEWED → APPROVED → CERTIFIED`, append-only, never overwriting prior states.
- R-2.3 AI MUST NOT certify geometry. Promotion toward CERTIFIED MUST require a human action attributable to a licensed professional identity.
- R-2.4 Entities with `AuthorityClass = AI_PROPOSED` MUST be excluded, by the data model (not UI), from: stamped deliverables, final legal descriptions, certified exports.
- R-2.5 Certified deliverables MUST be generated exclusively from the Certified Snapshot (approved entities only).
- R-2.6 The audit chain MUST be append-only, hash-chained, and verifiable offline.
- R-2.7 CAD layers MUST NOT carry authority semantics; they are visualization-only.
- R-2.8 AI authority is split by domain (D1): AI MAY be authoritative for production drafting (annotation, labels, tables, sheets, QC flags); AI MUST be advisory-only for boundary, control networks, and certifiable geometry.

## R-3 Platform

- R-3.1 Primary platform: macOS on Apple Silicon. No design decision may compromise Apple Silicon performance (charter).
- R-3.2 Engines MUST be C++23 with no UI or Apple-framework dependencies (portability of the open Core) (D15).
- R-3.3 The flagship app MUST be Swift/SwiftUI with a Metal viewport (D16, D17).
- R-3.4 Rendering MUST sit behind an internal RHI seam permitting a future Vulkan backend (D16).
- R-3.5 Windows/Linux are secondary; the open Core MUST build on Linux for CI even before any non-mac app exists.

## R-4 Geometry & Survey Engines

- R-4.1 Geometry backend: Boost.Geometry, GEOS (dynamically linked), Eigen, nanoflann; PDAL, GDAL, PROJ for data. No GPL static linkage into the open Core (D12).
- R-4.2 The TIN engine MUST be built in-house: constrained Delaunay, breakline integrity, edge swaps, volumes, contours, surface queries (D13).
- R-4.3 Numeric policy: IEEE 754 binary64, exact/adaptive predicates on robustness-critical paths, no `-ffast-math` in geometry or geodetic code (carried from auracad rules).
- R-4.4 Deterministic paths: identical inputs MUST produce identical certified outputs; CRS changes MUST force recomputation (carried TOTaLi invariant).
- R-4.5 OCCT MUST be isolated behind `interop_core` module boundaries; no OCCT types in survey/geometry public APIs (D12).

## R-5 AI

- R-5.1 V1 AI = LLM orchestration + deterministic algorithms + small task-specific local models. No custom foundation model in the v1 critical path (D4).
- R-5.2 Inference MUST default to on-device (CoreML/ANE/ONNX); cloud inference MUST be per-project opt-in (D8).
- R-5.3 AI components MUST be model-agnostic at the interface level (swap models without engine changes).
- R-5.4 All AI-generated geometry MUST enter as `AI_PROPOSED` with `SourceAgent` and `Confidence` recorded.

## R-6 Learning Flywheel

- R-6.1 Training capture unit: human decisions (selection among alternatives, corrections, approvals) — not raw project data (D21).
- R-6.2 Pipeline MUST be: raw data → local abstraction → privacy filter → learning events → corpus. Raw projects MUST NOT be uploaded as the primary mechanism.
- R-6.3 Learning events MUST contain no raw coordinates or client PII (normalized/de-georeferenced geometry only).
- R-6.4 Upstream sharing MUST be opt-in per firm; enterprise raw-data sharing only under explicit contract (Tier 2).
- R-6.5 Local learning capture MUST function with sharing fully disabled.

## R-7 Data & Sync

- R-7.1 Project = single self-contained local container (DB + binary sidecars); openable and verifiable without network (D10).
- R-7.2 Optional sync service for multi-user firms; local container remains source of truth; self-hosting MUST be possible for enterprise.
- R-7.3 Point-cloud handling MUST scale to 100M+ points on 16 GB unified memory via streaming/out-of-core design.

## R-8 Interop

- R-8.1 DWG read/write via ODA SDK in a closed module/subprocess (D19). DXF, LandXML, IFC, glTF, PNEZD in the open Core.
- R-8.2 Carlson/IntelliCAD bridge maintained as migration ramp (transitional-first, D9).

## R-9 Quality Gates (every production feature)

Unit, integration, regression, geometry-validation tests + performance benchmarks. ASAN/UBSAN on Debug CI. Failing tests MUST NOT merge. New engines MUST validate against the TOTaLi reference oracle during migration (D22).
