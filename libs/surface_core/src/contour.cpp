// SPDX-License-Identifier: Apache-2.0
//
// Contour extraction over the TIN (Phase 6.3). See contour.h for the algorithm and the
// symbolic-perturbation degeneracy rule. Consumes the Tin strictly through its public
// read-only API (triangles() / vertex()).
#include "ingeneer/surface/contour.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace ingeneer::surface {

namespace {

// Undirected mesh-edge key (vertex-id pair). Finite vertex ids are < 2^32 so the packed
// 64-bit key is collision-free.
std::uint64_t edge_key(VertexId a, VertexId b) noexcept {
    const VertexId lo = a < b ? a : b;
    const VertexId hi = a < b ? b : a;
    return (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);
}

// Symbolic perturbation: z exactly on the level is treated as z + ε, i.e. above. The
// comparison itself is exact (no rounding), so classification is robust.
bool above(double z, double level) noexcept { return z >= level; }

// Intersection of the level plane with edge (a, b), a/b ordered by vertex id by the caller
// so adjacent triangles compute the bit-identical point. Exactly one endpoint may sit
// exactly on the level (two would make the edge uncrossed); emit that vertex bit-exactly so
// duplicate squashing during chaining works by exact equality.
ContourPoint interp(const TinVertex& a, const TinVertex& b, double level) noexcept {
    if (a.z == level) return {a.x, a.y};
    if (b.z == level) return {b.x, b.y};
    const double t = (level - a.z) / (b.z - a.z);
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
}

struct Segment {
    std::uint64_t key[2];  // crossed mesh edges
    ContourPoint pt[2];
};

struct EndRef {
    std::uint32_t seg = 0;
    std::uint32_t end = 0;  // 0 or 1
};

bool same_point(const ContourPoint& a, const ContourPoint& b) noexcept {
    return a.x == b.x && a.y == b.y;
}

}  // namespace

std::expected<ContourLevel, ContourError> extract_contours(const Tin& tin, double level) {
    if (!std::isfinite(level)) {
        return std::unexpected(ContourError{ContourErrc::NonFiniteLevel});
    }

    ContourLevel out;
    out.level = level;

    // --- 1. one segment per straddling triangle -----------------------------------------
    std::vector<Segment> segs;
    for (const auto& tri : tin.triangles()) {
        std::uint64_t keys[3];
        ContourPoint pts[3];
        int n = 0;
        for (int i = 0; i < 3 && n < 3; ++i) {
            const VertexId va = tri[static_cast<std::size_t>(i)];
            const VertexId vb = tri[static_cast<std::size_t>((i + 1) % 3)];
            const TinVertex& a = tin.vertex(va);
            const TinVertex& b = tin.vertex(vb);
            if (above(a.z, level) == above(b.z, level)) continue;
            // Order endpoints by vertex id for a direction-independent intersection point.
            keys[n] = edge_key(va, vb);
            pts[n] = va < vb ? interp(a, b, level) : interp(b, a, level);
            ++n;
        }
        // A triangle either has all vertices on one side (n == 0) or straddles (n == 2).
        if (n == 2) {
            segs.push_back(Segment{{keys[0], keys[1]}, {pts[0], pts[1]}});
        }
    }
    if (segs.empty()) return out;  // level outside the surface: empty result, not an error

    // --- 2. shared-edge adjacency --------------------------------------------------------
    // Each mesh edge belongs to at most two finite triangles, hence at most two segment
    // ends. Edges with a single end are hull edges => open-chain terminals.
    struct Slot {
        EndRef ref[2];
        std::uint32_t count = 0;
    };
    std::unordered_map<std::uint64_t, Slot> adj;
    adj.reserve(segs.size() * 2);
    for (std::uint32_t s = 0; s < segs.size(); ++s) {
        for (std::uint32_t e = 0; e < 2; ++e) {
            Slot& slot = adj[segs[s].key[e]];
            if (slot.count < 2) slot.ref[slot.count] = EndRef{s, e};
            ++slot.count;
        }
    }

    // --- 3. chain ------------------------------------------------------------------------
    std::vector<bool> used(segs.size(), false);

    // Walk from segment `s0`, starting at its end `e0`, appending points until the chain
    // hits a hull edge (open) or returns to the starting segment (closed).
    const auto walk = [&](std::uint32_t s0, std::uint32_t e0) {
        Contour c;
        std::uint32_t s = s0;
        std::uint32_t e = e0;
        c.points.push_back(segs[s].pt[e]);
        for (;;) {
            used[s] = true;
            const std::uint32_t far = 1 - e;
            c.points.push_back(segs[s].pt[far]);
            const Slot& slot = adj.at(segs[s].key[far]);
            const bool has_partner = slot.count == 2;
            EndRef next{};
            if (has_partner) {
                next = (slot.ref[0].seg == s && slot.ref[0].end == far) ? slot.ref[1] : slot.ref[0];
            }
            if (!has_partner) break;  // hull edge: open contour terminates here
            if (next.seg == s0) {     // back to the start: closed loop
                c.closed = true;
                break;
            }
            s = next.seg;
            e = next.end;
        }
        // Squash exact consecutive duplicates (vertex-on-level hits emit the vertex
        // bit-exactly from both incident edges). For closed loops also squash the wrap.
        std::vector<ContourPoint> sq;
        sq.reserve(c.points.size());
        for (const auto& p : c.points) {
            if (sq.empty() || !same_point(sq.back(), p)) sq.push_back(p);
        }
        if (c.closed) {
            while (sq.size() > 1 && same_point(sq.front(), sq.back())) sq.pop_back();
        }
        c.points = std::move(sq);
        // Drop degenerate results (e.g. a peak vertex exactly on the level collapses to a
        // single point — the ε-limit contour is empty).
        const std::size_t min_pts = c.closed ? 3 : 2;
        if (c.points.size() >= min_pts) out.contours.push_back(std::move(c));
    };

    // Open chains first: start at every segment end whose mesh edge is unshared (hull), so
    // each open polyline is traversed end-to-end exactly once.
    for (std::uint32_t s = 0; s < segs.size(); ++s) {
        if (used[s]) continue;
        for (std::uint32_t e = 0; e < 2; ++e) {
            if (!used[s] && adj.at(segs[s].key[e]).count == 1) walk(s, e);
        }
    }
    // Remaining segments belong to closed loops; the starting end is arbitrary.
    for (std::uint32_t s = 0; s < segs.size(); ++s) {
        if (!used[s]) walk(s, 0);
    }
    return out;
}

SmoothedContour chaikin_smooth(const Contour& raw, int iterations) {
    SmoothedContour sm;
    sm.closed = raw.closed;
    sm.points = raw.points;
    if (raw.points.size() < 3) return sm;  // nothing to cut

    for (int it = 0; it < iterations; ++it) {
        const std::vector<ContourPoint>& p = sm.points;
        const std::size_t n = p.size();
        std::vector<ContourPoint> next;
        next.reserve(2 * n);
        const auto cut = [&](const ContourPoint& a, const ContourPoint& b) {
            next.push_back({0.75 * a.x + 0.25 * b.x, 0.75 * a.y + 0.25 * b.y});
            next.push_back({0.25 * a.x + 0.75 * b.x, 0.25 * a.y + 0.75 * b.y});
        };
        if (sm.closed) {
            for (std::size_t i = 0; i < n; ++i) cut(p[i], p[(i + 1) % n]);
        } else {
            next.push_back(p.front());  // hull-terminated endpoints stay exact
            for (std::size_t i = 0; i + 1 < n; ++i) cut(p[i], p[i + 1]);
            next.push_back(p.back());
        }
        sm.points = std::move(next);
    }
    return sm;
}

}  // namespace ingeneer::surface
