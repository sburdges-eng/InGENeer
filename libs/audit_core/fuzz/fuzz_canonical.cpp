// Phase 3.6 — fuzz target for the record-encoding surface (canonical record + escaping +
// hashing). Compiled two ways: (1) with libFuzzer (-fsanitize=fuzzer) on a capable toolchain
// / Linux CI; (2) with the deterministic standalone driver (standalone_main.cpp) under
// ASan/UBSan, which runs today even though Apple clang ships no libFuzzer runtime.
//
// Invariant under ANY input bytes: no crash / UB / OOB, and the output is always 64 lowercase
// hex chars. ASan/UBSan turn a violation into a test failure.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ingeneer/audit/canonical.hpp"
#include "ingeneer/audit/sha256.hpp"

using namespace ingeneer::audit;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string_view in(reinterpret_cast<const char*>(data), size);

    // The arbitrary-bytes escaping surface must never crash.
    const std::string escaped = json_escape(in);

    // Split the input across the record's string fields; payload stays structurally "{}".
    const std::size_t q = size / 4;
    std::string_view ev = in.substr(0, q);
    std::string_view chain = in.substr(q, q);
    std::string_view ts = in.substr(2 * q, q);
    std::string_view prev = in.substr(3 * q);

    const std::string rec =
        canonical_record(static_cast<std::int64_t>(size), ts, chain, ev, "{}", prev);
    const std::string h = sha256_hex(rec);

    if (h.size() != 64) std::abort();
    for (char c : h) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) std::abort();
    }
    // Hashing the escaped form too (exercises larger buffers).
    if (sha256_hex(escaped).size() != 64) std::abort();
    return 0;
}
