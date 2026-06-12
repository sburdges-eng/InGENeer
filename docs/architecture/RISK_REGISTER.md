# Risk Register — Baseline V1

| ID | Risk | Sev | Status | Mitigation |
|----|------|-----|--------|------------|
| R-1 | Clean-sheet rewrite scope (second-system effect); ~32K LOC auracad + 700+ passing tests at stake | High | Open | Strangler pattern (ADR-0002/0019); knowledge-first rule; TOTaLi as reference oracle; incumbents win ties |
| R-2 | Authority-boundary creep — fuzzy AI-authoritative vs advisory line = liability for licensed work | High | Mitigated | Entity Authority System: machine-enforced at data model (ADR-0003); domain split codified R-2.8 |
| R-3 | Open-core forkability — competitors take the open survey/TIN engine | Med | Accepted | Moat = decisions flywheel (ADR-0017) + engine velocity; closed Intelligence layer |
| R-4 | DWG licensing | — | **Resolved** | ODA membership (ADR-0018); residual → R-10 |
| R-5 | Sync-service scope creep pulling v1 toward enterprise infra | Med | Open | Sync deferred until authority/audit storage stabilizes (ADR-0009) |
| R-6 | Local NL/agentic model quality gap vs cloud, given local-first default | Med | Open | Per-project cloud opt-in (ADR-0007); monitor local model progress |
| R-7 | CGAL GPL exposure | — | **Resolved** | CGAL dropped; permissive backend + custom TIN (ADR-0011/0012) |
| R-8 | Flywheel vs local-first confidentiality conflict | — | **Resolved** | Decision-based, privacy-filtered learning events (ADR-0017) |
| R-9 | Custom TIN hard 20%: constrained Delaunay + breakline integrity + degenerate field data + 100M+ pt scale | Med-High | Open | Port auracad adaptive predicates; oracle validation vs TOTaLi; out-of-core design upfront |
| R-10 | ODA membership cost/redistribution terms may not fit open-core structure | Med | **Resolved** | ADR-0020: open Core + closed ODA subprocess bridge compatible; **Sustaining** tier required for production distribution (>100 seats); budget + membership purchase remain business actions |
| R-11 | Swift↔C++ interop shortfall at viewport rates (A-6) | Low-Med | **Closed** | Proven by spike `tools/spikes/interop/` (run.sh): CPU path ~0.7 ns/call FFI + zero-copy (ADR-0025); render path Metal `bytesNoCopy` over 16KB-aligned C++ arena + realloc-under-render 1000/1000 clean under the H-27 quarantine contract (ADR-0025 addendum). Obj-C++ shim fallback not needed |
| R-12 | Regulator/board rejection of AI-proposed-geometry workflows (A-11) | Med | Open | Authority system designed for auditability-first; early engagement with boards/DOTs (open question §11.5) |
