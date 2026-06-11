# audit_core + Entity Authority System — Storage Schema Spec

> **Status:** Draft · **Date:** 2026-06-11 · **Phase:** 3.1 · **Handoff Pending Task:** 6
> **Module:** `libs/audit_core` (first foundational C++23 module)
> **Grounding:** ARCHITECTURE.md · REQUIREMENTS R-2.* · CONSTRAINTS C-1.*/C-4.*/C-5.* · ADR-0017 · ADR-0023 · `research/auracad/audit-chain-design.md` · `orchestrator/src/ingenieer/audit.py`

This spec defines the on-disk storage schema and integrity model for `audit_core`, the
module that records every authority-bearing event and enforces the Entity Authority
System at the **storage layer** (not the UI). It is the spec referenced by plan §3.1.
Phases 3.2–3.6 implement against this document.

---

## 0. Scope

In scope:
- The append-only, hash-chained **event store** (chain-of-custody).
- The **entity authority metadata** record (R-2.1, nine fields).
- The **promotion log** modelling `AI_PROPOSED → REVIEWED → APPROVED → CERTIFIED` (R-2.2).
- The **Certified Snapshot** read model (R-2.4/R-2.5).
- The **agent-work chain** — the second chain on shared chaining code ("one schema, two
  chains", plan §2). Same hashing/storage primitives, separate database, never mixed with
  the product/legal chain.

Out of scope (later phases / specs):
- Sync/merge of two chains (Phase 10 — `2026-06-11-sync-conflict-model-spec.md`).
- Plugin access to authority data (Phase 10 ABI spec).
- Geometry payload semantics (`geometry_core`, Phase 5).

## 1. Authority model (restated from requirements — binding)

| Source | Rule |
|---|---|
| C-1.1 | AI never certifies. **No code path** may promote to `CERTIFIED` without a human-attributed action. |
| C-1.2 | Audit chain is append-only. No deletion, no rewrite, no lossy compaction. |
| C-1.3 | Authority semantics live in entity metadata **only** — never in layers/colors/filenames. |
| C-1.4 | Certified deliverables derive only from the Certified Snapshot. |
| R-2.1 | Every entity carries: `EntityID, AuthorityClass, SourceAgent, CreatedBy, CreatedAt, ApprovedBy, ApprovedAt, Confidence, VerificationState`. |
| R-2.2 | Promotion is append-only; never overwrites prior states. |
| R-2.3 | Promotion toward `CERTIFIED` requires a human action attributable to a licensed professional identity. |
| R-2.4 | `AI_PROPOSED` entities are excluded **by the data model** from stamped/legal/certified outputs. |
| R-2.6 | Audit chain is append-only, hash-chained, verifiable **offline**. |

`AuthorityClass` enumeration (storage-canonical, ordered): `AI_PROPOSED(0) < REVIEWED(1) <
APPROVED(2) < CERTIFIED(3)`. Promotion is monotonic non-decreasing; demotion is expressed
as a **new** promotion event to a lower class with a human actor and reason, never an
in-place edit (append-only).

## 2. Storage engine decision

**SQLite, WAL mode, single-writer policy** (plan §3.1, H-21).

- `PRAGMA journal_mode=WAL;` — concurrent readers during a write.
- `PRAGMA synchronous=FULL;` — durability of the chain head matches the Python `fsync`
  discipline (auracad CXX_AGENTIC_RULES §5.4).
- `PRAGMA busy_timeout=5000;` — bounded contention wait.
- `PRAGMA foreign_keys=ON;`
- **Single writer per database connection-set**, enforced in the C++ layer by an
  application-level writer mutex (H-21). Readers are unrestricted.

Rationale: the legacy InGENeer/TOTaLi audit is JSONL; auracad is SQLite without a hash
column (`research/auracad/audit-chain-design.md`). We **merge**: SQLite for queryable
authority state + an embedded hash chain matching the Python SHA-256 semantics so a
record is byte-reproducible and offline-verifiable. JSONL export remains available for
the offline verifier and for cross-language chain-shape compatibility (§6).

## 3. Schemas

Two physically separate databases, identical chaining primitives:
- `product.audit.sqlite` — the legal/product chain (entities, promotions, deliverables).
- `agent.work.sqlite` — the agent-work chain (sessions, consolidation events). **Never**
  written into the product chain (plan §2 "two chains"; C-5.4 keeps agent recipes out of
  production paths).

### 3.1 `event` (the chain — both databases share this shape)

```sql
CREATE TABLE event (
    seq          INTEGER PRIMARY KEY,          -- 1-based, gap-free, monotonic
    ts           TEXT    NOT NULL,             -- RFC3339 UTC, e.g. 2026-06-11T05:30:00.123456+00:00
    chain_id     TEXT    NOT NULL,             -- project_id (product) or session-scope (agent)
    event_type   TEXT    NOT NULL,             -- e.g. ENTITY_CREATED, PROMOTION, SNAPSHOT
    payload_json TEXT    NOT NULL,             -- canonical JSON (sorted keys), domain payload
    prev_hash    TEXT    NOT NULL,             -- 64 hex; genesis = 64 zeros
    hash         TEXT    NOT NULL              -- SHA-256 of the canonical pre-hash record
);
CREATE UNIQUE INDEX ux_event_hash ON event(hash);
```

**Hash definition (frozen — must match `orchestrator/src/ingenieer/audit.py`):**
the pre-hash record is the JSON object
`{"seq","timestamp","project_id"|"chain_id","event","data","prev_hash"}` serialized exactly
as Python `json.dumps(record, sort_keys=True)` does with defaults: separators `", "` /
`": "` (with the single space) and `ensure_ascii=True` (every code point outside
`0x20..0x7E` escaped as `\uXXXX`, surrogate pairs above the BMP);
`hash = SHA256(bytes).hexdigest()`. The stored `hash` column equals that digest.
Cross-language fixtures (§6) pin this — including a non-ASCII vector — so the C++ writer
and the Python verifier agree bit-for-bit. **Genesis** `prev_hash` is 64 `0`s.

> Frozen-semantics note (ADR-0023 discipline): the field set, key order, and encoding of
> the pre-hash record are an **oracle**. Changing them is a versioned migration with a new
> fixture, never an edit-in-place.

### 3.2 `entity` (authority metadata — product chain only, R-2.1)

```sql
CREATE TABLE entity (
    entity_id        TEXT PRIMARY KEY,         -- stable UUID
    authority_class  INTEGER NOT NULL,         -- 0..3 enum; current (projected) class
    source_agent     TEXT,                     -- model/agent id if AI-originated, else NULL
    created_by       TEXT NOT NULL,            -- actor identity (human or agent)
    created_at       TEXT NOT NULL,            -- RFC3339 UTC
    approved_by      TEXT,                      -- licensed-professional identity; NULL until APPROVED+
    approved_at      TEXT,
    confidence       REAL,                      -- [0,1] if AI-originated, else NULL
    verification_state TEXT NOT NULL,           -- UNVERIFIED|VERIFIED|FAILED (R-2.* verification)
    head_seq         INTEGER NOT NULL,          -- seq of the latest promotion event for this entity
    FOREIGN KEY(head_seq) REFERENCES event(seq)
);
```

`entity` is a **projection**: it is rebuildable purely by replaying `promotion` events
(below). The chain is the source of truth (C-1.2); `entity` exists for query speed and is
never authoritative on its own.

### 3.3 `promotion` (append-only state ladder — R-2.2)

```sql
CREATE TABLE promotion (
    seq          INTEGER PRIMARY KEY,           -- == event.seq (the promotion is an event)
    entity_id    TEXT NOT NULL,
    from_class   INTEGER,                        -- NULL on first (creation) record
    to_class     INTEGER NOT NULL,
    actor        TEXT NOT NULL,                  -- who performed the transition
    actor_kind   TEXT NOT NULL,                  -- 'human' | 'agent'  (storage-enforced)
    reason       TEXT,
    FOREIGN KEY(seq) REFERENCES event(seq),
    FOREIGN KEY(entity_id) REFERENCES entity(entity_id)
);
```

**Storage-layer authority guards (enforced in C++ `audit_core`, tested in 3.3):**
1. `to_class = CERTIFIED` **rejected** unless `actor_kind = 'human'` **and** `actor`
   resolves to a licensed-professional identity (C-1.1, R-2.3). An AI-origin certification
   attempt is a hard error at the storage API, not a UI check.
2. `to_class = APPROVED` likewise requires `actor_kind = 'human'` (R-2.3 "human action
   attributable to a licensed professional").
3. Any promotion must reference the entity's current `head_seq` as its causal parent
   (optimistic-concurrency check) — prevents lost updates under the single-writer policy.
4. `from_class` must equal the entity's projected class at insert time; mismatch → reject.
5. No row is ever UPDATEd or DELETEd (append-only; enforced by triggers + API).

```sql
-- Hard append-only enforcement (defense in depth beyond the API)
CREATE TRIGGER no_update_event BEFORE UPDATE ON event
  BEGIN SELECT RAISE(ABORT, 'event is append-only'); END;
CREATE TRIGGER no_delete_event BEFORE DELETE ON event
  BEGIN SELECT RAISE(ABORT, 'event is append-only'); END;
-- (identical triggers on promotion)
```

### 3.4 Certified Snapshot (read model — R-2.4/R-2.5, C-1.4)

The Certified Snapshot is a **filtered query**, not a separate mutable store:

```sql
CREATE VIEW certified_snapshot AS
  SELECT * FROM entity WHERE authority_class = 3 /* CERTIFIED */;
```

Generation of any stamped/legal/certified deliverable reads **only** from
`certified_snapshot`. `AI_PROPOSED` (and `REVIEWED`/`APPROVED`) entities are excluded by
this data-model filter, satisfying R-2.4 without relying on UI. A snapshot
**materializer** that emits an immutable, hash-stamped export (its own genesis record) for
reproducible deliverables — recording a `SNAPSHOT` event capturing the set of entity
hashes included — is **DEFERRED past Phase 3 exit** (evaluator note N2, 2026-06-11): no
deliverable pipeline exists yet to consume it. Target: the first phase that produces a
stamped deliverable (Phase 9 ai_core integration or earlier if needed).

## 4. Agent-work chain (plan §2 / §3.5)

`agent.work.sqlite` reuses `event` (§3.1) with `event_type` in
{`SESSION_START`, `SESSION_END`, `FEATURE_RESULT`, `CONSOLIDATION`, `HANDOFF`}. It has **no**
`entity`/`promotion` tables — agents never carry product authority. Consolidation job MVP
(plan §3.5): `extract → append CONSOLIDATION event → update handoff`. Note-linking /
recipe-promotion sit behind a feature flag, outside production paths (C-5.4). The two
chains share only the hashing/append code, validated by the chain-shape compatibility test
(§6).

## 5. Public C++ API surface (engine rules)

`audit_core` is C++23, UI-free, Apple-framework-free (C-4.1); no OCCT types in its public
API (C-4.3). Error handling is `std::expected<T, AuditError>` (plan §3.3) — **no exceptions
across any future `extern "C"` boundary** (C-4.5). Sketch (signatures only — implemented in
3.2–3.4):

```cpp
namespace ingeneer::audit {
  enum class AuthorityClass : int { AiProposed=0, Reviewed=1, Approved=2, Certified=3 };
  enum class ActorKind { Human, Agent };

  struct AppendResult { std::int64_t seq; std::string hash; };

  // append one event; computes hash over canonical record; updates chain head
  std::expected<AppendResult, AuditError> append(const Event&);

  // promotion with storage-layer authority guards (§3.3); rejects AI-origin CERTIFY
  std::expected<AppendResult, AuditError> promote(const PromotionRequest&);

  // offline chain verification (R-2.6): recompute every hash, check prev linkage
  std::expected<void, AuditError> verify_chain() const;

  // Certified Snapshot read (R-2.4/2.5)
  std::expected<std::vector<Entity>, AuditError> certified_snapshot() const;
}
```

`AuditError` carries a code enum (e.g. `ChainBroken`, `AuthorityViolation`,
`ConcurrencyConflict`, `Io`) + message; never an exception type.

Determinism (C-4.6): hashing and verification are pure over their inputs; timestamps are
injected (not read from wall-clock inside the hashing path) so fixtures are reproducible.

## 6. Verification & test obligations (Phase 3.2–3.6 exit)

- **Chain-shape compatibility test**: a C++-written chain exports to JSONL and
  `orchestrator` Python `AuditLogger.verify_chain()` validates it, and vice-versa — pins
  the frozen hash semantics (§3.1) cross-language.
- **Property tests (3.3)**: *no path promotes to APPROVED/CERTIFIED without a human-
  attributed actor*; *append-only — no event/promotion is ever mutated*; *projection
  rebuild from events equals the stored `entity` table*.
- **Offline verify (3.2)**: tamper one byte → `verify_chain()` reports `ChainBroken` at the
  exact seq.
- **Snapshot filter (3.4)**: deliverable generation over a mixed authority set yields only
  CERTIFIED entities.
- **Fuzz (3.6)**: libFuzzer target over the container/record reader; ASan/UBSan green.
- **Concurrency (H-21)**: two writers → second blocks/serialises; `ConcurrencyConflict` on
  stale `head_seq`.

## 7. Open questions (do not block 3.2–3.4)

- Licensed-professional identity resolution: where does `audit_core` get the
  human-licensee registry? (Proposed: opaque verified-identity token passed in by the
  caller; `audit_core` validates a signature, does not own identity.) → candidate ADR.
- Snapshot export container format (JSONL vs SQLite attach) — pick in 3.4.
- Multi-project (multiple `chain_id`) in one DB vs one DB per project. (Proposed: one DB
  per project; `chain_id` retained for forward compat.)

## 8. Decision needed (human)

1. Confirm the **licensed-professional identity** mechanism boundary (above) before 3.3 —
   this touches R-2.3 legal attribution and may warrant an ADR.
2. Confirm SHA-256 (vs SHA-3/BLAKE3) is the frozen chain hash — current grounding
   (`audit.py`, TOTaLi) is SHA-256; this spec freezes SHA-256. Changing later is a
   versioned migration.

## Sources

- `docs/architecture/REQUIREMENTS.md` (R-2.1–R-2.8, R-5.4)
- `docs/architecture/CONSTRAINTS.md` (C-1.1–C-1.4, C-4.1/4.3/4.5/4.6, C-5.2/5.4)
- `docs/adr/README.md` — ADR-0017 (Entity Authority / flywheel), ADR-0023 (oracle discipline)
- `research/auracad/audit-chain-design.md` (Phase 2.5.2 extraction; merge target)
- `orchestrator/src/ingenieer/audit.py` (SHA-256 JSONL chain — frozen reference semantics)
- `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` §2, §3.1–3.6
