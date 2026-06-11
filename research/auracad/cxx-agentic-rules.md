# auracad — CXX Agentic Rules (Extracted Summary)

**Status:** Extracted 2026-06-11 (Phase 2.5.2)  
**Canonical source:** `~/Dev/auracad/docs/CXX_AGENTIC_RULES.md` (~290 lines)  
**InGENeer target:** Merge with plan §3.7 sanitizer matrix + `docs/governance/autonomation/`

## Ten hard rules (abbreviated)

1. Sanitizers required on debug builds (ASan/UBSan minimum)
2. No STL across `extern "C"` boundaries
3. No `-ffast-math` family flags
4. No silent file deletion in agent workflows
5. No `--no-verify` on git commits
6. No destructive git without human approval
7. Fuzz parsers accepting untrusted input
8. GIL release if holding >1ms in Python C extensions
9. No exceptions across C ABI
10. Audit writer changes require SHA-256 re-verify tests

## Workflows

Per-edit: format → debug build → ASan/UBSan → ctest → clang-tidy diff gate.

## Audit integrity (§5.4)

- JSONL schema aligned with TOTaLi Python SHA-256 chain
- `fsync` after append
- No raw coordinates or PII in agent-work telemetry

## Tooling cross-ref

`auracad/scripts/README.md` — gate scripts mapping to §3.1 checklist.

**Action:** When Phase 2.2 CMake presets land, cite this doc + InGENeer hardening plan §3.7 as single agent checklist (avoid duplicate/conflicting rules).
