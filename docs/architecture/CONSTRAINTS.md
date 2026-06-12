# Constraints — Baseline V1

Hard boundaries on all design and implementation work. Violations require an approved ADR superseding the constraint.

## C-1 Legal / Authority

- C-1.1 AI never certifies geometry. No code path may promote an entity to CERTIFIED without a human-attributed action. (D20)
- C-1.2 Audit chain is append-only. No deletion, no rewrite, no compaction that loses events.
- C-1.3 Authority semantics live in entity metadata only — never in layers, colors, filenames, or other user-mutable conventions.
- C-1.4 Certified deliverables derive only from the Certified Snapshot.

## C-2 Licensing

- C-2.1 No GPL code statically linked into the open Core. GEOS (LGPL) dynamic linkage only. CGAL is rejected (D12).
- C-2.2 ODA SDK confined to a closed interop module/subprocess; never exposed through open Core headers. (D19)
- C-2.3 The open/closed seam (D7) is fixed: survey/geometry/TIN/point-cloud/coordinate engines, formats, plugin SDK, automation API = open; JEPA, foundation models, agents, copilot, auto-drafting/QC/annotation, cloud, enterprise = closed.
- C-2.4 Closed components interact with the open Core via process/API boundaries, not by linking Core authority internals.

## C-3 Privacy

- C-3.1 No raw survey project data leaves the user's machine as a default or primary mechanism. (D8, D21)
- C-3.2 Learning events contain no raw coordinates and no client PII.
- C-3.3 Cloud inference is per-project opt-in.

## C-4 Technical

- C-4.1 Engines: C++23, UI-free, Apple-framework-free. Apple-specific code lives in `apps/` and Swift glue. (D15)
- C-4.2 No `-ffast-math` (or equivalent) in geometry/geodetic translation units. Frozen numeric policy constants.
- C-4.3 No OCCT types in public APIs outside `interop_core`. (D12)
- C-4.4 Renderer code only touches the GPU through the internal RHI seam. (D16)
- C-4.5 No FFI exceptions across `extern "C"`; GIL discipline on Python boundaries (carried from CXX_AGENTIC_RULES).
- C-4.6 Deterministic engines: no wall-clock, RNG, or locale dependence in measurement paths.

## C-5 Process

- C-5.1 Implementation is ON HOLD until architecture package review grants implementation authorization.
- C-5.2 Drift prevention: before any implementation task, validate against ARCHITECTURE.md, REQUIREMENTS.md, CONSTRAINTS.md, handoff.md, adr/. On drift: STOP, produce drift report, request approval.
- C-5.3 Migration order: knowledge first, code second, rewrite foundations when uncertain (R9 rule, D22).
- C-5.4 No placeholder implementations in production paths; failing tests never merge; public interface changes require approval.
- C-5.5 Monorepo top level is exactly: `apps/ libs/ research/ docs/ tools/ third_party/` — nothing else at **Stage 1b exit**. **Stage 1a (ADR-0022):** skeleton layout plus named legacy exceptions until Stage 3: `orchestrator/`, `icad-addin/`, `schemas/` only. (`third_party/` added by owner ruling 2026-06-11 for vendored dependencies — governance in `third_party/README.md`.)
- C-5.6 Every work session updates `docs/handoff.md`.
