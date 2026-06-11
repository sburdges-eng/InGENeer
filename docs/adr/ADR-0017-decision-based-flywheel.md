# ADR-0017 — Decision-Based Learning Flywheel

**Status:** Accepted (D18, D21). Resolves the flywheel-vs-confidentiality conflict (R-8).

## Strategic insight
The moat is not millions of coordinates. **The moat is millions of professional decisions.** Every AI correction, topology fix, parcel adjustment, and approval outcome is training signal for the Survey Foundation Model.

## Decision
The training unit is the **human decision**, not raw project data.

```
Raw Survey Data → Local Abstraction → Privacy Filter → Learning Events → Global Training Corpus
```

- Store: graph topology, error patterns, correction patterns, approval outcomes, normalized (de-georeferenced) geometry, workflow metadata.
- Never store: actual coordinates, actual parcel geometry, project/client PII.
- Examples of captured decisions: boundary conflict → human selected solution A; contour anomaly → human selected repair B; legal-description ambiguity → human interpretation C.
- **Tier 1 (default):** local capture always on — improves the user's own local models; nothing leaves the machine.
- **Tier 1b (opt-in):** privacy-filtered learning events shared upstream, per-firm consent, optionally incentivized.
- **Tier 2 (enterprise):** raw/specific datasets only under explicit contract and consent.

`Raw Project → Cloud` is never the primary mechanism.

## Rejected
- Local-only flywheel: safest, slowest; kept as the default tier rather than the whole strategy.
- Raw-data pooling as primary: trust-destroying for the target market.

## Consequences
Position: customer trust + local first + privacy-preserving learning + decision-based training. The Entity Authority System (ADR-0003) is the natural event source — promotions, rejections, and corrections are exactly the decisions worth learning from. A-7 assumes abstracted decisions carry sufficient training signal.
