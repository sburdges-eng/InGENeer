#include "ingeneer/audit/canonical.hpp"

#include <cstdio>

namespace ingeneer::audit {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
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
