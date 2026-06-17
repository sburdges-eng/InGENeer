// SPDX-License-Identifier: Apache-2.0
#include "ingeneer/pointcloud/octree_build_detail.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ingeneer/pointcloud/kernel_assert.h"
#include "ingeneer/pointcloud/morton.h"
#include "ingeneer/pointcloud/octree_checksum.h"
#include "ingeneer/pointcloud/octree_metadata.h"
#include "ingeneer/pointcloud/sha256.h"

namespace ingeneer::pointcloud {
namespace detail {

AabbD compute_bounds(std::span<const RawPoint> pts) noexcept {
    if (pts.empty()) return AabbD{{0, 0, 0}, {0, 0, 0}};
    AabbD b{{pts[0].x, pts[0].y, pts[0].z}, {pts[0].x, pts[0].y, pts[0].z}};
    for (const RawPoint& p : pts) {
        b.min[0] = std::min(b.min[0], p.x);
        b.max[0] = std::max(b.max[0], p.x);
        b.min[1] = std::min(b.min[1], p.y);
        b.max[1] = std::max(b.max[1], p.y);
        b.min[2] = std::min(b.min[2], p.z);
        b.max[2] = std::max(b.max[2], p.z);
    }
    return b;
}

Quantizer make_quantizer(const AabbD& w) noexcept {
    double edge = std::max({w.max[0] - w.min[0], w.max[1] - w.min[1], w.max[2] - w.min[2]});
    if (!(edge > 0.0)) edge = 1.0;
    Quantizer q{};
    q.origin[0] = w.min[0];
    q.origin[1] = w.min[1];
    q.origin[2] = w.min[2];
    q.scale = edge / 2147483648.0;
    return q;
}

std::uint8_t grid_cell_axis(std::int32_t qv, std::uint8_t level) noexcept {
    const unsigned shift = static_cast<unsigned>(kQuantBits - level - 7);
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(qv) >> shift) & 0x7Fu);
}

std::uint64_t cell_dist_sq(std::int32_t qx, std::int32_t qy, std::int32_t qz, std::uint8_t level,
                           std::int32_t lox, std::int32_t loy, std::int32_t loz, std::uint8_t cx,
                           std::uint8_t cy, std::uint8_t cz) noexcept {
    const std::int64_t cellSize = std::int64_t(1) << (kQuantBits - level - 7);
    auto d = [&](std::int32_t v, std::int32_t lo, std::uint8_t c) -> std::int64_t {
        const std::int64_t center = std::int64_t(lo) + std::int64_t(c) * cellSize + cellSize / 2;
        return std::int64_t(v) - center;
    };
    const std::int64_t dx = d(qx, lox, cx), dy = d(qy, loy, cy), dz = d(qz, loz, cz);
    return static_cast<std::uint64_t>(dx * dx + dy * dy + dz * dz);
}

}  // namespace detail

namespace {

struct Staged {
    std::uint64_t morton;
    std::uint32_t ingest;
    std::int32_t qx, qy, qz;
    std::uint32_t src;
};

struct BuildNode {
    std::uint8_t level = 0;
    std::int32_t lo[3] = {0, 0, 0};
    std::vector<std::uint32_t> payload;
    std::array<std::int32_t, 8> child = {-1, -1, -1, -1, -1, -1, -1, -1};
    std::uint8_t child_mask = 0;
};

void grid_sample(BuildNode& node, std::span<const Staged> all, std::vector<std::uint32_t>&& incoming,
                 std::vector<std::uint32_t>& out_remaining) {
    struct Best {
        std::uint64_t d2;
        std::uint64_t morton;
        std::uint32_t ingest;
        std::uint32_t idx;
    };
    std::unordered_map<std::uint32_t, Best> cells;
    cells.reserve(incoming.size());
    for (std::uint32_t idx : incoming) {
        const Staged& s = all[idx];
        const std::uint8_t cx = detail::grid_cell_axis(s.qx, node.level);
        const std::uint8_t cy = detail::grid_cell_axis(s.qy, node.level);
        const std::uint8_t cz = detail::grid_cell_axis(s.qz, node.level);
        const std::uint32_t key = (static_cast<std::uint32_t>(cx) << 14) |
                                  (static_cast<std::uint32_t>(cy) << 7) | cz;
        const std::uint64_t d2 =
            detail::cell_dist_sq(s.qx, s.qy, s.qz, node.level, node.lo[0], node.lo[1], node.lo[2], cx,
                                 cy, cz);
        Best cand{d2, s.morton, s.ingest, idx};
        auto it = cells.find(key);
        if (it == cells.end()) {
            cells.emplace(key, cand);
            continue;
        }
        Best& b = it->second;
        if (std::tie(cand.d2, cand.morton, cand.ingest) < std::tie(b.d2, b.morton, b.ingest)) b = cand;
    }
    std::vector<std::uint32_t> winners;
    winners.reserve(cells.size());
    std::unordered_map<std::uint32_t, char> wmark;
    wmark.reserve(cells.size());
    for (auto& [k, b] : cells) {
        (void)k;
        winners.push_back(b.idx);
        wmark.emplace(b.idx, 1);
    }
    out_remaining.clear();
    for (std::uint32_t idx : incoming)
        if (!wmark.count(idx)) out_remaining.push_back(idx);
    std::sort(winners.begin(), winners.end(), [&](std::uint32_t a, std::uint32_t b) {
        return std::tie(all[a].morton, all[a].ingest) < std::tie(all[b].morton, all[b].ingest);
    });
    node.payload = winners;
}

std::int32_t build_node(std::vector<BuildNode>& nodes, std::span<const Staged> all, std::uint8_t level,
                        const std::int32_t lo[3], std::vector<std::uint32_t>&& incoming,
                        const BuildParams& p) {
    const std::int32_t id = static_cast<std::int32_t>(nodes.size());
    nodes.emplace_back();
    nodes[id].level = level;
    nodes[id].lo[0] = lo[0];
    nodes[id].lo[1] = lo[1];
    nodes[id].lo[2] = lo[2];

    if (incoming.size() <= p.max_node_points || level + 1 >= kMaxDepth) {
        nodes[id].payload = std::move(incoming);
        std::sort(nodes[id].payload.begin(), nodes[id].payload.end(), [&](std::uint32_t a, std::uint32_t b) {
            return std::tie(all[a].morton, all[a].ingest) < std::tie(all[b].morton, all[b].ingest);
        });
        return id;
    }

    std::vector<std::uint32_t> remaining;
    grid_sample(nodes[id], all, std::move(incoming), remaining);

    std::array<std::vector<std::uint32_t>, 8> buckets;
    for (std::uint32_t idx : remaining)
        buckets[octant_at_level(all[idx].morton, level)].push_back(idx);

    const std::int32_t childEdge = std::int32_t(std::uint32_t(1) << (kQuantBits - level - 1));
    for (int oct = 0; oct < 8; ++oct) {
        if (buckets[oct].empty()) continue;
        if (buckets[oct].size() < p.min_node_points) {
            for (std::uint32_t idx : buckets[oct]) nodes[static_cast<std::size_t>(id)].payload.push_back(idx);
            continue;
        }
        std::int32_t clo[3] = {lo[0] + ((oct >> 2) & 1) * childEdge, lo[1] + ((oct >> 1) & 1) * childEdge,
                               lo[2] + (oct & 1) * childEdge};
        const std::int32_t cid =
            build_node(nodes, all, static_cast<std::uint8_t>(level + 1), clo, std::move(buckets[oct]), p);
        nodes[static_cast<std::size_t>(id)].child[static_cast<std::size_t>(oct)] = cid;
        nodes[static_cast<std::size_t>(id)].child_mask |= static_cast<std::uint8_t>(1u << oct);
    }

    // Spec §2.2: a leaf exceeding maxNodePoints must split. When grid_sample leaves too many
    // winners and no octant descent, force a geometric octant partition of the payload.
    if (nodes[static_cast<std::size_t>(id)].child_mask == 0 &&
        nodes[static_cast<std::size_t>(id)].payload.size() > p.max_node_points && level + 1 < kMaxDepth) {
        std::array<std::vector<std::uint32_t>, 8> pbuckets;
        for (std::uint32_t idx : nodes[static_cast<std::size_t>(id)].payload)
            pbuckets[octant_at_level(all[idx].morton, level)].push_back(idx);
        nodes[static_cast<std::size_t>(id)].payload.clear();
        for (int oct = 0; oct < 8; ++oct) {
            if (pbuckets[static_cast<std::size_t>(oct)].empty()) continue;
            if (pbuckets[static_cast<std::size_t>(oct)].size() < p.min_node_points) {
                for (std::uint32_t idx : pbuckets[static_cast<std::size_t>(oct)])
                    nodes[static_cast<std::size_t>(id)].payload.push_back(idx);
                continue;
            }
            std::int32_t clo[3] = {lo[0] + ((oct >> 2) & 1) * childEdge, lo[1] + ((oct >> 1) & 1) * childEdge,
                                   lo[2] + (oct & 1) * childEdge};
            const std::int32_t cid = build_node(nodes, all, static_cast<std::uint8_t>(level + 1), clo,
                                                std::move(pbuckets[static_cast<std::size_t>(oct)]), p);
            nodes[static_cast<std::size_t>(id)].child[static_cast<std::size_t>(oct)] = cid;
            nodes[static_cast<std::size_t>(id)].child_mask |= static_cast<std::uint8_t>(1u << oct);
        }
    }

    std::sort(nodes[static_cast<std::size_t>(id)].payload.begin(), nodes[static_cast<std::size_t>(id)].payload.end(),
              [&](std::uint32_t a, std::uint32_t b) {
                  return std::tie(all[a].morton, all[a].ingest) < std::tie(all[b].morton, all[b].ingest);
              });
    return id;
}

std::size_t per_point_bytes(const AttributeSchema& s) {
    return 12 + 2 + 1 + 1 + (s.has_rgb ? 6 : 0) + (s.has_gps_time ? 8 : 0);
}

inline std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) / a * a; }

}  // namespace

std::expected<BuildReport, OctreeError> build_octree_incore(std::span<const RawPoint> pts,
                                                            const BuildParams& params,
                                                            const std::filesystem::path& out_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(out_dir, ec);

    const Quantizer q = detail::make_quantizer(detail::compute_bounds(pts));
    const double root_edge = q.scale * 2147483648.0;

    std::vector<Staged> staged;
    staged.reserve(pts.size());
    for (std::uint32_t i = 0; i < pts.size(); ++i) {
        const RawPoint& p = pts[i];
        const std::int32_t qx = q.quantize(p.x, 0), qy = q.quantize(p.y, 1), qz = q.quantize(p.z, 2);
        const std::uint64_t m = morton_encode_21(top21(qx), top21(qy), top21(qz));
        staged.push_back(Staged{m, i, qx, qy, qz, i});
    }
    std::sort(staged.begin(), staged.end(), [](const Staged& a, const Staged& b) {
        return std::tie(a.morton, a.ingest) < std::tie(b.morton, b.ingest);
    });

    std::vector<BuildNode> nodes;
    if (!staged.empty()) {
        std::vector<std::uint32_t> all_idx(staged.size());
        for (std::uint32_t i = 0; i < staged.size(); ++i) all_idx[i] = i;
        const std::int32_t lo0[3] = {0, 0, 0};
        build_node(nodes, staged, 0, lo0, std::move(all_idx), params);
    }

    std::vector<std::int32_t> bfs;
    std::vector<std::uint32_t> first_child(nodes.size(), kNoNode.v);
    if (!nodes.empty()) {
        std::queue<std::int32_t> qd;
        qd.push(0);
        std::vector<std::int32_t> order;
        while (!qd.empty()) {
            const std::int32_t n = qd.front();
            qd.pop();
            order.push_back(n);
            for (int o = 0; o < 8; ++o)
                if (nodes[n].child[o] >= 0) qd.push(nodes[n].child[o]);
        }
        std::vector<std::uint32_t> id_of(nodes.size(), kNoNode.v);
        for (std::uint32_t i = 0; i < order.size(); ++i) id_of[order[i]] = i;
        bfs = order;
        for (std::int32_t n : order) {
            std::uint32_t fc = kNoNode.v;
            for (int o = 0; o < 8; ++o)
                if (nodes[n].child[o] >= 0) {
                    fc = id_of[nodes[n].child[o]];
                    break;
                }
            first_child[n] = fc;
        }
    }

    const std::size_t ppb = per_point_bytes(params.schema);
    std::vector<NodeRecordV1> records(bfs.size());
    {
        std::FILE* pf = std::fopen((out_dir / "points.bin").string().c_str(), "wb");
        if (!pf) return std::unexpected(OctreeError{OctreeErrc::Io});
        std::vector<std::uint8_t> page(kPayloadAlign, 0);
        std::memcpy(page.data(), kMagic, 8);
        page[8] = 1;
        page[9] = 0;
        page[10] = 0;
        page[11] = 0;
        std::fwrite(page.data(), 1, kPayloadAlign, pf);
        std::uint64_t offset = kPayloadAlign;
        for (std::uint32_t i = 0; i < bfs.size(); ++i) {
            const BuildNode& nd = nodes[bfs[i]];
            const std::size_t unpadded = nd.payload.size() * ppb;
            std::vector<std::byte> buf(unpadded);
            std::byte* w = buf.data();
            for (std::uint32_t idx : nd.payload) {
                const Staged& s = staged[idx];
                const std::int32_t pos[3] = {s.qx, s.qy, s.qz};
                std::memcpy(w, pos, 12);
                w += 12;
            }
            for (std::uint32_t idx : nd.payload) {
                const std::uint16_t v = pts[staged[idx].src].intensity;
                std::memcpy(w, &v, 2);
                w += 2;
            }
            for (std::uint32_t idx : nd.payload) {
                const std::uint8_t v = pts[staged[idx].src].classification;
                std::memcpy(w, &v, 1);
                w += 1;
            }
            for (std::uint32_t idx : nd.payload) {
                const std::uint8_t v = pts[staged[idx].src].return_flags;
                std::memcpy(w, &v, 1);
                w += 1;
            }
            if (params.schema.has_rgb)
                for (std::uint32_t idx : nd.payload) {
                    std::memcpy(w, pts[staged[idx].src].rgb, 6);
                    w += 6;
                }
            if (params.schema.has_gps_time)
                for (std::uint32_t idx : nd.payload) {
                    const double v = pts[staged[idx].src].gps_time;
                    std::memcpy(w, &v, 8);
                    w += 8;
                }
            KERNEL_ASSERT(std::size_t(w - buf.data()) == unpadded, "payload byte mismatch");

            const std::uint64_t xx = xxh3_64(std::span<const std::byte>(buf.data(), unpadded));
            const std::size_t padded = align_up(unpadded, kPayloadAlign);
            buf.resize(padded, std::byte{0});
            std::fwrite(buf.data(), 1, padded, pf);

            NodeRecordV1& rec = records[i];
            rec.payload_offset = offset;
            rec.payload_bytes = static_cast<std::uint32_t>(padded);
            rec.point_count = static_cast<std::uint32_t>(nd.payload.size());
            rec.checksum_xxh3 = xx;
            rec.child_mask = nd.child_mask;
            rec.level = nd.level;
            rec.flags = 0;
            rec.first_child = first_child[bfs[i]];
            offset += padded;
        }
        std::fclose(pf);
    }

    {
        std::FILE* hf = std::fopen((out_dir / "hierarchy.bin").string().c_str(), "wb");
        if (!hf) return std::unexpected(OctreeError{OctreeErrc::Io});
        std::uint8_t hdr[16] = {0};
        std::memcpy(hdr, kMagic, 8);
        hdr[8] = 1;
        hdr[9] = 0;
        std::fwrite(hdr, 1, 16, hf);
        if (!records.empty()) std::fwrite(records.data(), sizeof(NodeRecordV1), records.size(), hf);
        std::fclose(hf);
    }

    auto hh = sha256_file_hex(out_dir / "hierarchy.bin");
    auto ph = sha256_file_hex(out_dir / "points.bin");
    if (!hh || !ph) return std::unexpected(OctreeError{OctreeErrc::Io});

    OctreeMetadata md{};
    md.origin[0] = q.origin[0];
    md.origin[1] = q.origin[1];
    md.origin[2] = q.origin[2];
    md.scale = q.scale;
    md.root_edge = root_edge;
    md.root_cube_world = AabbD{{q.origin[0], q.origin[1], q.origin[2]},
                               {q.origin[0] + root_edge, q.origin[1] + root_edge, q.origin[2] + root_edge}};
    md.point_count = pts.size();
    md.node_count = static_cast<std::uint32_t>(records.size());
    std::uint8_t maxlvl = 0;
    for (const auto& r : records) maxlvl = std::max(maxlvl, r.level);
    md.max_level = maxlvl;
    md.sampling_grid = params.sampling_grid;
    md.max_node_points = params.max_node_points;
    md.min_node_points = params.min_node_points;
    md.schema = params.schema;
    md.hierarchy_sha256 = *hh;
    md.points_sha256 = *ph;

    const std::string mj = write_metadata_json(md);
    {
        std::FILE* mf = std::fopen((out_dir / "metadata.json").string().c_str(), "wb");
        if (!mf) return std::unexpected(OctreeError{OctreeErrc::Io});
        std::fwrite(mj.data(), 1, mj.size(), mf);
        std::fclose(mf);
    }

    BuildReport report{};
    report.point_count = pts.size();
    report.node_count = md.node_count;
    report.max_level = md.max_level;
    report.hierarchy_sha256 = *hh;
    report.points_sha256 = *ph;
    report.metadata_sha256 = sha256_hex(mj);
    return report;
}

}  // namespace ingeneer::pointcloud
