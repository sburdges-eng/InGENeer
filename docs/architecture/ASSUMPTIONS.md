# Assumptions — Baseline V1

Assumptions underpinning approved decisions. If one falsifies, re-open the referenced decision.

| ID | Assumption | Depends | Falsification signal |
|----|-----------|---------|----------------------|
| A-1 | "Clean-sheet" = design freedom, not mandatory discard; ports compete on merit, incumbents win ties on equal merit | D3, D22 | Rewrite scope ballooning past trade-study estimates |
| A-2 | The open survey/TIN/point-cloud stack is safe to open because the moat is decisions-data + Intelligence layer | D7, D18, D21 | A well-funded fork closing the gap without the flywheel |
| A-3 | Apple Silicon local models can carry classification/segmentation at production point-cloud scale | D8, R-7.3 | Benchmarks failing on 100M+ pt datasets on M-series |
| A-4 | US small-firm PLS market adopts macOS-native tooling if field-to-finish time drops materially | D5, D6 | Beta cohort churn despite time savings |
| A-5 | A focused in-house TIN engine (constrained Delaunay + breaklines) is achievable on top of ported Shewchuk-style predicates | D13 | Robustness failures on degenerate field data after predicate port |
| A-6 | Swift↔C++ interop is production-ready for app↔engine boundary at viewport frame rates | D15, D17 | Interop overhead forcing an Obj-C++ shim layer |
| A-7 | Human-decision learning events carry enough signal to train survey foundation models without raw coordinates | D21 | Model quality plateau attributable to abstraction loss |
| A-8 | ODA terms permit the closed-module/subprocess isolation pattern at acceptable cost | D19, R10 | License review contradicting redistribution plan |
| A-9 | The append-only promotion log makes multi-user sync tractable via command-log replay | D10 | Conflict classes emerging that replay cannot resolve |
| A-10 | TOTaLi pipeline remains a valid reference oracle while new engines are built (frozen semantics) | D22 | Oracle drift — TOTaLi behavior changes mid-migration |
| A-11 | Entity Authority System will be acceptable (eventually favorable) to survey boards, DOTs, utilities, courts | D20 | Board feedback rejecting AI-proposed geometry workflows outright |
