// Breakline (constrained-edge) insertion — Phase 6.2, hardening item H-6.
//
// Algorithm (per the plan, binding):
//   1. Insert the polyline's points as ordinary vertices (exact-duplicate xy reuses the
//      existing vertex; its z is kept).
//   2. For each consecutive vertex pair, RECOVER the exact segment as a constrained edge:
//      collect the triangles crossed by the segment (exact orient2d classification only),
//      remove them, and retriangulate the two boundary pseudo-polygons with Anglada's
//      algorithm. The Delaunay-mate selection keeps every non-constrained edge locally
//      Delaunay, so the result is the constrained Delaunay triangulation.
//   3. Exact degeneracies: a segment passing exactly through a vertex splits at that
//      vertex; a segment collinear-overlapping an existing constrained edge MERGES with
//      it (shared sub-edges are re-constrained idempotently).
//   4. A segment properly crossing an existing constrained edge follows the explicit
//      CrossingPolicy (H-6); the audited semantics — including the Z-combination rule for
//      Split — are documented on the enum in tin.h.
//
// The crossed-triangle march is READ-ONLY: any policy event (reject, split) or
// vertex-split aborts it before mutation, so insert_breakline can roll back wholesale by
// snapshot on any error. Intersection point COMPUTATION uses doubles; all crossing and
// sidedness CLASSIFICATION goes through geometry_core's exact predicates.
#include <unordered_map>
#include <utility>
#include <vector>

#include "ingeneer/geom/predicates.h"
#include "ingeneer/surface/tin.h"

namespace ingeneer::surface {
namespace {

using ingeneer::geom::predicates::incircle_2d;
using ingeneer::geom::predicates::orient2d;
using ingeneer::geom::predicates::Orientation;
using ingeneer::geom::predicates::Point2;

// With exact collinearity established and p known distinct from both endpoints (vertex
// xy coordinates are unique), bbox inclusion means p lies strictly inside segment (a, b).
bool within_bbox(const TinVertex& a, const TinVertex& b, const TinVertex& p) noexcept {
    const double lox = a.x < b.x ? a.x : b.x;
    const double hix = a.x < b.x ? b.x : a.x;
    const double loy = a.y < b.y ? a.y : b.y;
    const double hiy = a.y < b.y ? b.y : a.y;
    return lox <= p.x && p.x <= hix && loy <= p.y && p.y <= hiy;
}

}  // namespace

std::uint64_t Tin::edge_key(VertexId a, VertexId b) noexcept {
    const VertexId lo = a < b ? a : b;
    const VertexId hi = a < b ? b : a;
    return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

bool Tin::is_constrained(VertexId a, VertexId b) const noexcept {
    return constrained_.contains(edge_key(a, b));
}

TriId Tin::tri_with_vertex(VertexId v) const noexcept {
    for (TriId t = 0; t < tris_.size(); ++t) {
        const Tri& tri = tris_[t];
        if (!tri.alive || tri.ghost) continue;
        if (tri.v[0] == v || tri.v[1] == v || tri.v[2] == v) return t;
    }
    return kNoTriangle;
}

// Anglada pseudo-polygon triangulation. Polygon: base edge u -> w plus `chain`, the far
// boundary ordered from the u side to the w side, lying LEFT of u -> w at the top level.
// The Delaunay mate of (u, w) is selected by pairwise incircle among the chain vertices
// strictly LEFT of the base (circles through a fixed chord are a pencil, so a single max
// scan finds the empty-circle mate); the recursion then triangulates both sub-polygons.
// Emitted triangles are CCW by construction (the mate filter enforces LEFT).
void Tin::anglada_fill(VertexId u, VertexId w, std::span<const VertexId> chain,
                       std::vector<std::array<VertexId, 3>>& out) const {
    if (chain.empty()) return;
    const Point2 pu{vertices_[u].x, vertices_[u].y};
    const Point2 pw{vertices_[w].x, vertices_[w].y};
    std::size_t ci = chain.size();  // index of current mate candidate (none yet)
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const Point2 pi{vertices_[chain[i]].x, vertices_[chain[i]].y};
        if (orient2d(pu, pw, pi) != Orientation::LEFT) continue;  // mate must be CCW
        if (ci == chain.size()) {
            ci = i;
            continue;
        }
        const Point2 pc{vertices_[chain[ci]].x, vertices_[chain[ci]].y};
        if (incircle_2d(pu, pw, pc, pi) == Orientation::LEFT) ci = i;
    }
    if (ci == chain.size())
        return;  // no CCW mate: degenerate sub-polygon (unreachable —
                 // collinear hits split segments before recovery; a
                 // hole here is caught loudly by the audits)
    const VertexId c = chain[ci];
    anglada_fill(u, c, chain.first(ci), out);
    anglada_fill(c, w, chain.subspan(ci + 1), out);
    out.push_back({u, w, c});
}

// Replace `region` (the triangles crossed by segment a -> b; all finite) with the Anglada
// retriangulation of the left/right boundary chains. Neighbor wiring is generic
// undirected-edge pairing: internal edges pair between two fresh triangles, boundary
// edges reconnect to the recorded outside neighbors (which may be ghosts). All validation
// happens BEFORE the first mutation (see the contract in tin.h).
bool Tin::retriangulate_region(const std::vector<TriId>& region, VertexId a, VertexId b,
                               const std::vector<VertexId>& left,
                               const std::vector<VertexId>& right) {
    std::vector<bool> in_region(tris_.size(), false);
    for (const TriId t : region) in_region[t] = true;
    std::unordered_map<std::uint64_t, TriId> outside;
    for (const TriId t : region) {
        for (int i = 0; i < 3; ++i) {
            const TriId m = tris_[t].n[static_cast<std::size_t>(i)];
            if (m != kNoTriangle && in_region[m]) continue;
            outside.emplace(edge_key(tris_[t].v[static_cast<std::size_t>((i + 1) % 3)],
                                     tris_[t].v[static_cast<std::size_t>((i + 2) % 3)]),
                            m);
        }
    }
    std::vector<std::array<VertexId, 3>> polys;
    polys.reserve(left.size() + right.size());
    anglada_fill(a, b, left, polys);
    const std::vector<VertexId> rrev(right.rbegin(), right.rend());
    anglada_fill(b, a, rrev, polys);

    // ---- pre-mutation validation (Phase 6.5, fuzz-found) -------------------------------
    // With a correct incircle, Anglada consumes every chain vertex and the new triangles
    // tile the region exactly (theorem). geometry_core's incircle_2d Layer-A filter
    // defect (see debug_audit) can select a WRONG Delaunay mate, leaving a degenerate
    // sub-polygon with no CCW mate — a hole — which would wire a corrupt mesh. Verify
    // closure exactly before mutating: full triangle count; every new edge paired with
    // exactly one other new edge or exactly one region-boundary edge; every
    // region-boundary edge consumed exactly once.
    if (polys.size() != left.size() + right.size()) return false;
    {
        std::unordered_map<std::uint64_t, int> occ;
        occ.reserve(3 * polys.size());
        for (const auto& t3 : polys) {
            for (int i = 0; i < 3; ++i) {
                ++occ[edge_key(t3[static_cast<std::size_t>(i)],
                               t3[static_cast<std::size_t>((i + 1) % 3)])];
            }
        }
        std::size_t boundary_used = 0;
        for (const auto& [key, count] : occ) {
            if (outside.contains(key)) {
                if (count != 1) return false;
                ++boundary_used;
            } else if (count != 2) {
                return false;
            }
        }
        if (boundary_used != outside.size()) return false;
    }

    for (const TriId t : region) release_tri(t);

    std::vector<TriId> ids(polys.size(), kNoTriangle);
    for (std::size_t k = 0; k < polys.size(); ++k) {
        ids[k] = alloc_tri();
        tris_[ids[k]].v = polys[k];
    }
    std::unordered_map<std::uint64_t, std::pair<TriId, int>> open;
    for (const TriId id : ids) {
        for (int i = 0; i < 3; ++i) {
            const VertexId u = tris_[id].v[static_cast<std::size_t>((i + 1) % 3)];
            const VertexId w = tris_[id].v[static_cast<std::size_t>((i + 2) % 3)];
            const std::uint64_t key = edge_key(u, w);
            if (const auto it = outside.find(key); it != outside.end()) {
                const TriId m = it->second;
                tris_[id].n[static_cast<std::size_t>(i)] = m;
                if (m != kNoTriangle) {
                    Tri& o = tris_[m];
                    for (int j = 0; j < 3; ++j) {
                        if (edge_key(o.v[static_cast<std::size_t>((j + 1) % 3)],
                                     o.v[static_cast<std::size_t>((j + 2) % 3)]) == key) {
                            o.n[static_cast<std::size_t>(j)] = id;
                            break;
                        }
                    }
                }
            } else if (const auto jt = open.find(key); jt != open.end()) {
                tris_[id].n[static_cast<std::size_t>(i)] = jt->second.first;
                tris_[jt->second.first].n[static_cast<std::size_t>(jt->second.second)] = id;
                open.erase(jt);
            } else {
                open.emplace(key, std::pair<TriId, int>{id, i});
            }
        }
    }
    if (!ids.empty()) last_tri_ = ids.front();
    return true;
}

// Recover segment a0 -> b0 as constrained edge(s). Worklist of sub-segments: splits at
// exactly-hit vertices and (under Split) at crossing intersections push follow-up
// segments instead of recursing.
std::expected<void, TinError> Tin::recover_edges(VertexId a0, VertexId b0, CrossingPolicy policy) {
    std::vector<std::pair<VertexId, VertexId>> work{{a0, b0}};
    std::size_t wi = 0;
    const std::size_t budget = 1024 + 64 * (vertices_.size() + constrained_.size());
    for (std::size_t iter = 0; wi < work.size(); ++iter) {
        if (iter > budget) return std::unexpected(TinError{TinErrc::WalkOverflow});
        const auto [a, b] = work[wi++];
        if (a == b) continue;

        // ---- ring of triangles around a (finite and ghost; neighbor-link rotation) -----
        const TriId t0 = tri_with_vertex(a);
        if (t0 == kNoTriangle) return std::unexpected(TinError{TinErrc::WalkOverflow});
        std::vector<TriId> ring;
        {
            TriId cur = t0;
            for (std::size_t s = 0; s <= tris_.size(); ++s) {
                ring.push_back(cur);
                std::size_t ia = 0;
                while (ia < 3 && tris_[cur].v[ia] != a) ++ia;
                if (ia == 3) return std::unexpected(TinError{TinErrc::WalkOverflow});
                const TriId nxt = tris_[cur].n[(ia + 1) % 3];  // across edge (a, v[ia+2])
                if (nxt == kNoTriangle || nxt == t0) break;
                cur = nxt;
            }
        }

        // ---- pass 1: edge (a, b) already exists -> constrain (idempotent merge) --------
        bool resolved = false;
        for (const TriId t : ring) {
            if (tris_[t].ghost) continue;
            std::size_t ia = 0;
            while (tris_[t].v[ia] != a) ++ia;
            const VertexId u = tris_[t].v[(ia + 1) % 3];
            const VertexId w = tris_[t].v[(ia + 2) % 3];
            if (u == b || w == b) {
                constrained_.insert(edge_key(a, b));
                resolved = true;
                break;
            }
        }
        if (resolved) continue;

        // ---- pass 2: exact vertex-split or the entry wedge ------------------------------
        // For finite ring triangle (a, u, w) CCW, direction a -> b lies strictly in the
        // wedge iff b is LEFT of (a, u) and RIGHT of (a, w); COLLINEAR with an incident
        // edge whose far vertex sits inside segment (a, b) splits at that vertex.
        TriId entry = kNoTriangle;
        VertexId eu = 0;
        VertexId ew = 0;
        for (const TriId t : ring) {
            if (tris_[t].ghost) continue;
            std::size_t ia = 0;
            while (tris_[t].v[ia] != a) ++ia;
            const VertexId u = tris_[t].v[(ia + 1) % 3];
            const VertexId w = tris_[t].v[(ia + 2) % 3];
            const Orientation ou = orient_vv(a, u, vertices_[b].x, vertices_[b].y);
            const Orientation ow = orient_vv(a, w, vertices_[b].x, vertices_[b].y);
            if (ou == Orientation::COLLINEAR &&
                within_bbox(vertices_[a], vertices_[b], vertices_[u])) {
                constrained_.insert(edge_key(a, u));  // (a, u) is an edge of t
                work.push_back({u, b});
                resolved = true;
                break;
            }
            if (ow == Orientation::COLLINEAR &&
                within_bbox(vertices_[a], vertices_[b], vertices_[w])) {
                constrained_.insert(edge_key(a, w));
                work.push_back({w, b});
                resolved = true;
                break;
            }
            if (ou == Orientation::LEFT && ow == Orientation::RIGHT) {
                entry = t;
                eu = u;
                ew = w;
                break;
            }
        }
        if (resolved) continue;
        if (entry == kNoTriangle) return std::unexpected(TinError{TinErrc::WalkOverflow});

        // ---- march across the crossed triangles (READ-ONLY) ----------------------------
        // eu is RIGHT of a -> b and ew LEFT (by the wedge test above).
        std::vector<TriId> region{entry};
        std::vector<VertexId> lchain{ew};
        std::vector<VertexId> rchain{eu};
        VertexId l = ew;
        VertexId r = eu;
        VertexId bcur = b;
        bool requeued = false;
        bool finished = false;
        bool failed = false;
        TinErrc fail_code = TinErrc::WalkOverflow;

        // Existing constrained edge (c, d) properly crosses working segment (a, b): H-6.
        auto crossing_event = [&](VertexId c, VertexId d) {
            if (policy == CrossingPolicy::Reject) {
                failed = true;
                fail_code = TinErrc::ConstraintIntersection;
                return;
            }
            // Split: double-precision intersection of (a, b) x (c, d); z lerped along the
            // NEW breakline's working segment (audited rule — see CrossingPolicy in tin.h).
            const TinVertex& pa = vertices_[a];
            const TinVertex& pb = vertices_[b];
            const TinVertex& pc = vertices_[c];
            const TinVertex& pd = vertices_[d];
            const double rx = pb.x - pa.x;
            const double ry = pb.y - pa.y;
            const double sx = pd.x - pc.x;
            const double sy = pd.y - pc.y;
            const double den = rx * sy - ry * sx;
            double tt = den != 0.0 ? ((pc.x - pa.x) * sy - (pc.y - pa.y) * sx) / den : 0.5;
            if (!(tt > 0.0)) tt = 0.0;
            if (!(tt < 1.0)) tt = 1.0;
            const double qx = pa.x + tt * rx;
            const double qy = pa.y + tt * ry;
            const double qz = pa.z + tt * (pb.z - pa.z);
            // Exact-duplicate pre-scan: the rounded intersection can land bit-exactly on
            // an existing vertex (the engine never stores two vertices with equal xy).
            VertexId dup = kGhostVertex;
            for (VertexId v = 0; v < vertices_.size() && dup == kGhostVertex; ++v) {
                if (vertices_[v].x == qx && vertices_[v].y == qy) dup = v;
            }
            if (dup != kGhostVertex) {
                if (dup == a || dup == b) {
                    // Intersection collapsed onto a working-segment endpoint:
                    // numerically unsplittable. Loud fallback (see CrossingPolicy).
                    failed = true;
                    fail_code = TinErrc::ConstraintIntersection;
                    return;
                }
                if (dup != c && dup != d) {
                    // Relocate the crossed constraint through the existing vertex.
                    constrained_.erase(edge_key(c, d));
                    work.push_back({c, dup});
                    work.push_back({dup, d});
                }
                work.push_back({a, dup});
                work.push_back({dup, b});
                requeued = true;
                return;
            }

            // Split the constraint at the (semantically on-edge) intersection point via
            // the dedicated segment-split primitive: 2->4 split of both adjacent
            // triangles, halves constrained immediately, Lawson flips on both sides.
            // (Inserting q through the regular cavity machinery is incorrect in either
            // barrier state — see Tin::split_constraint.)
            const auto vq = split_constraint(c, d, qx, qy, qz);
            if (!vq) {
                failed = true;
                fail_code = vq.error().code;
                return;
            }
            work.push_back({a, *vq});
            work.push_back({*vq, b});
            requeued = true;
        };

        if (is_constrained(eu, ew)) {
            crossing_event(eu, ew);
        } else {
            std::size_t ia = 0;
            while (tris_[entry].v[ia] != a) ++ia;
            TriId cur = tris_[entry].n[ia];  // across (eu, ew), opposite a
            for (std::size_t s = 0; s < 4 * tris_.size() + 64; ++s) {
                if (cur == kNoTriangle || !tris_[cur].alive || tris_[cur].ghost) break;
                VertexId apex = kGhostVertex;
                for (std::size_t i = 0; i < 3; ++i) {
                    const VertexId v = tris_[cur].v[i];
                    if (v != l && v != r) {
                        apex = v;
                        break;
                    }
                }
                if (apex == kGhostVertex) break;
                region.push_back(cur);
                if (apex == b) {
                    finished = true;
                    break;
                }
                const Orientation o = orient_vv(a, b, vertices_[apex].x, vertices_[apex].y);
                if (o == Orientation::COLLINEAR) {
                    // Segment passes exactly through `apex`: finish (a, apex), requeue.
                    if (!within_bbox(vertices_[a], vertices_[b], vertices_[apex])) break;
                    bcur = apex;
                    work.push_back({apex, b});
                    finished = true;
                    break;
                }
                if (o == Orientation::LEFT) {
                    if (is_constrained(apex, r)) {
                        crossing_event(apex, r);
                        break;
                    }
                    std::size_t il = 0;
                    while (tris_[cur].v[il] != l) ++il;
                    lchain.push_back(apex);
                    l = apex;
                    cur = tris_[cur].n[il];  // across (apex, r), opposite old l
                } else {
                    if (is_constrained(l, apex)) {
                        crossing_event(l, apex);
                        break;
                    }
                    std::size_t ir = 0;
                    while (tris_[cur].v[ir] != r) ++ir;
                    rchain.push_back(apex);
                    r = apex;
                    cur = tris_[cur].n[ir];  // across (l, apex), opposite old r
                }
            }
        }
        if (failed) return std::unexpected(TinError{fail_code});
        if (requeued) continue;  // march aborted before mutation; sub-segments queued
        if (!finished) return std::unexpected(TinError{TinErrc::WalkOverflow});

        if (!retriangulate_region(region, a, bcur, lchain, rchain)) {
            // Anglada closure validation failed (predicate-defect path; see the
            // contract in tin.h): nothing was mutated — fail the breakline loudly and
            // let insert_breakline's snapshot rollback restore the pre-call state.
            return std::unexpected(TinError{TinErrc::WalkOverflow});
        }
        constrained_.insert(edge_key(a, bcur));
    }
    return {};
}

std::expected<BreaklineId, TinError> Tin::breakline_impl(std::span<const TinVertex> polyline,
                                                         CrossingPolicy policy) {
    std::vector<VertexId> chain;
    chain.reserve(polyline.size());
    for (const TinVertex& p : polyline) {
        const auto v = insert(p.x, p.y, p.z);
        if (!v) return std::unexpected(v.error());
        if (chain.empty() || chain.back() != *v) chain.push_back(*v);
    }
    if (chain.size() < 2) return std::unexpected(TinError{TinErrc::BreaklineTooShort});
    if (!initialized_) return std::unexpected(TinError{TinErrc::MeshNotInitialized});
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        if (const auto rec = recover_edges(chain[i], chain[i + 1], policy); !rec) {
            return std::unexpected(rec.error());
        }
    }
    return breakline_count_++;
}

std::expected<BreaklineId, TinError> Tin::insert_breakline(std::span<const TinVertex> polyline,
                                                           CrossingPolicy policy) noexcept {
    if (polyline.size() < 2) return std::unexpected(TinError{TinErrc::BreaklineTooShort});
    Tin snapshot{*this};
    auto out = breakline_impl(polyline, policy);
    if (!out) *this = std::move(snapshot);  // roll back to the exact pre-call state
#if defined(INGENEER_KERNEL_DEBUG_AUDIT)
    debug_audit();
#endif
    return out;
}

}  // namespace ingeneer::surface
