// audit_core — error type. No exceptions cross any future extern "C" boundary (C-4.5);
// the public API returns std::expected<T, AuditError> (plan §3.3).
#ifndef INGENEER_AUDIT_ERROR_HPP
#define INGENEER_AUDIT_ERROR_HPP

#include <string>

namespace ingeneer::audit {

enum class AuditErrc {
    Ok = 0,
    Io,                   // file/SQLite I/O failure
    ChainBroken,          // hash recomputation or prev linkage mismatch
    AuthorityViolation,   // AI-origin certify/approve, or unauthorized class transition
    ConcurrencyConflict,  // stale head_seq (optimistic concurrency)
    NotFound,             // entity/record missing
    InvalidArgument,      // malformed request
};

struct AuditError {
    AuditErrc code = AuditErrc::Ok;
    std::string message;

    AuditError() = default;
    AuditError(AuditErrc c, std::string m) : code(c), message(std::move(m)) {}
};

}  // namespace ingeneer::audit

#endif  // INGENEER_AUDIT_ERROR_HPP
