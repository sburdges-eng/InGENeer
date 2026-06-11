# ADR-0014 — Languages: C++23 Core, Swift App, Python Intelligence

**Status:** Accepted (D15).

## Decision
- **C++23** for all engines/kernels (`libs/`): portable, permissively licensable, performance-critical. UI-free and Apple-framework-free.
- **Swift** for the flagship macOS app (`apps/desktop`), UI, and CoreML/Metal glue, using Swift↔C++ interop directly (no Obj-C++ shim unless interop falls short — A-6).
- **Python** for Aura Intelligence orchestration (agents, LLM integration), across a process/API boundary from the Core.

## Rejected
- Swift-heavy core: weakens the open Core's cross-platform story and C++ library ecosystem access.
- C++-everywhere with thin shell: forfeits Swift safety and Apple framework leverage in the app layer.

## Consequences
Carried hard rules: no FFI exceptions across `extern "C"`, GIL discipline on Python boundaries, sanitizer gates (ASAN/UBSAN debug CI), no `-ffast-math` in geometry/geodetic code.
