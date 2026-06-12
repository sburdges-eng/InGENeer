# ADR-0027: Baseline Freeze Protocol — SHA-256, Manifests, Versioned Freeze Tags

**Status:** Accepted
**Date:** 2026-06-11
**Deciders:** Owner (recorded verbatim from owner decision; nominal "ADR-012" in the decision text — 0012 was taken, recorded here as 0027)
**Related:** ADR-0023 (oracle discipline), audit_core storage spec §3.1/§8 (open decision 2 — now closed), R-2.6 (offline verifiability)

## Context

Phase 3 froze the audit chain's canonical record format and asked two confirmation
questions: is SHA-256 the frozen chain hash, and how are frozen baselines (oracle
fixtures, canonical formats, feature lists) governed over time? ADR-0023 established
the oracle discipline ("frozen semantics, fixture metadata") for TOTaLi oracles; this
ADR generalizes it into the repo-wide freeze protocol.

## Decision

1. **SHA-256 is confirmed as the frozen hash** for the audit chain, canonical records,
   and all baseline manifests. Changing it is a versioned migration with new fixtures —
   never an edit-in-place.
2. **Freeze manifests.** Every frozen baseline is described by a manifest file
   (`docs/freezes/FREEZE-<name>-<version>.manifest.json`): a sorted list of
   `{path, sha256}` entries for each artifact in the baseline, plus `{name, version,
   date, description, supersedes}`. The manifest's own SHA-256 is the baseline's
   identity.
3. **Versioned freeze tags.** Each recorded freeze gets an annotated git tag
   `freeze/<name>/v<version>` pointing at the commit where the manifest landed.
   Superseding a freeze means a NEW manifest + NEW tag with `supersedes` set — prior
   freezes remain verifiable forever.
4. **What gets frozen** (initial set): the canonical record format + its cross-language
   oracle vectors (audit_core), oracle fixtures extracted per ADR-0023, numeric policy
   constants (geometry_core), and `docs/specs/feature_list.json` baselines.

## Consequences

- Verifying "does today's tree still honor baseline X" is mechanical: hash the manifest's
  paths and compare — offline, no tooling beyond `shasum`.
- A small `tools/scripts/freeze_manifest.py` (generate + verify) is the natural follow-up;
  manifests may be created by hand until then.
- Frozen-semantics changes now have a uniform procedure across the repo: new fixture →
  new manifest → new tag → migration note; CI can later enforce manifest validity.

## Rejected alternatives

- **Implicit freezing via tests only:** tests pin behavior but not artifact identity;
  a manifest names exactly which bytes are the baseline.
- **SHA-3/BLAKE3:** no compatibility ecosystem advantage here outweighs breaking the
  existing Python/TOTaLi SHA-256 chain compatibility already shipped and oracle-pinned.
