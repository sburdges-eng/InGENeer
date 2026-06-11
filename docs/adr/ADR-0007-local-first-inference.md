# ADR-0007 — Local-First Inference, Cloud Opt-In

**Status:** Accepted (D8).

## Decision
On-device inference (CoreML/ANE, ONNX Runtime) is the default for all AI features. Cloud frontier-LLM use is per-project opt-in. Client survey data is confidential by default and never leaves the machine as a side effect of using the product.

## Rejected
- Local-only hard requirement: caps NL/agentic quality unnecessarily when users consent.
- Cloud-as-normal-path: conflicts with survey-data confidentiality and the trust positioning.

## Consequences
Local model quality gap (R-6) accepted and monitored; Apple Silicon (unified memory, ANE) is the performance bet (A-3); the flywheel (ADR-0017) must work entirely locally when sharing is disabled.
