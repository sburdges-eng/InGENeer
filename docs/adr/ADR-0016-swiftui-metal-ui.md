# ADR-0016 — SwiftUI + Metal Viewport UI

**Status:** Accepted (D17).

## Decision
The flagship macOS app is native: SwiftUI application chrome, a Metal-backed viewport view, and AppKit where SwiftUI is weak (docking, dense inspectors, precise input). Best-possible Apple experience is the product positioning; secondary platforms get their own UI later over the same headless core.

## Rejected
- Qt6 retained: one codebase for three OSes but permanently non-native macOS feel — conflicts with Apple-first positioning. The existing auracad Qt6 UI is end-of-life for the flagship.
- Dual-track now: premature; Windows/Linux UIs wait until those platforms activate.

## Consequences
The core remains UI-agnostic (headless-first, already proven in auracad); all UI authority displays are read-only views over the Entity Authority System (ADR-0003) — enforcement never lives in UI.
