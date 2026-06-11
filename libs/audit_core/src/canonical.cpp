#include "ingeneer/audit/canonical.hpp"

#include <cstdio>

namespace ingeneer::audit {
namespace {

void append_u_escape(std::string& out, std::uint32_t cp) {
    char buf[7];
    std::snprintf(buf, sizeof(buf), "\\u%04x", cp);
    out += buf;
}

// Append one Unicode code point as Python json.dumps(ensure_ascii=True) would:
// BMP -> \uXXXX (lowercase hex); astral -> UTF-16 surrogate pair \uD8xx\uDCxx.
void append_codepoint_escape(std::string& out, std::uint32_t cp) {
    if (cp >= 0x10000) {
        cp -= 0x10000;
        append_u_escape(out, 0xD800 + (cp >> 10));
        append_u_escape(out, 0xDC00 + (cp & 0x3FF));
    } else {
        append_u_escape(out, cp);
    }
}

}  // namespace

std::string json_escape(std::string_view s) {
    // Frozen parity contract (spec §3.1 / §6): output must be byte-identical to Python
    // json.dumps(<str>)[1:-1] with default ensure_ascii=True. That means:
    //   - '"' and '\\' escaped; \b \f \n \r \t shortcuts; other C0 controls -> \u00xx
    //   - every code point outside 0x20..0x7E -> \uXXXX (surrogate pairs above the BMP)
    // Input is expected to be valid UTF-8 (Python str payloads always are). Invalid UTF-8
    // bytes are mapped deterministically to U+FFFD so arbitrary bytes (fuzzing) never
    // crash and never silently pass through unescaped.
    std::string out;
    out.reserve(s.size() + 8);
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == '"') {
            out += "\\\"";
            ++i;
        } else if (c == '\\') {
            out += "\\\\";
            ++i;
        } else if (c == '\b') {
            out += "\\b";
            ++i;
        } else if (c == '\f') {
            out += "\\f";
            ++i;
        } else if (c == '\n') {
            out += "\\n";
            ++i;
        } else if (c == '\r') {
            out += "\\r";
            ++i;
        } else if (c == '\t') {
            out += "\\t";
            ++i;
        } else if (c >= 0x20 && c <= 0x7E) {
            out += static_cast<char>(c);
            ++i;
        } else if (c < 0x20 || c == 0x7F) {
            append_u_escape(out, c);
            ++i;
        } else {
            // Decode one UTF-8 sequence; on malformed input emit U+FFFD for the lead byte.
            std::uint32_t cp = 0xFFFD;
            std::size_t len = 1;
            if ((c & 0xE0) == 0xC0 && i + 1 < n) {
                const auto c1 = static_cast<unsigned char>(s[i + 1]);
                if ((c1 & 0xC0) == 0x80) {
                    const std::uint32_t v =
                        (static_cast<std::uint32_t>(c & 0x1Fu) << 6) | (c1 & 0x3Fu);
                    if (v >= 0x80) {  // reject overlong
                        cp = v;
                        len = 2;
                    }
                }
            } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
                const auto c1 = static_cast<unsigned char>(s[i + 1]);
                const auto c2 = static_cast<unsigned char>(s[i + 2]);
                if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
                    const std::uint32_t v = (static_cast<std::uint32_t>(c & 0x0Fu) << 12) |
                                            (static_cast<std::uint32_t>(c1 & 0x3Fu) << 6) |
                                            (c2 & 0x3Fu);
                    // Reject overlong and lone surrogates.
                    if (v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF)) {
                        cp = v;
                        len = 3;
                    }
                }
            } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
                const auto c1 = static_cast<unsigned char>(s[i + 1]);
                const auto c2 = static_cast<unsigned char>(s[i + 2]);
                const auto c3 = static_cast<unsigned char>(s[i + 3]);
                if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                    const std::uint32_t v = (static_cast<std::uint32_t>(c & 0x07u) << 18) |
                                            (static_cast<std::uint32_t>(c1 & 0x3Fu) << 12) |
                                            (static_cast<std::uint32_t>(c2 & 0x3Fu) << 6) |
                                            (c3 & 0x3Fu);
                    if (v >= 0x10000 && v <= 0x10FFFF) {  // reject overlong/out-of-range
                        cp = v;
                        len = 4;
                    }
                }
            }
            append_codepoint_escape(out, cp);
            i += len;
        }
    }
    return out;
}

std::string canonical_record(std::int64_t seq, std::string_view timestamp,
                             std::string_view chain_id, std::string_view event_type,
                             std::string_view payload_json, std::string_view prev_hash) {
    // Separators match Python json.dumps(sort_keys=True) default: ", " and ": " (with the
    // single space). This makes the chain byte-identical to orchestrator audit.py (spec §6).
    std::string out;
    out.reserve(payload_json.size() + timestamp.size() + chain_id.size() + 128);
    out += "{\"data\": ";
    out += payload_json;  // embedded verbatim (caller-canonical, same separators)
    out += ", \"event\": \"";
    out += json_escape(event_type);
    out += "\", \"prev_hash\": \"";
    out += json_escape(prev_hash);
    out += "\", \"project_id\": \"";
    out += json_escape(chain_id);
    out += "\", \"seq\": ";
    out += std::to_string(seq);
    out += ", \"timestamp\": \"";
    out += json_escape(timestamp);
    out += "\"}";
    return out;
}

}  // namespace ingeneer::audit
