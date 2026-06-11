# Sync Conflict Model — Design Spec

**Status:** Draft
**Date:** 2026-06-11
**Phase:** 10 (Plugin SDK ABI + sync conflict model)
**Pending Task:** 7 (command-log-replay sync over promotion log → ADR; spec-first)
**Author:** Claude Code session (feat/llm-intent-generator)

## 1. Purpose & Scope

This is a **design-only** specification for how a *future* optional multi-user sync
capability would reconcile concurrent edits across replicas of an InGENeer project
container. It exists to make the conflict model decidable *before* any sync code is
written, so that the authority/audit substrate (Phase 3 `audit_core`) can be designed
without foreclosing a sync path — and without being distorted by one.

**Spec-first, no service is built now.** This document defines the conflict model,
invariants, and open questions. It does **not** authorize, scaffold, or implement a sync
service, transport, server, or wire protocol. That stays out of scope per:

- **RISK_REGISTER R-5** — "Sync-service scope creep pulling v1 toward enterprise infra"
  (Med, Open): mitigation is *"Sync deferred until authority/audit storage stabilizes
  (ADR-0009)."*
- **Plan §8 H-18** — "Unbounded scope creep (sync service, enterprise features)":
  mitigation *"sync deferred until authority/audit storage stabilizes; YAGNI enforced at
  plan review."* Plan §10 Phase 10 reiterates: *"Phase 10 sync conflict model is
  spec-first only — no sync implementation in this plan."*
- **ADR-0009** — Status records the conflict model as **open**, with a stated lean:
  *"command-log replay over the append-only promotion log."* This spec elaborates that
  lean into a concrete, reviewable model and surfaces the decisions an ADR must ratify.

In scope: the conflict model, ordering semantics, authority-aware merge rules, the
append-only-under-merge invariant, privacy of sync metadata, partition/failure behavior,
and the human decisions an ADR must capture.

Out of scope (explicit non-goals): transport choice (PostGIS vs object store per
ADR-0009), authentication/identity provisioning, server deployment, CRDT libraries, UI
for conflict resolution, and any timeline commitment. The local container remains the
source of truth (R-7.1, R-7.2); sync is additive and removable.

## 2. The Substrate: Replay the Log, Not the State

InGENeer's project container is a single self-contained local store: a SQLite-backed
entity/audit store plus binary sidecars (ADR-0009, R-7.1). The authoritative record of
*how the project came to be* is the **append-only, hash-chained command/promotion log**
(R-2.6, C-1.2) — the same substrate `audit_core` rewrites in Phase 3, merging TOTaLi/
InGENeer's SHA-256 JSONL chain-of-custody semantics with auracad's command naming and the
Entity Authority promotion log (see `research/auracad/audit-chain-design.md`).

The sync model replays **the log of commands and promotion events**, not raw entity
state. Each replica holds:

1. its own append-only event log (commands + promotions, hash-chained), and
2. a derived/materialized entity store that is a *pure function* of the log.

Sync exchanges and merges **log entries**; the entity store is then re-derived
deterministically by replay. This is the operational meaning of **A-9**:

> A-9 — "The append-only promotion log makes multi-user sync tractable via command-log
> replay." (Depends: D10. Falsification signal: *"Conflict classes emerging that replay
> cannot resolve."*)

### 2.1 Why command-log-replay, not CRDT-of-state

A CRDT (conflict-free replicated data type) over *entity state* would converge two
replicas to a merged value automatically and silently. For an **authority-bearing**
system that is the wrong default:

- **Attribution cannot be auto-merged.** Authority metadata records *who* did *what*
  and *when* (R-2.1: `CreatedBy`, `ApprovedBy`, `ApprovedAt`, …). A state CRDT that
  picks a winning field value erases or fabricates attribution. The unit that must
  survive sync is the *attributed action* (the command/promotion event), not the
  resulting value. A command log preserves each action verbatim.
- **Append-only is native to a log, foreign to a state CRDT.** C-1.2 forbids deletion,
  rewrite, or lossy compaction. An event log is append-only by construction; a converged
  CRDT state document is mutated in place and loses the per-action history.
- **Offline verifiability needs the chain (R-2.6).** A hash-chained event log can be
  verified offline by anyone; a CRDT's merge metadata is not a chain-of-custody.
- **Certification is a human gate, not a merge function (C-1.1, R-2.3).** Some events
  (promotion toward CERTIFIED) *must not* be auto-resolved. A log lets us classify an
  event and route authority-bearing classes to a human; a state CRDT has no place to
  refuse to converge.

The cost is that replay is not *automatically* conflict-free: two logs may interleave in
ways that need a deterministic merge rule (§3) and, for authority transitions, an
explicit human decision (§4). That cost is accepted deliberately — it is the price of
keeping attribution and certification honest.

## 3. Conflict Model

### 3.1 Identity and ordering

- **Replica identity.** Each container instance has a stable opaque `ReplicaId`
  (random, not derived from user PII — see §6). Field crews and office workstations are
  distinct replicas even for the same user.
- **Per-entity sequence.** Every event names the `EntityID` it acts on and carries a
  per-entity monotonic `entitySeq` *within its origin replica*. This gives a total order
  of one replica's edits to one entity.
- **Global causality.** Events carry a causal stamp (a vector/version summary keyed by
  `ReplicaId`, or a Lamport-style logical clock — see Decision D-S3) so the merge can
  tell *concurrent* events from *causally-ordered* ones. Wall-clock timestamps are
  **evidence only**, never the tiebreaker (consistent with the engines' determinism
  posture, C-4.6: no wall-clock dependence in decision paths).

An event is roughly:

```jsonc
// illustrative — NOT a wire schema, NOT authorized for implementation
{
  "eventId":    "<sha256 of canonical body>",
  "replicaId":  "<opaque>",
  "entityId":   "<EntityID>",
  "entitySeq":  42,                 // per-entity, per-replica monotonic
  "causal":     { "<replicaId>": 17, "...": 3 },  // version summary
  "prevHash":   "<sha256 of prior event on THIS replica's chain>",
  "kind":       "command" | "promotion",
  "payload":    { /* command name + args, OR promotion transition */ }
}
```

### 3.2 Concurrent edits to the same entity

Two replicas edit `EntityID=E` while partitioned. On merge, the union of both replicas'
events for `E` is replayed. Three cases:

1. **Causally ordered** (one replica had seen the other's event): replay in causal
   order; no conflict.
2. **Concurrent, commutative** (e.g., two independent annotation/label additions, two
   non-overlapping property sets): both events are kept and applied; deterministic
   ordering by `(causalRank, replicaId, entitySeq, eventId)` ensures every replica
   reaches byte-identical materialized state (mirrors R-4.4 determinism).
3. **Concurrent, conflicting** (both mutate the same field of the same entity in
   incompatible ways): the merge is **deterministic but lossless** — *both* events remain
   in the log; the materialized store records a **conflict marker** on `E` and applies a
   deterministic provisional value (last in the deterministic order). The conflict marker
   is itself an appended event, surfaced for human resolution. Nothing is discarded.

### 3.3 Deterministic merge

Given the same set of input event logs, the merge MUST produce the same merged log and
the same materialized store on every replica (a function of the inputs only — no
wall-clock, no RNG, no locale; C-4.6 spirit). The merge order key is fixed and total. A
"merge" is not a mutation of either source chain; it produces a new **causality/merge
record** (§5) that references both parents by hash.

## 4. Authority-Aware Conflict Rules

This is the heart of the model. Not all events are equal: **production-drafting** events
(annotation, labels, tables, sheets, QC flags — where AI MAY be authoritative, R-2.8)
can be auto-merged under §3. **Authority transitions** cannot.

### 4.1 The promotion ladder is append-only and human-gated

Promotion follows `AI_PROPOSED → REVIEWED → APPROVED → CERTIFIED`, append-only, never
overwriting prior states (R-2.2). AI MUST NOT certify; promotion toward CERTIFIED
requires a human action attributable to a licensed professional identity (R-2.3, C-1.1,
R-2.8 advisory-only for boundary/control/certifiable geometry).

### 4.2 Hard rule: authority transitions are never silently auto-merged

> A `CERTIFY` (or any promotion toward CERTIFIED/APPROVED) on one replica that is
> *concurrent* with an edit to the same entity on another replica MUST surface as an
> **explicit, human-resolved conflict**. The merge MUST NOT silently pick a winner, and
> MUST NOT carry the human attribution of the certification onto state the certifier
> never saw.

Rationale: a certifier attests to a specific Certified Snapshot (R-2.5, C-1.4). If
replica A certifies entity `E` at causal point *p*, and replica B concurrently edits `E`
(reaching causal point *q* ∥ *p*), then auto-merging B's edit "under" A's certification
would forge attribution — it would imply the licensed professional certified geometry
they never reviewed. That is exactly the liability the Entity Authority System exists to
prevent (RISK_REGISTER R-2; ADR-0003).

Therefore the merge classifies the interaction:

| Concurrent pair on same entity | Resolution |
|---|---|
| draft edit ∥ draft edit | auto-merge (§3.2), deterministic |
| draft edit ∥ promotion **below** CERTIFIED/APPROVED gate | auto-merge events, but mark entity **needs re-review** (downgrade verification state of the materialized view, never delete events) |
| draft edit ∥ APPROVE / CERTIFY | **HUMAN CONFLICT** — block auto-resolution; the certification does **not** attach to the concurrent edit; surface both branches to a licensed reviewer |
| CERTIFY ∥ CERTIFY (two replicas) | **HUMAN CONFLICT** — two attestations of divergent snapshots cannot both stand silently |

### 4.3 Certified Snapshot consistency (C-1.4, R-2.5)

Certified deliverables derive *only* from the Certified Snapshot — the set of approved/
certified entities. A merge MUST NOT produce a Certified Snapshot that contains entity
state never attested by the recorded certifier. Operationally: certification binds to a
specific causal version of an entity; if sync moves the entity past that version, the
certification's `VerificationState` for the materialized view reverts to *needs
re-certification* (a new appended event), while the original CERTIFIED event remains in
the log unchanged (append-only, C-1.2). No code path may emit a stamped/certified export
from a merged-but-unre-attested entity (R-2.4 exclusion is enforced at the data model,
not UI).

## 5. Append-Only Invariant Under Sync (C-1.2 / R-2.6)

Merging two chains MUST preserve append-only semantics and offline verifiability.

- **No rewrite of either parent chain.** Each replica's chain (`prevHash` linkage,
  SHA-256 per `audit_core` target) is immutable. Sync never edits historical events,
  never re-orders a replica's own chain, never compacts (C-1.2).
- **Per-replica chains + a merge/causality record.** Rather than forcing all events into
  one linear hash chain (which would require rewriting one side), the model keeps **one
  hash chain per replica** and introduces an explicit **merge record**: an appended event
  that references the tip hash of every parent chain it incorporates.

```jsonc
// illustrative merge record — design only
{
  "eventId":   "<sha256>",
  "kind":      "merge",
  "parents":   [ { "replicaId": "A", "tipHash": "..." },
                 { "replicaId": "B", "tipHash": "..." } ],
  "mergeOrderKey": "deterministic (see §3.3)",
  "conflicts": [ /* entityIds routed to human resolution, §4 */ ]
}
```

- **Offline verifiability survives the merge.** A verifier checks each replica's chain
  independently (each is a valid SHA-256 `prevHash` chain on its own), then checks that
  every merge record's `parents[].tipHash` actually matches a real event in the named
  chain. The combined structure is a hash-DAG (per-replica linear chains joined by merge
  records) that is verifiable offline with no server (R-2.6) — the property `verify_chain()`
  guarantees today, generalized from a chain to a DAG.
- **Replay is idempotent (§7).** Re-applying an already-incorporated event is a no-op
  keyed on `eventId`; merging the same parents twice yields the same merge record.

This per-replica-chain + merge-record shape is the central structural proposal and the
primary thing the ADR must accept or replace (Decision D-S1).

## 6. Privacy: What Crosses the Wire

Sync metadata MUST NOT leak survey coordinates or client PII (C-3.1, C-3.2, R-6.3;
CXX_AGENTIC_RULES §5.4 "no coordinate/PII leakage in telemetry chain", cited in
`research/auracad/audit-chain-design.md`).

- The **conflict model itself operates on event identity and causality**, not on
  geometry: `ReplicaId`, `EntityID`, `entitySeq`, causal stamps, `eventId`/hashes, and
  promotion-transition kinds. None of those require transmitting coordinates.
- `ReplicaId` is an opaque random token, **not** derived from a username, email, MAC
  address, device serial, or geolocation (so the *metadata layer* carries no PII even
  before payload encryption is considered).
- **Command payloads do contain project data** (they describe edits). This is the line
  between two concerns: *replication of a firm's own project across that firm's own
  replicas* (R-7.2 multi-user firm sync; the data legitimately moves, optionally
  self-hosted) versus *the learning flywheel* (R-6: decision-based, privacy-filtered,
  de-georeferenced learning events that MUST NOT carry raw coordinates/PII). **These are
  different pipelines.** This spec governs the former; it MUST NOT become a backdoor that
  ships raw project data into the latter. C-3.1 still holds: raw project data does not
  leave the user's machine as a *default* mechanism — sync is opt-in (R-7.2) and
  self-hostable for enterprise.
- What the **merge/causality layer** exposes for cross-firm or telemetry purposes (if
  ever) is restricted to hashes and counts — no entity payloads. Concretely: a
  conflict-resolution audit can be summarized as "N conflicts on M entities" without
  emitting any coordinate.

Decision D-S4 records whether payload-at-rest/in-transit encryption is in scope for the
sync ADR or a separate security ADR.

## 7. Failure & Partition Handling

- **Partition is the normal case.** Local-first + offline field work (ADR-0009, R-7.1)
  means replicas are *usually* partitioned; sync is an occasional reconvergence, not a
  live session. The model assumes long partitions and is designed around them, not around
  low-latency coordination.
- **Idempotent replay.** Every event is keyed by content-addressed `eventId`
  (SHA-256 of canonical body). Re-receiving an event is a no-op. Re-running a merge over
  the same parent tips yields the identical merge record. A crash mid-merge is recovered
  by re-deriving the materialized store from the (durable, fsync'd) logs — the store is
  disposable; the logs are truth.
- **Tombstone-free / no deletes (C-1.2).** There are no delete operations and therefore
  no delete-vs-edit conflicts and no tombstone garbage-collection problem. "Removing" an
  entity is an appended *retire/supersede* event that the materialized view honors while
  the entity's history remains in the log forever. This sidesteps the classic CRDT
  tombstone class entirely.
- **Durability.** Logs are written append-only with `fsync` before acknowledging
  (carried from `audit_core` WAL policy, ref H-21 in the audit-chain extraction). A
  partial write is detected by the hash chain on next verify and the trailing partial
  event is ignored (it was never acknowledged).
- **No partial-merge state leaks into Certified outputs.** While conflicts from §4 await
  human resolution, affected entities are excluded from any certified deliverable by the
  data model (R-2.4/R-2.5), exactly as `AI_PROPOSED` entities are.

## 8. Open Questions (→ ADR)

These elaborate ADR-0009's open conflict-model status and feed the Phase 10 sync ADR:

1. **DAG vs linearization.** Per-replica chains joined by merge records (§5) vs a single
   re-linearized chain. (Recommended: DAG, to preserve append-only without rewrite.)
2. **Causality representation.** Vector-version summary per `ReplicaId` (precise,
   grows with replica count) vs Lamport/hybrid logical clock (compact, coarser). Bounding
   metadata growth for many field replicas.
3. **Conflict granularity.** Entity-level vs field-level conflict detection. Field-level
   reduces false conflicts but complicates the authority gate in §4.
4. **Re-certification trigger semantics.** Exactly which causal movements revert a
   `CERTIFIED` materialized view to *needs re-certification* (§4.3) — any concurrent
   event, or only ones touching certifiable fields.
5. **Transport boundary.** ADR-0009 names PostGIS or object store as *options*; this
   spec is transport-agnostic. The ADR should restate that transport is a **separate**
   decision and not bundle it with the conflict model.
6. **Relationship to `audit_core` (Phase 3).** The merge record and DAG verification
   generalize `verify_chain()` from chain to DAG. The ADR must state whether `audit_core`
   ships chain-only in v1 with the DAG verifier added later, or designs the DAG-ready
   verifier from the start (without building sync).

## 9. Decision needed (human)

The following require owner / licensed-professional sign-off before this becomes an ADR.
None may be resolved by an agent.

- **D-S1 — Structural model.** Accept "per-replica hash chains + merge records (hash-DAG)"
  as the append-only-preserving sync substrate, or direct an alternative. (§5)
- **D-S2 — Authority gate strictness.** Confirm that *any* concurrent edit against an
  APPROVE/CERTIFY MUST block and route to a human (§4.2), and confirm the
  re-certification-revert behavior (§4.3) is acceptable to the certification posture
  (C-1.4, R-2.3, R-2.5). This is a liability-bearing decision.
- **D-S3 — Causality scheme.** Choose vector-version vs logical-clock causality (Open
  Q2) given expected field-replica counts.
- **D-S4 — Privacy/security scope.** Decide whether sync-payload encryption is in this
  ADR or a separate security ADR, and ratify that the firm-sync pipeline is firewalled
  from the learning-flywheel pipeline (§6, C-3.*, R-6).
- **D-S5 — Phasing.** Confirm sync stays **spec-only** through this plan (R-5 / H-18) and
  set the explicit gate ("authority/audit storage stabilizes", per ADR-0009 / R-7.2)
  that must be met before any sync *implementation* phase is scheduled.

## Sources

- `docs/architecture/CONSTRAINTS.md` — C-1.1 (AI never certifies), C-1.2 (append-only
  audit chain), C-1.4 (certified deliverables from Certified Snapshot), C-3.1/C-3.2/C-3.3
  (privacy), C-4.6 (deterministic, no wall-clock).
- `docs/architecture/REQUIREMENTS.md` — R-2.1 (authority metadata), R-2.2 (promotion
  ladder), R-2.3 (human-attributed certification), R-2.4/R-2.5 (Certified Snapshot
  exclusion/derivation), R-2.6 (append-only hash-chained verifiable offline), R-2.8
  (authority domain split), R-4.4 (determinism), R-6.1–R-6.3 (decision-based privacy-
  filtered learning), R-7.1/R-7.2 (local container source of truth, optional sync).
- `docs/architecture/RISK_REGISTER.md` — R-2 (authority-boundary creep), R-5 (sync-service
  scope creep, Open).
- `docs/architecture/ASSUMPTIONS.md` — **A-9** ("The append-only promotion log makes
  multi-user sync tractable via command-log replay", Depends D10; falsification: conflict
  classes replay cannot resolve).
- `docs/adr/ADR-0009-project-container-sync.md` — container + optional sync; conflict
  model open with the command-log-replay lean.
- `docs/adr/ADR-0003-entity-authority-system.md` — Entity Authority System (D1, D20).
- `docs/adr/README.md` — ADR index.
- `research/auracad/audit-chain-design.md` — Phase 3 `audit_core` substrate: SHA-256 JSONL
  chain-of-custody, promotion log, `verify_chain()`, no coordinate/PII leakage
  (CXX_AGENTIC_RULES §5.4), WAL/fsync policy (H-21).
- `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` — §8 H-18 (scope
  creep), §10 Phase 10 (sync conflict model spec-first only).
- `docs/superpowers/specs/2026-06-11-ingeneer-skills-and-tools-design.md` — spec style/tone
  reference.
