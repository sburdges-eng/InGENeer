# ADR-0026: Licensed Professional Identity — Cryptographic Signatures, Offline-Capable

**Status:** Accepted
**Date:** 2026-06-11
**Deciders:** Owner (recorded verbatim from owner decision; nominal "ADR-011" in the decision text — 0011 was taken, recorded here as 0026)
**Related:** C-1.1 (AI never certifies), R-2.3 (human-attributed promotion), ADR-0003 (Entity Authority System), audit_core storage spec §7/§8 (open decision 1 — now closed)

## Context

The Entity Authority System requires that promotion toward `CERTIFIED` be "a human action
attributable to a licensed professional identity" (R-2.3). Phase 3 implemented the
storage-layer guard (`actor_kind = human` enforced at the database), but deferred *how* a
human actor proves they are a licensed professional. Options considered: opaque trusted
strings (status quo — attribution by convention only), an online state-board license
verification API, or cryptographic identity.

A licensing-board API dependency was evaluated and rejected as a core dependency: it makes
certification require internet connectivity, couples legal workflows to third-party uptime,
and varies per jurisdiction.

## Decision

1. **Cryptographic identity system.** Each licensed professional holds a signing keypair.
   Authority promotions to `APPROVED`/`CERTIFIED` **require a digital signature** by the
   professional's private key over the promotion's canonical record bytes (the same frozen
   canonicalization the audit chain hashes — spec §3.1).
2. **Offline operation is supported and mandatory-capable.** Signature creation and
   verification MUST work with no network access (C-3.* local-first posture; field use).
   The public-key registry is a local, append-only, chain-recorded artifact.
3. **Board verification is optional, layered, and never blocking.** External license
   validation (State Board → API → Validation) is **future work**: an optional enrichment
   that can attest "key X belongs to license Y, active on date Z" and record that
   attestation in the chain. Certification MUST NOT require it. **Do not require internet
   for certification.**

## Consequences

- `audit_core` gains (future phase, before first certified deliverable): a `signature`
  field on promotion records to `APPROVED`/`CERTIFIED`; storage-layer rejection of
  unsigned/invalid-signature promotions; a key-registry table (key id → public key →
  holder identity), itself chain-recorded and append-only.
- Algorithm selection (Ed25519 is the working candidate: small, deterministic, permissive
  vendorable implementations) is an implementation decision for that phase — it must keep
  the open Core dependency-light and license-clean (C-2.1), and the signature bytes enter
  the canonical record, so the choice freezes once shipped (ADR-0027 protocol applies).
- Key lifecycle (issuance, revocation, expiry, rotation) is spec work for the same phase;
  revocation must be expressible append-only (a revocation record, never deletion).
- The verify-offline property (R-2.6) now extends to signatures: a chain plus the key
  registry is verifiable on an air-gapped machine.

## Rejected alternatives

- **Trusted-string attribution (status quo):** no cryptographic accountability; fails the
  defensibility bar for stamped deliverables.
- **Online board-API verification as a certification requirement:** internet dependency in
  a legal-critical path; jurisdiction-specific; third-party availability coupling. Kept
  only as the optional future attestation layer described above.
