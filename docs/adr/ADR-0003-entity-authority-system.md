# ADR-0003 — AI Authority Doctrine & Entity Authority System

**Status:** Accepted (D1, D20). Confidence: near-mandatory. Legal provenance systems outlive AI models; if the platform is ever evaluated by survey boards, DOTs, utilities, or courts, this decision matters more than any model choice.

## Doctrine
> AI may create geometry. AI may modify geometry. AI may recommend geometry. **AI may NEVER certify geometry.** Certification requires human promotion of entity authority state. Authority is enforced at the entity level and recorded in the immutable audit chain.

Authority splits by domain: AI may be authoritative for production drafting (annotation, labels, tables, sheets, QC flags); AI is advisory-only for boundary resolution, control networks, and certifiable geometry.

## Decision — a stack, not a single mechanism
1. **Primary — entity authority metadata.** Every object: `EntityID, AuthorityClass, SourceAgent, CreatedBy/At, ApprovedBy/At, Confidence, VerificationState`. Example: `BoundaryLine_155 {AuthorityClass: AI_PROPOSED, VerificationState: PENDING, Confidence: 0.91, SourceAgent: BoundaryAgent_v4}` → after approval `{AuthorityClass: CERTIFIED, ApprovedBy: PLS_10284, ApprovedAt: …}`.
2. **Promotion workflow:** `AI_PROPOSED → REVIEWED → APPROVED → CERTIFIED`. Append-only; never overwrite; immutable history in the audit chain.
3. **Mechanical enforcement — database, not UI.** `AI_PROPOSED` cannot: appear on stamped deliverables, participate in final legal descriptions, be exported as certified geometry, be approved by AI. Only human authority promotes.
4. **Secondary — dual-document model.** Project working state vs **Certified Snapshot** generated from approved entities only (working branch vs tagged release `v1.0`).
5. **Tertiary — layers as visualization only** (AI_BOUNDARIES, AI_TOPO, AI_CONTOURS). Useful for display; authority comes from metadata.

## Rejected
- Layer-based authority alone: layers are user-mutable; too weak.
- Dual-document alone: heavy UX without per-entity provenance.
- AI-authoritative-everywhere ("AI is the OS" literal): liability-incompatible with licensed survey practice.

## Consequences
`audit_core` enforces promotion rules at the storage layer; export and legal-description code paths filter on `AuthorityClass`; UI renders authority state but cannot alter enforcement. Carried-forward TOTaLi invariants (DRAFT-layer, no auto-promote, PLS signature) map onto this system and are subsumed by it.
