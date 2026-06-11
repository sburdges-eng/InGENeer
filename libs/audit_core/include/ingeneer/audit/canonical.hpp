// audit_core — frozen canonical record serialization for the hash chain (spec §3.1).
//
// The pre-hash record is a fixed-key object serialized with sorted keys and Python
// json.dumps(sort_keys=True) DEFAULT separators (", " and ": ", with the single space),
// ASCII, JSON-escaped string values:
//   {"data": <payload_json>, "event": "<type>", "prev_hash": "<hex>",
//    "project_id": "<chain>", "seq": <n>, "timestamp": "<ts>"}
// hash = SHA-256(utf8 bytes of that string), lowercase hex. Chosen to be BYTE-IDENTICAL to
// orchestrator audit.py so a chain written by either language verifies in the other (spec §6).
// These bytes are an ORACLE (ADR-0023): field set, key order, and separators are frozen;
// changing them is a versioned migration with a new fixture, never an edit-in-place.
#ifndef INGENEER_AUDIT_CANONICAL_HPP
#define INGENEER_AUDIT_CANONICAL_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace ingeneer::audit {

// JSON-escape a string value (no surrounding quotes added).
std::string json_escape(std::string_view s);

// Build the canonical pre-hash record string. `payload_json` is embedded verbatim (caller
// guarantees it is itself canonical JSON).
std::string canonical_record(std::int64_t seq, std::string_view timestamp,
                             std::string_view chain_id, std::string_view event_type,
                             std::string_view payload_json, std::string_view prev_hash);

}  // namespace ingeneer::audit

#endif  // INGENEER_AUDIT_CANONICAL_HPP
