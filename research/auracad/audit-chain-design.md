# auracad — Audit Chain Design Extraction

**Status:** Extracted 2026-06-11 (Phase 2.5.2)  
**Stage 3 disposition:** **Promote design → rewrite in `libs/audit_core`** (Phase 3)

## What auracad actually implements

| Component | Path | Behavior |
|-----------|------|----------|
| Command log | `core/audit_log.{h,cpp}` | SQLite `command_log`: seq, cmd_name, cmd_json, timestamp, source. Append-only INSERT. **No per-row hash.** |
| Hash chain | `core/crypto_hash.{h,cpp}` | `CryptoHashChain`: **FNV-1a 64-bit** rolling hash — **not SHA-256** |
| Integration | `core/command_bus.cpp` | Records commands to `AuditLog`; **does not wire** `CryptoHashChain` |

## What InGENeer already has (closer to target)

`orchestrator/src/ingenieer/audit.py` — ported from TOTaLi:

- Append-only JSONL
- SHA-256 per record with `prev_hash` chaining
- `verify_chain()` offline verification

## Target for `audit_core` (Phase 3)

Merge:

- **TOTaLi / InGENeer Python:** SHA-256 JSONL semantics, chain-of-custody
- **auracad:** command naming + JSON payload shape for CAD operations
- **Entity Authority extensions:** promotion log, 9-field entity metadata (req R-2.1), AI-origin certification rejection (C-1.1)

**CXX_AGENTIC_RULES §5.4** (auracad doc): canonical JSONL, SHA-256 matching Python, `fsync`, no coordinate/PII leakage in telemetry chain.

## Do not port as-is

- FNV-64 `CryptoHashChain` — replace with SHA-256
- SQLite-only command log without hash column — extend to hash-chained events + WAL policy (H-21)

## Reference tests

- auracad: `tests/harness/crypto_chain_test.cpp` (FNV — reference only)
- InGENeer: `orchestrator/tests/test_audit.py`, `test_audit_reader.py`
- TOTaLi: `tests/test_audit_hash_chain.py`
