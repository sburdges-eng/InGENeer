# Glossary

| Term | Definition |
|------|-----------|
| **AuthorityClass** | Immutable-history entity field: `AI_PROPOSED`, `REVIEWED`, `APPROVED`, `CERTIFIED`. Source of authority truth (not layers). |
| **Aura Intelligence** | Closed AI layer: foundation models, agents, copilot, auto-drafting/QC/annotation, cloud, enterprise. |
| **AuraCAD Core** | Open engine layer: survey/geometry/TIN/point-cloud/coordinate engines, formats, plugin SDK, automation API. |
| **Certified Snapshot** | Deliverable-grade document generated exclusively from APPROVED/CERTIFIED entities; analogous to a tagged release vs working branch. |
| **Decision-based learning** | Flywheel doctrine: capture human decisions (corrections, selections, approvals) as abstracted learning events — never raw coordinates/PII. |
| **Entity Authority System** | The ADR-0003 stack: per-entity authority metadata + promotion workflow + database enforcement + dual-document + audit chain. |
| **Field-to-finish** | Field data collection → processing → drafting → deliverable, end-to-end. V1 automation target ≥90%. |
| **Learning event** | Privacy-filtered training record: graph topology, error/correction pattern, approval outcome, normalized geometry. |
| **Oracle (reference)** | TOTaLi's working pipeline, frozen-semantics, used to validate new engines during migration. |
| **PLS** | Professional Land Surveyor — the human certifying authority. |
| **Promotion** | Human-attributed advance of AuthorityClass toward CERTIFIED. Never performable by AI. Append-only. |
| **RHI seam** | Internal render-hardware-interface abstraction; Metal first, Vulkan possible later. |
| **Strangler pattern** | Migration approach: new system grows around the old until the old is archived. Knowledge first, code second. |
| **Survey Foundation Model** | Future closed model trained on the decisions corpus: survey semantics, control networks, boundary logic, legal descriptions. |
| **Spatial JEPA** | Research track: joint-embedding predictive architecture for point clouds/surfaces/topology. |
| **TIN** | Triangulated irregular network; in-house engine with constrained Delaunay + breaklines (ADR-0012). |
