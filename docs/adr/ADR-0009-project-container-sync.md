# ADR-0009 — Project Container + Optional Sync Service

**Status:** Accepted (D10). Conflict model open (lean: command-log replay over the append-only promotion log).

## Decision
A project is a single self-contained local container: SQLite-backed entity/audit store + binary sidecars for point clouds and surfaces. Openable, verifiable, and certifiable offline. An optional sync service (PostGIS or object store) layers multi-user support for firms; the local container remains the source of truth; enterprise self-hosting must be possible.

## Rejected
- Server-first (PostGIS): conflicts with local-first, offline field work, and small-firm simplicity.
- Container-only: blocks the enterprise segment.

## Consequences
Container format is a public, documented part of the open Core; sync design deferred until after authority/audit storage stabilizes; A-9 assumes append-only logs make replay-based sync tractable.
