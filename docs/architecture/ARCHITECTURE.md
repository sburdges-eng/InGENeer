# InGENeer Platform Architecture — Baseline V1

**Status:** APPROVED AS BASELINE V1 (2026-06-11)
**Implementation:** ON HOLD — requires architecture package review and explicit implementation authorization.
**Branding:** "InGENeer" used as working name; TOTALI / AuraCAD / InGENeer pending final selection.
**Traceability:** Decision IDs D1–D22 reference the Architecture Discovery Interview (Rounds 1–3, June 2026). ADRs in `../adr/`.

---

## 1. Mission

AI-native surveying and land development CAD platform, Apple Silicon first. Target: ≥90% automation of field-to-finish, with legal defensibility as a first-class architectural property.

**V1 wedge (D5):** topographic field-to-finish pipeline (field data → surface → linework → annotated sheet) as the spine; legal-description engine as the early revenue sidecar. **Jurisdiction (D11):** US-only v1 (state plane, ALTA/NSPS, PLSS, metes-and-bounds); international architected-for, not built.

## 2. Governing Doctrine

> AI may create geometry. AI may modify geometry. AI may recommend geometry.
> **AI may NEVER certify geometry.**
> Certification requires human promotion of entity authority state.
> Authority is enforced at the entity level and recorded in the immutable audit chain.

(D1, D20 — see ADR-0003. Considered near-mandatory; legal provenance systems outlive AI models.)

Authority is **split by domain** (D1): AI may be authoritative for production drafting (annotation, labeling, sheets, tables, QC flagging); AI is advisory-only for boundary resolution, control networks, and any geometry destined for certification.

## 3. System Layers

```
Field Data (TS / GNSS / UAV / LiDAR)
        ↓
Survey Core (libs/) — deterministic engines
        ↓                          ↑ proposals / corrections
Aura Intelligence (closed) — agents, models, copilot
        ↓
Entity Authority System — promotion gate (human-only certification)
        ↓
Certified Snapshot → Deliverables (plats, legals, sheets, exports)
```

The open Core is deterministic and authoritative for measurement. The closed Intelligence layer is probabilistic and advisory/productive. They communicate across a process/API boundary — Intelligence never links the Core's authority internals.

## 4. Module Architecture (monorepo layout)

```
ingeneer/                      (this repo, InGENeer, becomes the monorepo — Stage 1)
  apps/
    desktop/                   Swift + SwiftUI + Metal flagship macOS app (D17)
  libs/                        OPEN CORE (D7) — C++23 (D15)
    survey_core/               Boundary, Parcel, Alignment, Corridor, QA engines (D14)
    geometry_core/             Exact predicates, planar topology; backend: Boost.Geometry,
                               GEOS (LGPL, dynamic), Eigen, nanoflann (D12)
    parcel_core/
    surface_core/              Custom TIN engine: constrained Delaunay, breaklines,
                               volumes, contours, surface queries (D13)
    pointcloud_core/           PDAL-backed ingestion, classification interfaces
    coordinate_core/           PROJ-backed CRS, geodetics, state plane
    interop_core/              GDAL (GIS); OCCT satellite: DWG interop, future
                               solids/NURBS/BIM/mechanical (D12); ODA codec in a
                               CLOSED module/subprocess (D19); DXF, IFC, LandXML
    ai_core/                   Model runtime interfaces (CoreML/ONNX), authority-safe
                               proposal APIs — interfaces open, models closed
    audit_core/                Append-only SHA-256-chained audit log; Entity Authority
                               System enforcement (D20)
  research/                    PRESERVED KNOWLEDGE (migration Stage 2)
    jepa/                      Spatial JEPA R&D (parallel track, not v1 — D4)
    boundary_ai/
    legal_ai/
  docs/
    adr/                       Decision records
    architecture/              This package
  tools/
```

**Open/closed seam (D7):** open = survey/geometry/TIN/point-cloud/coordinate engines, file formats, plugin SDK, automation API. Closed = Spatial JEPA, Survey Foundation Models, agent system, copilot, auto-drafting/QC/annotation, cloud services, enterprise features, ODA codec module. Permissive licensing of the Core is possible because CGAL was rejected (D12) — Apache-2.0/BSD candidate, final license TBD.

## 5. Entity Authority System (D20)

Every entity carries: `EntityID, AuthorityClass, SourceAgent, CreatedBy/At, ApprovedBy/At, Confidence, VerificationState`.

Promotion workflow (append-only, never overwrite):

```
AI_PROPOSED → REVIEWED → APPROVED → CERTIFIED
```

Enforced in the data model, not UI. `AI_PROPOSED` cannot: appear on stamped deliverables; participate in final legal descriptions; export as certified geometry; be approved by AI. Only human authority promotes.

**Dual-document layer:** `Project` working state vs **Certified Snapshot** generated exclusively from approved entities (working branch vs tagged release). **Layers are visualization-only** (AI_BOUNDARIES, AI_TOPO, …); authority lives in metadata.

## 6. AI Architecture

**Strategy (D4):** orchestrate-first. V1 = frontier LLM orchestration + deterministic algorithms + small task-specific local models (CoreML/ONNX). Spatial JEPA and Survey Foundation Model are parallel R&D (`research/`), shipped later.

**Inference locality (D8):** local-first on Apple Silicon (CoreML/ANE); cloud LLMs per-project opt-in. Client survey data confidential by default.

**Learning flywheel (D18, D21):** the training unit is *human decisions*, not raw data.

```
Raw Survey Data → Local Abstraction → Privacy Filter → Learning Events → Global Training Corpus
```

Store: graph topology, error/correction patterns, approval outcomes, normalized geometry. Never: raw coordinates, project PII. Local capture always (improves the user's own models); upstream sharing opt-in per firm; Tier 2 enterprise data contracts for specific datasets. The moat = millions of professional decisions + the survey geometry engine.

## 7. Platform & Rendering

- **Languages (D15):** C++23 engines (portable, open); Swift for app/UI/CoreML/Metal glue via Swift↔C++ interop; Python for Intelligence orchestration.
- **Rendering (D16):** Metal-first behind a thin internal RHI seam (point clouds, TIN, 2D drafting); Vulkan backend possible later for secondary platforms.
- **UI (D17):** SwiftUI chrome + Metal viewport + AppKit where SwiftUI is weak. Qt6 UI (legacy auracad) is end-of-life for the flagship.
- Apple Silicon exploitation: unified memory (zero-copy point cloud → GPU), ANE via CoreML, Accelerate.

## 8. Data Architecture (D10)

Local self-contained **project container** (SQLite + binary sidecars for point clouds/surfaces) is the source of truth. Optional **sync service** for multi-user firms layered on top (conflict model TBD — the append-only promotion log is replay-friendly; see Open Questions). Audit chain (`audit_core`) is append-only and defensibility-critical.

## 9. Interop & Migration Posture

- **DWG (D19):** ODA membership; codec isolated in closed module/subprocess.
- **Carlson/IntelliCAD bridge (D9):** transitional first (migration ramp), strategic second if mixed-shop demand persists.
- **Legacy repos (D22 + R9 rule):** strangler pattern, knowledge-first. See `MIGRATION_PLAN.md`. TOTaLi's working pipeline (676 tests) remains the reference oracle during engine rewrites.

## 10. Market & Licensing (D6, D7)

Dual market: solo PLS / small firms first (adoption velocity), mid/enterprise second. Open-core model per §4 seam.

## 11. Open Questions (non-gating, pre-implementation)

1. Plugin SDK ABI strategy across the C++/Swift open Core.
2. Sync-service conflict model (command-log replay vs CRDT).
3. Agent system internals (Aura Intelligence decomposition).
4. Final branding.
5. State-board / DOT / court acceptance strategy for the Entity Authority System.
6. ODA membership tier, cost, and redistribution terms vs open-core structure (R10).
7. Final open-source license for the Core (Apache-2.0 vs BSD vs MPL).

## 12. Success Criteria (from charter)

Clean clone builds; full test suite passes; CAD engine operational; survey workflows operational; AI workflows operational; documentation + handoff + deployment guide complete; macOS release, Apple Silicon optimized; production readiness verified.
