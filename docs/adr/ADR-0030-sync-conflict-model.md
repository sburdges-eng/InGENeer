# ADR-0030: Sync Conflict Model — Command-Log Replay, Authority-Aware Merge, Per-Replica Hash-DAG

**Status:** Proposed (awaiting owner sign-off on D-S1–D-S5 below; NOT accepted)
**Date:** 2026-06-12
**Deciders:** Pending — owner / licensed-professional sign-off required on every item in "Open questions blocking acceptance"
**Related:** ADR-0009 (project container + optional sync; conflict model open with the command-log-replay lean), ADR-0003 (Entity Authority System), A-9 (replay tractability assumption, depends D10), C-1.1/C-1.2/C-1.4, C-3.1/C-3.2, C-4.6, R-2.1–R-2.6/R-2.8, R-4.4, R-6.1–R-6.3, R-7.1/R-7.2, risk R-5 (sync scope creep), plan §8 H-18/§10 Phase 10, sync spec (`docs/superpowers/specs/2026-06-11-sync-conflict-model-spec.md`)

## Context

ADR-0009 made the local project container the source of truth (R-7.1) with sync
optional and additive, and left the conflict model **open** with a stated lean:
"command-log replay over the append-only promotion log." Phase 10 of the agentic plan
elaborated that lean into a reviewable design
(`docs/superpowers/specs/2026-06-11-sync-conflict-model-spec.md`) — **spec-first
only**: per RISK_REGISTER R-5 and plan §8 H-18, no sync service, transport, server,
or wire protocol is authorized; the spec exists so the Phase 3 `audit_core`
authority/audit substrate can be designed without foreclosing a sync path and without
being distorted by one.

The unit the model must keep honest is the attributed action: the container's
authoritative record is the append-only, hash-chained command/promotion log (R-2.6,
C-1.2), and assumption A-9 holds that this log makes multi-user sync tractable via
command-log replay (falsification signal: "conflict classes emerging that replay
cannot resolve"). The spec's §8 open questions and §9 decisions D-S1–D-S5 are the
items an ADR must capture; this ADR drafts that capture. Transport choice (PostGIS vs
object store per ADR-0009), identity provisioning, server deployment, CRDT libraries,
and conflict-resolution UI remain explicit non-goals.

## Decision (proposed — pending the sign-offs below)

The load-bearing choices of the sync spec, proposed for ratification:

1. **Replay the log, not the state** (spec §2, §2.1). Each replica holds its own
   append-only hash-chained event log (commands + promotions) and a materialized
   entity store that is a pure function of the log. Sync exchanges and merges log
   entries; state is re-derived deterministically by replay. CRDT-of-state is
   rejected for an authority-bearing system: a state CRDT erases or fabricates
   attribution (R-2.1), mutates in place against C-1.2's append-only mandate, lacks
   chain-of-custody offline verifiability (R-2.6), and has no place to refuse to
   converge where certification demands a human gate (C-1.1, R-2.3).
2. **Identity and ordering** (spec §3.1). Stable opaque random `ReplicaId` (never
   derived from user PII); per-entity, per-replica monotonic `entitySeq`; a causal
   stamp (vector-version summary or logical clock — D-S3) distinguishing concurrent
   from causally-ordered events. Wall-clock timestamps are evidence only, never the
   tiebreaker (C-4.6 posture). Events are content-addressed (`eventId` = SHA-256 of
   canonical body).
3. **Deterministic, lossless merge** (spec §3.2, §3.3). Causally-ordered events
   replay in causal order; concurrent commutative events are both kept under the
   fixed total order key `(causalRank, replicaId, entitySeq, eventId)`, giving
   byte-identical materialized state on every replica (R-4.4); concurrent
   conflicting events both remain in the log with an appended conflict marker and a
   deterministic provisional value — nothing is discarded. A merge never mutates
   either source chain; it is a pure function of its inputs (no wall-clock, no RNG,
   no locale).
4. **Authority-aware merge: authority transitions are never silently auto-merged**
   (spec §4). Production-drafting events auto-merge under the rules above;
   promotions do not. Classification of concurrent pairs on the same entity:
   draft ∥ draft → auto-merge; draft ∥ promotion below the APPROVED/CERTIFIED gate →
   auto-merge events but mark the entity *needs re-review* (never deleting events);
   draft ∥ APPROVE/CERTIFY → **human conflict** — the certification does not attach
   to the concurrent edit, both branches surface to a licensed reviewer;
   CERTIFY ∥ CERTIFY → **human conflict**. Rationale: auto-merging an edit "under" a
   certification would forge attribution — implying the licensed professional
   certified geometry they never reviewed (ADR-0003; risk R-2).
5. **Certified Snapshot consistency** (spec §4.3). Certification binds to a specific
   causal version of an entity (R-2.5, C-1.4). If sync moves the entity past that
   version, the materialized view reverts to *needs re-certification* via a new
   appended event while the original CERTIFIED event remains in the log unchanged
   (C-1.2). No code path emits a stamped/certified export from a
   merged-but-unre-attested entity (R-2.4, enforced at the data model).
6. **Per-replica hash chains + merge records: a hash-DAG** (spec §5; the central
   structural proposal, D-S1). Each replica's SHA-256 `prevHash` chain is immutable —
   sync never edits, reorders, or compacts history. A merge appends an explicit merge
   record referencing the tip hash of every parent chain. Verification generalizes
   `audit_core`'s `verify_chain()` from chain to DAG: verify each replica chain
   independently, then verify every merge record's `parents[].tipHash` — offline,
   no server (R-2.6). Replay is idempotent: re-received events are no-ops keyed on
   `eventId`; merging the same parents twice yields the same merge record.
7. **Privacy: sync metadata carries no coordinates or PII** (spec §6). The conflict
   model operates on identity and causality (hashes, sequence numbers, transition
   kinds), not geometry (C-3.1, C-3.2). Command payloads do carry project data —
   that is firm-replica sync (R-7.2, opt-in, self-hostable), a pipeline firewalled
   from the decision-based, privacy-filtered learning flywheel (R-6); sync must not
   become a backdoor shipping raw project data into learning. Any cross-firm/
   telemetry exposure of the merge layer is restricted to hashes and counts.
8. **Partition and failure posture** (spec §7). Partition is the normal case
   (local-first field work, ADR-0009/R-7.1); the model assumes long partitions.
   Durability is append-only with fsync before acknowledge; a crash mid-merge is
   recovered by re-deriving the disposable materialized store from the logs. There
   are no deletes and therefore no tombstones (C-1.2): removal is an appended
   retire/supersede event. Entities with unresolved human conflicts are excluded
   from certified deliverables exactly as `AI_PROPOSED` entities are (R-2.4/R-2.5).

Per the spec's scope, this ADR decides the **conflict model only**: transport remains
a separate, unbundled decision (spec §8 Q5; ADR-0009's PostGIS/object-store options
are unaffected), and no implementation is authorized (D-S5).

## Open questions blocking acceptance (owner sign-off checklist)

Carried verbatim from spec §9 ("Decision needed — human"). None may be resolved by an
agent; this ADR remains **Proposed** until each is ruled:

- **D-S1 — Structural model.** Accept "per-replica hash chains + merge records
  (hash-DAG)" as the append-only-preserving sync substrate, or direct an alternative.
  (spec §5)
- **D-S2 — Authority gate strictness.** Confirm that *any* concurrent edit against an
  APPROVE/CERTIFY MUST block and route to a human (spec §4.2), and confirm the
  re-certification-revert behavior (spec §4.3) is acceptable to the certification
  posture (C-1.4, R-2.3, R-2.5). This is a liability-bearing decision.
- **D-S3 — Causality scheme.** Choose vector-version vs logical-clock causality
  (spec §8 Q2) given expected field-replica counts.
- **D-S4 — Privacy/security scope.** Decide whether sync-payload encryption is in
  this ADR or a separate security ADR, and ratify that the firm-sync pipeline is
  firewalled from the learning-flywheel pipeline (spec §6, C-3.*, R-6).
- **D-S5 — Phasing.** Confirm sync stays **spec-only** through this plan (R-5 /
  H-18) and set the explicit gate ("authority/audit storage stabilizes", per
  ADR-0009 / R-7.2) that must be met before any sync *implementation* phase is
  scheduled.

The spec's §8 open questions feed these rulings: Q1 (DAG vs linearization → D-S1),
Q2 (causality representation → D-S3), Q3 (entity- vs field-level conflict
granularity), Q4 (re-certification trigger semantics — which causal movements revert
a CERTIFIED view, → D-S2), Q5 (transport stays a separate decision), and Q6 (whether
`audit_core` ships chain-only in v1 with the DAG verifier added later, or designs the
DAG-ready verifier from the start — without building sync).

## Consequences

- Acceptance closes ADR-0009's open conflict-model status with the elaborated form of
  its recorded lean; ADR-0009's transport options and local-source-of-truth posture
  are unchanged.
- `audit_core` (Phase 3) gains a concrete forward-compatibility target: the merge
  record and DAG verification generalize `verify_chain()` from chain to DAG (spec §8
  Q6 decides chain-only-now vs DAG-ready-now — open above).
- Acceptance authorizes **no sync implementation** (D-S5; R-5, H-18): no service,
  transport, server, or wire protocol. The illustrative event/merge-record JSON in
  the spec is design notation, not a wire schema.
- A-9 acquires its operational test: if conflict classes emerge that replay plus the
  §4 human gate cannot resolve, A-9's falsification signal has fired and this model
  must be revisited.
- The certification liability boundary survives sync by construction: no merge can
  attach a professional's attestation to state they never reviewed, and unresolved
  authority conflicts are structurally excluded from certified deliverables.

## Alternatives considered (from the spec; rejections pending ratification with this ADR)

- **CRDT over entity state:** rejected — silent convergence erases/fabricates
  attribution, mutates state in place against C-1.2, lacks offline chain-of-custody
  verifiability, and cannot refuse to converge where certification requires a human
  (spec §2.1).
- **Single re-linearized merged chain:** rejected as the default — forcing all events
  into one linear hash chain requires rewriting one side's history, violating
  append-only (spec §5, §8 Q1; the per-replica DAG is the recommended shape, pending
  D-S1).
- **Wall-clock tiebreaking / last-writer-wins:** rejected — timestamps are evidence
  only; the merge key is deterministic and clock-free (spec §3.1, §3.3; C-4.6).
- **Silent auto-merge of authority transitions:** rejected — forges attribution onto
  state the certifier never saw; exactly the liability the Entity Authority System
  exists to prevent (spec §4.2; ADR-0003, risk R-2).
- **Delete operations with tombstones:** rejected — C-1.2 forbids deletion; appended
  retire/supersede events sidestep the CRDT tombstone/GC problem class entirely
  (spec §7).
