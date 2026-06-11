// audit_core — project-container record reader (plan §9 item 3.6) and JSONL chain
// verification for the Python AuditLogger interchange format (spec §6).
//
// The reader parses one JSON value per line while PRESERVING raw scalar tokens (numbers,
// strings, literals are kept as their original bytes). Canonical re-emission only re-orders
// object keys (recursively, sorted by DECODED key string — exactly Python's
// json.dumps(sort_keys=True)) and normalizes separators to ", " / ": ". Because scalar
// bytes are never reformatted, a record written by Python re-hashes to identical bytes
// without any float-formatting hazard.
#ifndef INGENEER_AUDIT_JSONL_HPP
#define INGENEER_AUDIT_JSONL_HPP

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "ingeneer/audit/error.hpp"

namespace ingeneer::audit {

// Decode a JSON string *body* (no surrounding quotes): standard escapes plus \uXXXX with
// UTF-16 surrogate pairs -> UTF-8. Returns nullopt-like failure via expected.
std::expected<std::string, AuditError> json_unescape(std::string_view body);

// Parse one JSON value and re-emit it canonically (sorted keys, ", "/": " separators, raw
// scalar tokens verbatim). Fails on malformed JSON or trailing garbage.
std::expected<std::string, AuditError> canonicalize_json(std::string_view text);

// True iff a number token is byte-identical to what Python json.dumps would re-emit after
// json.loads (ints: minimal form, no "-0"; floats: CPython shortest repr formatting).
// Evaluator finding N1: a token like "1.50" hashes fine in C++ but Python re-dumps "1.5",
// silently breaking cross-language verification — so non-fixed-point tokens are rejected
// at append time.
bool is_python_number_token(std::string_view token);

// Walk a JSON document and fail if any number token is not a Python re-dump fixed point.
std::expected<void, AuditError> validate_python_number_tokens(std::string_view text);

// One parsed chain record from a JSONL line (Python AuditLogger format).
struct JsonlRecord {
    std::int64_t seq = 0;
    std::string timestamp;       // decoded
    std::string project_id;      // decoded
    std::string event;           // decoded
    std::string data_canonical;  // canonicalized raw JSON subtree
    std::string prev_hash;       // decoded
    std::string hash;            // decoded
};

// Parse one JSONL line into a record. Top-level must be an object containing exactly the
// audit fields (extra keys are rejected — frozen record shape, ADR-0023).
std::expected<JsonlRecord, AuditError> parse_jsonl_record(std::string_view line);

// Verify a full JSONL chain file: per-record SHA-256 recomputation over the canonical
// pre-hash bytes, prev_hash linkage, gap-free seq. Offline (R-2.6).
std::expected<void, AuditError> verify_jsonl_chain(const std::string& path);

}  // namespace ingeneer::audit

#endif  // INGENEER_AUDIT_JSONL_HPP
