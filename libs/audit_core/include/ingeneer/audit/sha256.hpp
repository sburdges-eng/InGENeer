// audit_core — SHA-256 (FIPS 180-4), self-contained, no external crypto dependency.
//
// Rationale (CONSTRAINTS C-4.1): engines must be Apple-framework-free and UI-free, so we
// cannot use CommonCrypto. To stay permissive-license-clean (C-2.1) and dependency-light
// we implement SHA-256 from the published FIPS 180-4 spec rather than linking OpenSSL.
// The chain hash semantics are frozen (ADR-0023 oracle discipline); see the storage spec.
#ifndef INGENEER_AUDIT_SHA256_HPP
#define INGENEER_AUDIT_SHA256_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ingeneer::audit {

// Lowercase 64-char hex SHA-256 of the given bytes.
std::string sha256_hex(std::string_view data);

// 32 raw digest bytes.
std::array<std::uint8_t, 32> sha256_raw(std::string_view data);

}  // namespace ingeneer::audit

#endif  // INGENEER_AUDIT_SHA256_HPP
