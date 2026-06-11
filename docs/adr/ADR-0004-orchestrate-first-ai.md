# ADR-0004 — Orchestrate-First AI Strategy

**Status:** Accepted (D4).

## Context
Spatial JEPA and a Survey Foundation Model are multi-year research efforts requiring large proprietary datasets the platform doesn't yet have.

## Decision
V1 ships on: frontier LLMs for natural-language, workflow planning, and QA agents; deterministic algorithms for all measurement; small task-specific local models (CoreML/ONNX) for classification/segmentation. Spatial JEPA and the Survey Foundation Model run as parallel R&D tracks under `research/` (jepa, boundary_ai, legal_ai), shipping when ready — they are not v1 blockers. The decision-based flywheel (ADR-0017) accumulates their training corpus in the meantime.

## Rejected
- Custom foundation models in the v1 critical path: longest time-to-revenue, data-starved.
- Early fine-tuning of open 3D models as a v1 dependency: research risk in the critical path.

## Consequences
All AI integration is model-agnostic at the interface level; `ai_core` exposes proposal APIs that any backing model can drive; AI output always enters as `AI_PROPOSED` (ADR-0003).
