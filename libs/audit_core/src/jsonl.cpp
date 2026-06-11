#include "ingeneer/audit/jsonl.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <memory>
#include <utility>

#include "ingeneer/audit/canonical.hpp"
#include "ingeneer/audit/sha256.hpp"

namespace ingeneer::audit {
namespace {

AuditError bad(std::string msg) { return AuditError(AuditErrc::InvalidArgument, std::move(msg)); }

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Length of a valid UTF-8 sequence starting at s[i] (lead byte >= 0x80), or 0 if invalid
// (overlong, surrogate, out of range, truncated). Python's json.loads rejects invalid
// UTF-8 input, so the reader must too — otherwise re-escaping (U+FFFD) breaks losslessness.
std::size_t utf8_seq_len(std::string_view s, std::size_t i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    auto cont = [&](std::size_t k) {
        return k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80;
    };
    if ((b0 & 0xE0) == 0xC0) {
        if (!cont(i + 1)) return 0;
        const std::uint32_t v = (static_cast<std::uint32_t>(b0 & 0x1Fu) << 6) |
                                (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        return v >= 0x80 ? 2 : 0;  // reject overlong
    }
    if ((b0 & 0xF0) == 0xE0) {
        if (!cont(i + 1) || !cont(i + 2)) return 0;
        const std::uint32_t v = (static_cast<std::uint32_t>(b0 & 0x0Fu) << 12) |
                                ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                                (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        return (v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF)) ? 3 : 0;
    }
    if ((b0 & 0xF8) == 0xF0) {
        if (!cont(i + 1) || !cont(i + 2) || !cont(i + 3)) return 0;
        const std::uint32_t v = (static_cast<std::uint32_t>(b0 & 0x07u) << 18) |
                                ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                                ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
                                (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        return (v >= 0x10000 && v <= 0x10FFFF) ? 4 : 0;
    }
    return 0;  // lone continuation byte or invalid lead
}

// --- raw-token JSON tree -------------------------------------------------------------

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    enum class Kind { Object, Array, Scalar } kind = Kind::Scalar;
    // Object: raw (still-escaped) key token bodies paired with children, original order.
    std::vector<std::pair<std::string, NodePtr>> members;
    std::vector<NodePtr> elements;  // Array
    std::string raw;                // Scalar: verbatim token bytes (incl. quotes for strings)
};

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    std::expected<NodePtr, AuditError> parse_document() {
        skip_ws();
        auto v = parse_value(0);
        if (!v) return v;
        skip_ws();
        if (pos_ != s_.size()) return std::unexpected(bad("trailing garbage after JSON value"));
        return v;
    }

private:
    static constexpr int kMaxDepth = 64;  // bounded recursion (fuzz safety)

    std::string_view s_;
    std::size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool eof() const { return pos_ >= s_.size(); }

    std::expected<NodePtr, AuditError> parse_value(int depth) {
        if (depth > kMaxDepth) return std::unexpected(bad("nesting too deep"));
        if (eof()) return std::unexpected(bad("unexpected end of input"));
        const char c = s_[pos_];
        if (c == '{') return parse_object(depth);
        if (c == '[') return parse_array(depth);
        if (c == '"') return parse_string_node();
        return parse_literal_or_number();
    }

    std::expected<NodePtr, AuditError> parse_object(int depth) {
        auto node = std::make_unique<Node>();
        node->kind = Node::Kind::Object;
        ++pos_;  // '{'
        skip_ws();
        if (!eof() && s_[pos_] == '}') {
            ++pos_;
            return node;
        }
        while (true) {
            skip_ws();
            if (eof() || s_[pos_] != '"') return std::unexpected(bad("expected object key"));
            auto key = scan_string_token();
            if (!key) return std::unexpected(key.error());
            skip_ws();
            if (eof() || s_[pos_] != ':') return std::unexpected(bad("expected ':'"));
            ++pos_;
            skip_ws();
            auto val = parse_value(depth + 1);
            if (!val) return val;
            // strip surrounding quotes from the key token -> raw body
            node->members.emplace_back(key->substr(1, key->size() - 2), std::move(*val));
            skip_ws();
            if (eof()) return std::unexpected(bad("unterminated object"));
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == '}') {
                ++pos_;
                return node;
            }
            return std::unexpected(bad("expected ',' or '}'"));
        }
    }

    std::expected<NodePtr, AuditError> parse_array(int depth) {
        auto node = std::make_unique<Node>();
        node->kind = Node::Kind::Array;
        ++pos_;  // '['
        skip_ws();
        if (!eof() && s_[pos_] == ']') {
            ++pos_;
            return node;
        }
        while (true) {
            skip_ws();
            auto val = parse_value(depth + 1);
            if (!val) return val;
            node->elements.push_back(std::move(*val));
            skip_ws();
            if (eof()) return std::unexpected(bad("unterminated array"));
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == ']') {
                ++pos_;
                return node;
            }
            return std::unexpected(bad("expected ',' or ']'"));
        }
    }

    std::expected<NodePtr, AuditError> parse_string_node() {
        auto tok = scan_string_token();
        if (!tok) return std::unexpected(tok.error());
        auto node = std::make_unique<Node>();
        node->kind = Node::Kind::Scalar;
        node->raw = std::move(*tok);
        return node;
    }

    // Scan a complete string token including quotes; validates escape shapes.
    std::expected<std::string, AuditError> scan_string_token() {
        const std::size_t start = pos_;
        ++pos_;  // opening quote
        while (true) {
            if (eof()) return std::unexpected(bad("unterminated string"));
            const char c = s_[pos_];
            if (c == '"') {
                ++pos_;
                return std::string(s_.substr(start, pos_ - start));
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return std::unexpected(bad("raw control character in string"));
            }
            if (c == '\\') {
                if (pos_ + 1 >= s_.size()) return std::unexpected(bad("dangling escape"));
                const char e = s_[pos_ + 1];
                if (e == 'u') {
                    if (pos_ + 5 >= s_.size()) return std::unexpected(bad("short \\u escape"));
                    for (std::size_t k = pos_ + 2; k < pos_ + 6; ++k) {
                        if (hex_val(s_[k]) < 0) return std::unexpected(bad("bad \\u escape"));
                    }
                    pos_ += 6;
                } else if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' || e == 'n' ||
                           e == 'r' || e == 't') {
                    pos_ += 2;
                } else {
                    return std::unexpected(bad("invalid escape"));
                }
            } else if (static_cast<unsigned char>(c) >= 0x80) {
                // Raw multibyte content must be valid UTF-8 — Python json.loads rejects
                // invalid input, and re-escaping must stay lossless (libFuzzer finding:
                // stray continuation bytes survived to json_escape, which maps them to
                // U+FFFD, silently changing the round-tripped bytes).
                const std::size_t len = utf8_seq_len(s_, pos_);
                if (len == 0) return std::unexpected(bad("invalid UTF-8 in string"));
                pos_ += len;
            } else {
                ++pos_;
            }
        }
    }

    std::expected<NodePtr, AuditError> parse_literal_or_number() {
        const std::size_t start = pos_;
        // literals (incl. Python's non-standard NaN/Infinity emissions)
        for (std::string_view lit : {std::string_view("true"), std::string_view("false"),
                                     std::string_view("null"), std::string_view("NaN"),
                                     std::string_view("Infinity"), std::string_view("-Infinity")}) {
            if (s_.substr(pos_, lit.size()) == lit) {
                pos_ += lit.size();
                auto node = std::make_unique<Node>();
                node->raw = std::string(lit);
                return node;
            }
        }
        // number: -?digits(.digits)?([eE][+-]?digits)?
        std::size_t p = pos_;
        if (p < s_.size() && s_[p] == '-') ++p;
        const std::size_t int_start = p;
        while (p < s_.size() && s_[p] >= '0' && s_[p] <= '9') ++p;
        if (p == int_start) return std::unexpected(bad("invalid token"));
        if (p < s_.size() && s_[p] == '.') {
            ++p;
            const std::size_t frac = p;
            while (p < s_.size() && s_[p] >= '0' && s_[p] <= '9') ++p;
            if (p == frac) return std::unexpected(bad("invalid number fraction"));
        }
        if (p < s_.size() && (s_[p] == 'e' || s_[p] == 'E')) {
            ++p;
            if (p < s_.size() && (s_[p] == '+' || s_[p] == '-')) ++p;
            const std::size_t exp = p;
            while (p < s_.size() && s_[p] >= '0' && s_[p] <= '9') ++p;
            if (p == exp) return std::unexpected(bad("invalid number exponent"));
        }
        auto node = std::make_unique<Node>();
        node->raw = std::string(s_.substr(start, p - start));
        pos_ = p;
        return node;
    }
};

// Canonical emission: objects sorted by DECODED key (Python sorts str by code points,
// which equals UTF-8 byte order), separators ", " / ": ", scalar tokens verbatim.
std::expected<void, AuditError> emit_canonical(const Node& n, std::string& out) {
    switch (n.kind) {
        case Node::Kind::Scalar:
            out += n.raw;
            return {};
        case Node::Kind::Array: {
            out += '[';
            for (std::size_t i = 0; i < n.elements.size(); ++i) {
                if (i != 0) out += ", ";
                auto r = emit_canonical(*n.elements[i], out);
                if (!r) return r;
            }
            out += ']';
            return {};
        }
        case Node::Kind::Object: {
            // decode keys once for ordering; emit the raw key bytes
            std::vector<std::pair<std::string, const std::pair<std::string, NodePtr>*>> order;
            order.reserve(n.members.size());
            for (const auto& m : n.members) {
                auto dec = json_unescape(m.first);
                if (!dec) return std::unexpected(dec.error());
                order.emplace_back(std::move(*dec), &m);
            }
            std::stable_sort(order.begin(), order.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            out += '{';
            for (std::size_t i = 0; i < order.size(); ++i) {
                if (i != 0) out += ", ";
                out += '"';
                out += order[i].second->first;
                out += "\": ";
                auto r = emit_canonical(*order[i].second->second, out);
                if (!r) return r;
            }
            out += '}';
            return {};
        }
    }
    return std::unexpected(bad("unreachable node kind"));
}

}  // namespace

std::expected<std::string, AuditError> json_unescape(std::string_view body) {
    std::string out;
    out.reserve(body.size());
    std::size_t i = 0;
    while (i < body.size()) {
        const char c = body[i];
        if (c != '\\') {
            out += c;
            ++i;
            continue;
        }
        if (i + 1 >= body.size()) return std::unexpected(bad("dangling escape"));
        const char e = body[i + 1];
        switch (e) {
            case '"':
                out += '"';
                i += 2;
                break;
            case '\\':
                out += '\\';
                i += 2;
                break;
            case '/':
                out += '/';
                i += 2;
                break;
            case 'b':
                out += '\b';
                i += 2;
                break;
            case 'f':
                out += '\f';
                i += 2;
                break;
            case 'n':
                out += '\n';
                i += 2;
                break;
            case 'r':
                out += '\r';
                i += 2;
                break;
            case 't':
                out += '\t';
                i += 2;
                break;
            case 'u': {
                if (i + 6 > body.size()) return std::unexpected(bad("short \\u escape"));
                std::uint32_t cp = 0;
                for (std::size_t k = i + 2; k < i + 6; ++k) {
                    const int v = hex_val(body[k]);
                    if (v < 0) return std::unexpected(bad("bad \\u escape"));
                    cp = (cp << 4) | static_cast<std::uint32_t>(v);
                }
                i += 6;
                if (cp >= 0xD800 && cp <= 0xDBFF) {  // high surrogate: need the pair
                    if (i + 6 > body.size() || body[i] != '\\' || body[i + 1] != 'u') {
                        return std::unexpected(bad("lone high surrogate"));
                    }
                    std::uint32_t lo = 0;
                    for (std::size_t k = i + 2; k < i + 6; ++k) {
                        const int v = hex_val(body[k]);
                        if (v < 0) return std::unexpected(bad("bad \\u escape"));
                        lo = (lo << 4) | static_cast<std::uint32_t>(v);
                    }
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        return std::unexpected(bad("invalid low surrogate"));
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return std::unexpected(bad("lone low surrogate"));
                }
                append_utf8(out, cp);
                break;
            }
            default:
                return std::unexpected(bad("invalid escape"));
        }
    }
    return out;
}

namespace {

// CPython float repr formatting over the shortest round-trip digits (which std::to_chars
// scientific also produces): fixed form when -4 < decpt <= 16, else "d.ddde±XX" with a
// mandatory sign and >= 2 exponent digits; integral floats render as "N.0".
std::string python_float_repr(double d) {
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::scientific);
    std::string sci(buf, res.ptr);

    std::string sign;
    std::size_t i = 0;
    if (!sci.empty() && sci[0] == '-') {
        sign = "-";
        i = 1;
    }
    const std::size_t e = sci.find('e', i);
    std::string digits = sci.substr(i, e - i);
    const std::size_t dot = digits.find('.');
    if (dot != std::string::npos) digits.erase(dot, 1);
    const int exp10 = std::stoi(sci.substr(e + 1));
    const int decpt = exp10 + 1;  // digits before the decimal point

    std::string out = sign;
    if (decpt > -4 && decpt <= 16) {
        if (decpt <= 0) {
            out += "0.";
            out.append(static_cast<std::size_t>(-decpt), '0');
            out += digits;
        } else if (static_cast<std::size_t>(decpt) >= digits.size()) {
            out += digits;
            out.append(static_cast<std::size_t>(decpt) - digits.size(), '0');
            out += ".0";
        } else {
            out += digits.substr(0, static_cast<std::size_t>(decpt));
            out += '.';
            out += digits.substr(static_cast<std::size_t>(decpt));
        }
    } else {
        out += digits[0];
        if (digits.size() > 1) {
            out += '.';
            out += digits.substr(1);
        }
        out += 'e';
        out += exp10 < 0 ? '-' : '+';
        const int mag = exp10 < 0 ? -exp10 : exp10;
        if (mag < 10) out += '0';
        out += std::to_string(mag);
    }
    return out;
}

std::expected<void, AuditError> walk_numbers(const Node& n) {
    switch (n.kind) {
        case Node::Kind::Scalar: {
            const char c0 = n.raw.empty() ? '\0' : n.raw[0];
            const bool is_number = c0 == '-'
                                       ? (n.raw.size() > 1 && n.raw[1] >= '0' && n.raw[1] <= '9')
                                       : (c0 >= '0' && c0 <= '9');
            if (is_number && !is_python_number_token(n.raw)) {
                return std::unexpected(
                    bad("number token '" + n.raw + "' is not a Python re-dump fixed point"));
            }
            return {};
        }
        case Node::Kind::Array:
            for (const auto& el : n.elements) {
                if (auto r = walk_numbers(*el); !r) return r;
            }
            return {};
        case Node::Kind::Object:
            for (const auto& m : n.members) {
                if (auto r = walk_numbers(*m.second); !r) return r;
            }
            return {};
    }
    return std::unexpected(bad("unreachable node kind"));
}

}  // namespace

bool is_python_number_token(std::string_view token) {
    const bool has_frac_or_exp = token.find_first_of(".eE") != std::string_view::npos;
    if (!has_frac_or_exp) {
        // Integer: the grammar already bans leading zeros; Python re-dumps -0 as 0.
        return token != "-0";
    }
    // Float: must equal CPython repr of its parsed value.
    double d = 0.0;
    const auto res = std::from_chars(token.data(), token.data() + token.size(), d);
    if (res.ec != std::errc() || res.ptr != token.data() + token.size()) return false;
    return python_float_repr(d) == token;
}

std::expected<void, AuditError> validate_python_number_tokens(std::string_view text) {
    Parser p(text);
    auto tree = p.parse_document();
    if (!tree) return std::unexpected(tree.error());
    return walk_numbers(**tree);
}

std::expected<std::string, AuditError> canonicalize_json(std::string_view text) {
    Parser p(text);
    auto tree = p.parse_document();
    if (!tree) return std::unexpected(tree.error());
    std::string out;
    out.reserve(text.size());
    auto r = emit_canonical(**tree, out);
    if (!r) return std::unexpected(r.error());
    return out;
}

std::expected<JsonlRecord, AuditError> parse_jsonl_record(std::string_view line) {
    Parser p(line);
    auto tree = p.parse_document();
    if (!tree) return std::unexpected(tree.error());
    const Node& root = **tree;
    if (root.kind != Node::Kind::Object) return std::unexpected(bad("record is not an object"));

    JsonlRecord rec;
    bool have_seq = false, have_ts = false, have_proj = false, have_event = false,
         have_data = false, have_prev = false, have_hash = false;

    auto decoded_string = [](const Node& n) -> std::expected<std::string, AuditError> {
        if (n.kind != Node::Kind::Scalar || n.raw.size() < 2 || n.raw.front() != '"') {
            return std::unexpected(bad("expected string field"));
        }
        return json_unescape(std::string_view(n.raw).substr(1, n.raw.size() - 2));
    };

    for (const auto& m : root.members) {
        auto key = json_unescape(m.first);
        if (!key) return std::unexpected(key.error());
        const Node& v = *m.second;
        if (*key == "seq") {
            if (v.kind != Node::Kind::Scalar) return std::unexpected(bad("seq not a number"));
            std::int64_t seq = 0;
            for (const char c : v.raw) {
                if (c < '0' || c > '9' || seq > (INT64_MAX - 9) / 10) {
                    return std::unexpected(bad("seq not a non-negative integer"));
                }
                seq = seq * 10 + (c - '0');
            }
            if (v.raw.empty()) return std::unexpected(bad("empty seq"));
            rec.seq = seq;
            have_seq = true;
        } else if (*key == "timestamp") {
            auto s = decoded_string(v);
            if (!s) return std::unexpected(s.error());
            rec.timestamp = std::move(*s);
            have_ts = true;
        } else if (*key == "project_id") {
            auto s = decoded_string(v);
            if (!s) return std::unexpected(s.error());
            rec.project_id = std::move(*s);
            have_proj = true;
        } else if (*key == "event") {
            auto s = decoded_string(v);
            if (!s) return std::unexpected(s.error());
            rec.event = std::move(*s);
            have_event = true;
        } else if (*key == "data") {
            std::string out;
            auto r = emit_canonical(v, out);
            if (!r) return std::unexpected(r.error());
            rec.data_canonical = std::move(out);
            have_data = true;
        } else if (*key == "prev_hash") {
            auto s = decoded_string(v);
            if (!s) return std::unexpected(s.error());
            rec.prev_hash = std::move(*s);
            have_prev = true;
        } else if (*key == "hash") {
            auto s = decoded_string(v);
            if (!s) return std::unexpected(s.error());
            rec.hash = std::move(*s);
            have_hash = true;
        } else {
            return std::unexpected(bad("unknown record key: " + *key));
        }
    }
    if (!(have_seq && have_ts && have_proj && have_event && have_data && have_prev && have_hash)) {
        return std::unexpected(bad("missing record field"));
    }
    return rec;
}

std::expected<void, AuditError> verify_jsonl_chain(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(AuditError(AuditErrc::Io, "cannot open " + path));
    }
    std::string line;
    std::string expected_prev(64, '0');
    std::int64_t expected_seq = 1;
    std::int64_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty()) continue;
        auto rec = parse_jsonl_record(line);
        if (!rec) {
            return std::unexpected(
                AuditError(AuditErrc::ChainBroken,
                           "line " + std::to_string(line_no) + ": " + rec.error().message));
        }
        if (rec->seq != expected_seq) {
            return std::unexpected(
                AuditError(AuditErrc::ChainBroken, "seq gap at line " + std::to_string(line_no)));
        }
        if (rec->prev_hash != expected_prev) {
            return std::unexpected(AuditError(
                AuditErrc::ChainBroken, "prev_hash mismatch at seq " + std::to_string(rec->seq)));
        }
        const std::string record =
            canonical_record(rec->seq, rec->timestamp, rec->project_id, rec->event,
                             rec->data_canonical, rec->prev_hash);
        if (sha256_hex(record) != rec->hash) {
            return std::unexpected(AuditError(AuditErrc::ChainBroken,
                                              "hash mismatch at seq " + std::to_string(rec->seq)));
        }
        expected_prev = rec->hash;
        ++expected_seq;
    }
    return {};
}

}  // namespace ingeneer::audit
