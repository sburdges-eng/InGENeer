// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_metadata.h"

#include <charconv>
#include <cctype>
#include <cstdio>
#include <string>

namespace ingeneer::pointcloud {
namespace {

std::string dbl(double v) {
    char buf[64];
    auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    if (ec != std::errc()) return "0";
    return std::string(buf, p);
}

struct Scan {
    std::string_view s;
    std::size_t i = 0;
    bool fail = false;
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    char peek() { return i < s.size() ? s[i] : '\0'; }
    bool eat(char c) {
        ws();
        if (peek() == c) {
            ++i;
            return true;
        }
        fail = true;
        return false;
    }
    std::string str() {
        ws();
        if (peek() != '"') {
            fail = true;
            return {};
        }
        ++i;
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) break;
                out.push_back(s[i++]);
                continue;
            }
            out.push_back(c);
        }
        fail = true;
        return {};
    }
    double num() {
        ws();
        const std::size_t start = i;
        while (i < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' ||
                s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
            ++i;
        double v = 0;
        auto [p, ec] = std::from_chars(s.data() + start, s.data() + i, v);
        if (ec != std::errc()) fail = true;
        return v;
    }
    bool boolean() {
        ws();
        if (s.compare(i, 4, "true") == 0) {
            i += 4;
            return true;
        }
        if (s.compare(i, 5, "false") == 0) {
            i += 5;
            return false;
        }
        fail = true;
        return false;
    }
};

}  // namespace

std::string write_metadata_json(const OctreeMetadata& m) {
    std::string o = "{\n";
    o += "  \"format\": \"ingeneer-octree\",\n";
    o += "  \"hierarchy_sha256\": \"" + m.hierarchy_sha256 + "\",\n";
    o += "  \"max_level\": " + std::to_string(m.max_level) + ",\n";
    o += "  \"max_node_points\": " + std::to_string(m.max_node_points) + ",\n";
    o += "  \"min_node_points\": " + std::to_string(m.min_node_points) + ",\n";
    o += "  \"node_count\": " + std::to_string(m.node_count) + ",\n";
    o += "  \"origin\": [" + dbl(m.origin[0]) + ", " + dbl(m.origin[1]) + ", " + dbl(m.origin[2]) +
         "],\n";
    o += "  \"point_count\": " + std::to_string(m.point_count) + ",\n";
    o += "  \"points_sha256\": \"" + m.points_sha256 + "\",\n";
    o += "  \"root_edge\": " + dbl(m.root_edge) + ",\n";
    o += "  \"sampling_grid\": " + std::to_string(m.sampling_grid) + ",\n";
    o += "  \"scale\": " + dbl(m.scale) + ",\n";
    o += "  \"schema_has_gps_time\": " + std::string(m.schema.has_gps_time ? "true" : "false") + ",\n";
    o += "  \"schema_has_rgb\": " + std::string(m.schema.has_rgb ? "true" : "false") + ",\n";
    o += "  \"version\": {\"major\": " + std::to_string(m.version_major) + ", \"minor\": " +
         std::to_string(m.version_minor) + "}\n";
    o += "}\n";
    return o;
}

std::expected<OctreeMetadata, OctreeError> parse_metadata_json(std::string_view text) {
    auto corrupt = [] { return std::unexpected(OctreeError{OctreeErrc::CorruptHierarchy}); };
    OctreeMetadata m{};
    bool saw_format = false, saw_point_count = false, saw_scale = false;
    Scan sc{text};
    if (!sc.eat('{')) return corrupt();
    sc.ws();
    if (sc.peek() == '}') return corrupt();
    for (;;) {
        const std::string key = sc.str();
        if (sc.fail || !sc.eat(':')) return corrupt();
        sc.ws();
        if (key == "format") {
            if (sc.str() != "ingeneer-octree") return corrupt();
            saw_format = true;
        } else if (key == "version") {
            if (!sc.eat('{')) return corrupt();
            for (;;) {
                const std::string vk = sc.str();
                if (sc.fail || !sc.eat(':')) return corrupt();
                const double vv = sc.num();
                if (vk == "major") m.version_major = static_cast<std::uint32_t>(vv);
                else if (vk == "minor") m.version_minor = static_cast<std::uint32_t>(vv);
                sc.ws();
                if (sc.peek() == ',') {
                    ++sc.i;
                    continue;
                }
                if (!sc.eat('}')) return corrupt();
                break;
            }
        } else if (key == "origin") {
            if (!sc.eat('[')) return corrupt();
            m.origin[0] = sc.num();
            if (!sc.eat(',')) return corrupt();
            m.origin[1] = sc.num();
            if (!sc.eat(',')) return corrupt();
            m.origin[2] = sc.num();
            if (!sc.eat(']')) return corrupt();
        } else if (key == "scale") {
            m.scale = sc.num();
            saw_scale = true;
        } else if (key == "root_edge") {
            m.root_edge = sc.num();
        } else if (key == "point_count") {
            m.point_count = static_cast<std::uint64_t>(sc.num());
            saw_point_count = true;
        } else if (key == "node_count") {
            m.node_count = static_cast<std::uint32_t>(sc.num());
        } else if (key == "max_level") {
            m.max_level = static_cast<std::uint8_t>(sc.num());
        } else if (key == "sampling_grid") {
            m.sampling_grid = static_cast<std::uint32_t>(sc.num());
        } else if (key == "max_node_points") {
            m.max_node_points = static_cast<std::uint32_t>(sc.num());
        } else if (key == "min_node_points") {
            m.min_node_points = static_cast<std::uint32_t>(sc.num());
        } else if (key == "schema_has_rgb") {
            m.schema.has_rgb = sc.boolean();
        } else if (key == "schema_has_gps_time") {
            m.schema.has_gps_time = sc.boolean();
        } else if (key == "hierarchy_sha256") {
            m.hierarchy_sha256 = sc.str();
        } else if (key == "points_sha256") {
            m.points_sha256 = sc.str();
        } else {
            sc.ws();
            const char c = sc.peek();
            if (c == '"')
                sc.str();
            else if (c == '{' || c == '[') {
                int depth = 0;
                do {
                    char d = sc.peek();
                    if (d == '\0') {
                        sc.fail = true;
                        break;
                    }
                    if (d == '{' || d == '[') ++depth;
                    else if (d == '}' || d == ']') --depth;
                    ++sc.i;
                } while (depth > 0);
            } else if (c == 't' || c == 'f')
                sc.boolean();
            else
                sc.num();
        }
        if (sc.fail) return corrupt();
        sc.ws();
        if (sc.peek() == ',') {
            ++sc.i;
            continue;
        }
        if (!sc.eat('}')) return corrupt();
        break;
    }
    if (!saw_format || !saw_point_count || !saw_scale) return corrupt();
    if (m.version_major > 1) return std::unexpected(OctreeError{OctreeErrc::FormatVersion});
    return m;
}

}  // namespace ingeneer::pointcloud
