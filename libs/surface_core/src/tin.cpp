#include "ingeneer/surface/tin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "ingeneer/geom/predicates.h"
#include "ingeneer/surface/kernel_assert.h"

namespace ingeneer::surface {
namespace {

using ingeneer::geom::predicates::incircle_2d;
using ingeneer::geom::predicates::orient2d;
using ingeneer::geom::predicates::Orientation;
using ingeneer::geom::predicates::Point2;

}  // namespace

Orientation Tin::orient_vv(VertexId a, VertexId b, double px, double py) const noexcept {
    return orient2d(Point2{vertices_[a].x, vertices_[a].y}, Point2{vertices_[b].x, vertices_[b].y},
                    Point2{px, py});
}

TriId Tin::alloc_tri() {
    if (!free_.empty()) {
        const TriId t = free_.back();
        free_.pop_back();
        tris_[t] = Tri{};
        tris_[t].alive = true;
        return t;
    }
    tris_.push_back(Tri{});
    tris_.back().alive = true;
    return static_cast<TriId>(tris_.size() - 1);
}

void Tin::release_tri(TriId t) {
    tris_[t].alive = false;
    free_.push_back(t);
}

std::size_t Tin::triangle_count() const noexcept {
    std::size_t n = 0;
    for (const Tri& t : tris_) {
        if (t.alive && !t.ghost) ++n;
    }
    return n;
}

std::size_t Tin::hull_size() const noexcept {
    std::size_t n = 0;
    for (const Tri& t : tris_) {
        if (t.alive && t.ghost) ++n;
    }
    return n;
}

std::vector<std::array<VertexId, 3>> Tin::triangles() const {
    std::vector<std::array<VertexId, 3>> out;
    out.reserve(tris_.size());
    for (const Tri& t : tris_) {
        if (t.alive && !t.ghost) out.push_back(t.v);
    }
    return out;
}

// Conflict ("the point invades this triangle's region"):
//   finite triangle — p strictly inside the circumcircle (LEFT under the CCW invariant);
//   ghost triangle  — using the stored finite directed edge (u -> w), whose RIGHT side is
//                     the TIN interior by construction: conflict when p is strictly
//                     outside, or collinear AND on the closed segment (a hull-edge hit);
//                     collinear-beyond points belong to a neighboring ghost.
bool Tin::conflicts(const Tri& t, double px, double py) const noexcept {
    if (!t.ghost) {
        return incircle_2d(Point2{vertices_[t.v[0]].x, vertices_[t.v[0]].y},
                           Point2{vertices_[t.v[1]].x, vertices_[t.v[1]].y},
                           Point2{vertices_[t.v[2]].x, vertices_[t.v[2]].y},
                           Point2{px, py}) == Orientation::LEFT;
    }
    int g = 0;
    while (t.v[static_cast<std::size_t>(g)] != kGhostVertex) ++g;
    const VertexId u = t.v[static_cast<std::size_t>((g + 1) % 3)];
    const VertexId w = t.v[static_cast<std::size_t>((g + 2) % 3)];
    const Orientation o = orient_vv(u, w, px, py);
    if (o == Orientation::LEFT) return true;  // strictly outside this hull edge
    if (o != Orientation::COLLINEAR) return false;
    // Collinear with the hull edge: conflict only ON the closed segment (the point joins
    // this very edge). A point on the line BEYOND an endpoint must instead be claimed by
    // a neighboring ghost that sees it strictly outside — including it here would emit a
    // degenerate zero-area triangle from this ghost's finite boundary edge. (Finite
    // boundary edges can never be collinear with the inserted point: a point on the line
    // through a circumcircle chord that is strictly inside the circle lies strictly
    // between the chord endpoints, which forces the across-edge neighbor into the
    // cavity as well.)
    const TinVertex& uu = vertices_[u];
    const TinVertex& ww = vertices_[w];
    const double lox = uu.x < ww.x ? uu.x : ww.x;
    const double hix = uu.x < ww.x ? ww.x : uu.x;
    const double loy = uu.y < ww.y ? uu.y : ww.y;
    const double hiy = uu.y < ww.y ? ww.y : uu.y;
    return lox <= px && px <= hix && loy <= py && py <= hiy;
}

// Exact exhaustive seed scan (see tin.h). Containment first: a finite triangle contains
// p iff p is not strictly RIGHT of any of its directed CCW edges. The containing triangle
// is always a valid cavity seed: it is on p's side of every constraint barrier, and its
// circumcircle strictly contains p (p is in the closed triangle and distinct from every
// vertex). An arbitrary in-conflict triangle is NOT a valid seed — it can lie across a
// constrained edge from p, making the barrier-limited cavity unrepairable (the Phase 6.5
// fuzz-found insert-rejection wart).
TriId Tin::exhaustive_seed(double px, double py, VertexId& dup) const noexcept {
    dup = kGhostVertex;
    for (TriId t = 0; t < tris_.size(); ++t) {
        const Tri& tri = tris_[t];
        if (!tri.alive || tri.ghost) continue;
        bool inside = true;
        for (int i = 0; i < 3 && inside; ++i) {
            const VertexId u = tri.v[static_cast<std::size_t>((i + 1) % 3)];
            const VertexId w = tri.v[static_cast<std::size_t>((i + 2) % 3)];
            if (orient_vv(u, w, px, py) == Orientation::RIGHT) inside = false;
        }
        if (!inside) continue;
        for (int i = 0; i < 3; ++i) {
            const TinVertex& v = vertices_[tri.v[static_cast<std::size_t>(i)]];
            if (v.x == px && v.y == py) dup = tri.v[static_cast<std::size_t>(i)];
        }
        return t;
    }
    // No finite container: p is outside the hull; some ghost claims it by construction.
    for (TriId t = 0; t < tris_.size(); ++t) {
        if (tris_[t].alive && tris_[t].ghost && conflicts(tris_[t], px, py)) return t;
    }
    return kNoTriangle;
}

// Remembering walk through finite triangles toward (px, py); ends in a ghost when the
// point lies outside the hull. Exact duplicate vertices are reported through `dup`.
TriId Tin::locate(double px, double py, VertexId& dup) const noexcept {
    dup = kGhostVertex;
    TriId cur = last_tri_;
    if (cur == kNoTriangle || !tris_[cur].alive) {
        cur = kNoTriangle;
        for (TriId t = 0; t < tris_.size(); ++t) {
            if (tris_[t].alive && !tris_[t].ghost) {
                cur = t;
                break;
            }
        }
    }
    if (cur != kNoTriangle && tris_[cur].ghost) {
        int g = 0;
        while (tris_[cur].v[static_cast<std::size_t>(g)] != kGhostVertex) ++g;
        cur = tris_[cur].n[static_cast<std::size_t>(g)];  // finite across the hull edge
    }

    TriId prev = kNoTriangle;
    const std::size_t budget = 4 * tris_.size() + 64;
    for (std::size_t step = 0; cur != kNoTriangle && step < budget; ++step) {
        const Tri& t = tris_[cur];
        if (t.ghost) return cur;  // outside the hull; ghost conflicts by construction

        for (int i = 0; i < 3; ++i) {
            const TinVertex& v = vertices_[t.v[static_cast<std::size_t>(i)]];
            if (v.x == px && v.y == py) {
                dup = t.v[static_cast<std::size_t>(i)];
                return cur;
            }
        }

        TriId next = kNoTriangle;
        for (int i = 0; i < 3; ++i) {
            const VertexId eu = t.v[static_cast<std::size_t>((i + 1) % 3)];
            const VertexId ew = t.v[static_cast<std::size_t>((i + 2) % 3)];
            if (orient_vv(eu, ew, px, py) == Orientation::RIGHT) {
                const TriId cand = t.n[static_cast<std::size_t>(i)];
                if (cand != prev) {
                    next = cand;
                    break;
                }
                if (next == kNoTriangle) next = cand;  // forced back-step (degenerate)
            }
        }
        if (next == kNoTriangle) return cur;  // contained (or on an edge)
        prev = cur;
        cur = next;
    }

    // Fallback: exact exhaustive seed scan. Reachable when the walk cycles — a CDT is
    // not globally Delaunay, and the visibility walk is only guaranteed acyclic on
    // Delaunay triangulations, so constrained meshes CAN cycle it on degenerate input.
    return exhaustive_seed(px, py, dup);
}

std::expected<VertexId, TinError> Tin::insert(double x, double y, double z) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return std::unexpected(TinError{TinErrc::NonFiniteCoordinate});
    }
    if (!coordinate_in_domain(x) || !coordinate_in_domain(y) || !coordinate_in_domain(z)) {
        // Outside the exact-predicate safety domain (degree-4 overflow/underflow):
        // reject loudly rather than corrupt silently. See kCoordinateLimit.
        return std::unexpected(TinError{TinErrc::CoordinateOutOfDomain});
    }

    if (!initialized_) {
        // No mesh yet: exact-duplicate scan over all buffered vertices.
        for (VertexId i = 0; i < vertices_.size(); ++i) {
            if (vertices_[i].x == x && vertices_[i].y == y) return i;
        }
        vertices_.push_back(TinVertex{x, y, z});
        const VertexId fresh = static_cast<VertexId>(vertices_.size() - 1);
        pending_.push_back(fresh);
        if (pending_.size() >= 3) try_bootstrap();
#if defined(INGENEER_KERNEL_DEBUG_AUDIT)
        debug_audit();
#endif
        return fresh;
    }

    VertexId dup = kGhostVertex;
    const TriId seed = locate(x, y, dup);
    if (dup != kGhostVertex) return dup;
    if (seed == kNoTriangle) return std::unexpected(TinError{TinErrc::WalkOverflow});

    // Constrained-edge split (Phase 6.2): a point landing EXACTLY on a constrained edge
    // must split the constraint at that point — a vertex in an edge's interior is
    // structurally invalid. Exact test: orient2d collinearity with the edge AND inside its
    // bbox; the point is distinct from every vertex (duplicate check above), so "inside
    // the bbox" is strictly interior to the segment. At most one constrained edge can
    // contain the point (constrained edges never cross or overlap).
    VertexId split_c = kGhostVertex;
    VertexId split_d = kGhostVertex;
    for (const std::uint64_t k : constrained_) {
        const VertexId c = static_cast<VertexId>(k >> 32);
        const VertexId d = static_cast<VertexId>(k & 0xFFFFFFFFu);
        if (orient_vv(c, d, x, y) != Orientation::COLLINEAR) continue;
        const TinVertex& cc = vertices_[c];
        const TinVertex& dd = vertices_[d];
        const double lox = cc.x < dd.x ? cc.x : dd.x;
        const double hix = cc.x < dd.x ? dd.x : cc.x;
        const double loy = cc.y < dd.y ? cc.y : dd.y;
        const double hiy = cc.y < dd.y ? dd.y : cc.y;
        if (lox <= x && x <= hix && loy <= y && y <= hiy) {
            split_c = c;
            split_d = d;
            break;
        }
    }
    if (split_c != kGhostVertex) constrained_.erase(edge_key(split_c, split_d));

    vertices_.push_back(TinVertex{x, y, z});
    const VertexId p = static_cast<VertexId>(vertices_.size() - 1);
    if (!cavity_insert(p, seed)) {
        // Every cavity_insert failure path returns before mutating the mesh, so a full
        // rollback is exactly: un-split the constraint and drop the just-appended vertex
        // (p was never exposed to the caller, and no triangle references it).
        vertices_.pop_back();
        if (split_c != kGhostVertex) constrained_.insert(edge_key(split_c, split_d));
#if defined(INGENEER_KERNEL_DEBUG_AUDIT)
        debug_audit();
#endif
        return std::unexpected(TinError{TinErrc::WalkOverflow});
    }
    if (split_c != kGhostVertex) {
        // Both halves exist: p lies on the (c, d) chord, so both the cavity path and the
        // split+flip path emit the edges (c, p) and (p, d) — cavity: c and d are
        // cavity-boundary vertices and the fresh ring connects each to p; split+flip:
        // the 2-4 edge split creates them and Lawson flips never remove a p-incident
        // edge (every flip replaces a link edge with an edge ending at p).
        constrained_.insert(edge_key(split_c, p));
        constrained_.insert(edge_key(split_d, p));
    }
#if defined(INGENEER_KERNEL_DEBUG_AUDIT)
    debug_audit();
#endif
    return p;
}

// First non-collinear triple bootstraps the mesh; buffered collinear points are then
// replayed through the regular cavity path (their vertex ids are already allocated).
void Tin::try_bootstrap() noexcept {
    const VertexId a = pending_[0];
    const VertexId b = pending_[1];
    const VertexId c = pending_.back();
    const Orientation o = orient_vv(a, b, vertices_[c].x, vertices_[c].y);
    if (o == Orientation::COLLINEAR) return;  // keep buffering

    const VertexId v0 = a;
    const VertexId v1 = (o == Orientation::LEFT) ? b : c;
    const VertexId v2 = (o == Orientation::LEFT) ? c : b;

    const TriId f = alloc_tri();
    tris_[f].v = {v0, v1, v2};
    const TriId g01 = alloc_tri();
    const TriId g12 = alloc_tri();
    const TriId g20 = alloc_tri();
    // Ghost finite edges stored REVERSED: TIN interior on the RIGHT of (u -> w).
    tris_[g01].v = {v1, v0, kGhostVertex};
    tris_[g12].v = {v2, v1, kGhostVertex};
    tris_[g20].v = {v0, v2, kGhostVertex};
    for (const TriId g : {g01, g12, g20}) tris_[g].ghost = true;

    tris_[f].n = {g12, g20, g01};
    tris_[g01].n = {g20, g12, f};
    tris_[g12].n = {g01, g20, f};
    tris_[g20].n = {g12, g01, f};

    initialized_ = true;
    last_tri_ = f;

    // Replay the middle buffered points (collinear with the first edge) into the mesh.
    std::vector<VertexId> replay(pending_.begin() + 2, pending_.end() - 1);
    pending_.clear();
    for (const VertexId vid : replay) {
        VertexId dup = kGhostVertex;
        const TriId seed = locate(vertices_[vid].x, vertices_[vid].y, dup);
        if (dup != kGhostVertex || seed == kNoTriangle) continue;  // exact duplicate
        (void)cavity_insert(vid, seed);
    }
}

// Bowyer-Watson cavity insertion of an already-allocated vertex starting from a
// conflicting (or containing) seed triangle.
bool Tin::cavity_insert(VertexId p, TriId seed) noexcept {
    const double x = vertices_[p].x;
    const double y = vertices_[p].y;

    TriId start = seed;
    if (!conflicts(tris_[start], x, y)) {
        // Defensive: a contained point is always in conflict with its container; this
        // path is reachable only via exotic walk terminations. Exact containment-first
        // recovery (a containing triangle always conflicts and is always a valid seed).
        VertexId dup = kGhostVertex;
        start = exhaustive_seed(x, y, dup);
        if (start == kNoTriangle || dup != kGhostVertex) return false;
    }

    // ---- conflict BFS -------------------------------------------------------------
    // Constrained edges are BARRIERS (H-6): the conflict region never expands across one,
    // so constrained edges survive subsequent point insertions. In-cavity membership is
    // tracked with an epoch-stamped scratch array (O(cavity) per insert, not O(tris_)).
    if (mark_.size() < tris_.size()) mark_.resize(tris_.size(), 0);
    if (++mark_epoch_ == 0) {
        std::fill(mark_.begin(), mark_.end(), 0u);
        mark_epoch_ = 1;
    }
    const std::uint32_t epoch = mark_epoch_;
    const auto in_cavity = [&](TriId t) noexcept { return mark_[t] == epoch; };

    std::vector<TriId> cavity;
    std::vector<TriId> stack{start};
    mark_[start] = epoch;
    while (!stack.empty()) {
        const TriId t = stack.back();
        stack.pop_back();
        cavity.push_back(t);
        for (int i = 0; i < 3; ++i) {
            const TriId m = tris_[t].n[static_cast<std::size_t>(i)];
            if (m == kNoTriangle || in_cavity(m) || !tris_[m].alive) continue;
            if (!constrained_.empty() && constrained_.contains(edge_key(
                                             tris_[t].v[static_cast<std::size_t>((i + 1) % 3)],
                                             tris_[t].v[static_cast<std::size_t>((i + 2) % 3)]))) {
                continue;  // barrier
            }
            if (conflicts(tris_[m], x, y)) {
                mark_[m] = epoch;
                stack.push_back(m);
            }
        }
    }

    // ---- cavity validity (exact orient2d only; ALWAYS on) -----------------------------
    // With constraint barriers the conflict region is no longer guaranteed star-shaped
    // around p: it can wrap around a constraint endpoint (re-including both sides of a
    // constrained edge, i.e. a cavity-INTERIOR constrained edge) or include a triangle
    // whose finite boundary edge does not see p strictly CCW. Such a cavity cannot be
    // wired as a star of p; heuristically trimming it produces locally non-Delaunay
    // meshes. Detect the condition BEFORE mutating and divert to the classical
    // split+flip insertion, which is always structurally correct.
    //
    // The star check runs for PURE-Delaunay cavities too: with a correct incircle they
    // are star-shaped by theorem (the check is then pure insurance), but geometry_core's
    // incircle_2d Layer-A filter defect (see debug_audit) can certify garbage conflict
    // answers and pollute the cavity with non-conflicting triangles — observed under
    // fuzz wiring inverted (non-CCW) triangles from a far-outside-hull insert. Exact
    // orient2d (whose filter is sound) is the structural backstop.
    bool valid = true;
    for (const TriId t : cavity) {
        for (int i = 0; i < 3 && valid; ++i) {
            const VertexId u = tris_[t].v[static_cast<std::size_t>((i + 1) % 3)];
            const VertexId w = tris_[t].v[static_cast<std::size_t>((i + 2) % 3)];
            if (u == kGhostVertex || w == kGhostVertex) continue;
            const TriId m = tris_[t].n[static_cast<std::size_t>(i)];
            if (m != kNoTriangle && in_cavity(m)) {
                if (!constrained_.empty() && constrained_.contains(edge_key(u, w))) {
                    valid = false;  // wrap-around
                }
            } else if (orient_vv(u, w, x, y) != Orientation::LEFT) {
                valid = false;  // boundary edge does not see p: not a star of p
            }
        }
        if (!valid) break;
    }

    // ---- boundary edges -> new triangles (u, w, p) ----------------------------------
    struct NewTri {
        VertexId u, w;
        TriId id, outside;
    };
    std::vector<NewTri> fresh;
    if (valid) {
        for (const TriId t : cavity) {
            for (int i = 0; i < 3; ++i) {
                const TriId m = tris_[t].n[static_cast<std::size_t>(i)];
                if (m != kNoTriangle && in_cavity(m)) continue;
                const VertexId u = tris_[t].v[static_cast<std::size_t>((i + 1) % 3)];
                const VertexId w = tris_[t].v[static_cast<std::size_t>((i + 2) % 3)];
                fresh.push_back(NewTri{u, w, kNoTriangle, m});
            }
        }
        // The retriangulation below assumes the cavity boundary is a SINGLE cycle:
        // each vertex appears exactly once as a first and once as a second endpoint,
        // AND following the (u -> w) links from any edge visits every boundary edge
        // before closing. The uniqueness check alone misses a cavity that encloses an
        // island of non-cavity triangles (two disjoint cycles — possible only when the
        // defective incircle pollutes the conflict region; see debug_audit), which
        // would wire a non-manifold star. Detected BEFORE mutating anything.
        std::unordered_map<VertexId, VertexId> next;
        std::unordered_set<VertexId> seconds;
        next.reserve(fresh.size());
        seconds.reserve(fresh.size());
        for (const NewTri& nt : fresh) {
            if (!next.emplace(nt.u, nt.w).second || !seconds.insert(nt.w).second) {
                valid = false;
                break;
            }
        }
        if (valid && !fresh.empty()) {
            std::size_t steps = 0;
            VertexId cur = fresh.front().u;
            do {
                const auto it = next.find(cur);
                if (it == next.end()) {
                    valid = false;
                    break;
                }
                cur = it->second;
                ++steps;
            } while (cur != fresh.front().u && steps <= fresh.size());
            if (steps != fresh.size()) valid = false;
        }
        // Every finite vertex of a cavity triangle must lie ON the boundary cycle: a
        // polluted cavity (defective incircle; see debug_audit) can swallow a vertex's
        // entire star, and the star-of-p rewiring would orphan that vertex (Euler and
        // valence corruption). Unreachable with a correct predicate.
        if (valid) {
            for (const TriId t : cavity) {
                for (int i = 0; i < 3 && valid; ++i) {
                    const VertexId v = tris_[t].v[static_cast<std::size_t>(i)];
                    if (v != kGhostVertex && !next.contains(v)) valid = false;
                }
                if (!valid) break;
            }
        }
    }
    if (!valid) {
        // Divert to split+flip. Requires the actual container; the walk/seed gives it
        // exactly when finite (the walk stops only at a triangle with no strictly-RIGHT
        // edge). For p outside the hull (ghost seed) there is no container: fail loudly
        // before any mutation — insert() rolls back wholesale.
        if (tris_[start].ghost) return false;
        return insert_split_flip(p, start);
    }
    for (const TriId t : cavity) release_tri(t);
    for (NewTri& nt : fresh) {
        nt.id = alloc_tri();
        tris_[nt.id].v = {nt.u, nt.w, p};
        tris_[nt.id].ghost = (nt.u == kGhostVertex || nt.w == kGhostVertex);
    }

    // ---- neighbor wiring -------------------------------------------------------------
    // The cavity boundary is a single cycle, so "first vertex" / "second vertex" keys
    // are unique across the fresh ring. Edge (w, p) of (u, w, p) pairs with the fresh
    // triangle whose u' == w; edge (p, u) pairs with the one whose w' == u.
    std::unordered_map<VertexId, TriId> by_first;
    std::unordered_map<VertexId, TriId> by_second;
    by_first.reserve(fresh.size());
    by_second.reserve(fresh.size());
    for (const NewTri& nt : fresh) {
        by_first.emplace(nt.u, nt.id);
        by_second.emplace(nt.w, nt.id);
    }
    for (const NewTri& nt : fresh) {
        Tri& t = tris_[nt.id];
        t.n[0] = by_first.at(nt.w);   // opposite u: across (w, p)
        t.n[1] = by_second.at(nt.u);  // opposite w: across (p, u)
        t.n[2] = nt.outside;          // opposite p: across the boundary edge (u, w)
        if (nt.outside != kNoTriangle) {
            Tri& o = tris_[nt.outside];
            for (int i = 0; i < 3; ++i) {
                const VertexId ou = o.v[static_cast<std::size_t>((i + 1) % 3)];
                const VertexId ow = o.v[static_cast<std::size_t>((i + 2) % 3)];
                if ((ou == nt.w && ow == nt.u) || (ou == nt.u && ow == nt.w)) {
                    o.n[static_cast<std::size_t>(i)] = nt.id;
                    break;
                }
            }
        }
    }

    last_tri_ = fresh.front().id;
    for (const NewTri& nt : fresh) {
        if (!tris_[nt.id].ghost) {
            last_tri_ = nt.id;
            break;
        }
    }
    return true;
}

void Tin::replace_neighbor(TriId t, TriId from, TriId to) noexcept {
    if (t == kNoTriangle) return;
    for (int i = 0; i < 3; ++i) {
        if (tris_[t].n[static_cast<std::size_t>(i)] == from) {
            tris_[t].n[static_cast<std::size_t>(i)] = to;
            return;
        }
    }
    KERNEL_ASSERT(false, "replace_neighbor: stale neighbor link");
}

// Classical CDT vertex insertion: split the containing triangle (1->3, or 2->4 when p is
// exactly on an edge), then restore the constrained-Delaunay property by Lawson flips of
// p's link edges. Constrained edges and hull (ghost-adjacent) edges are never flipped.
// Every flip replaces a link edge (u, w) with the edge (p, apex), adding exactly one
// p-incident triangle, so the loop terminates after fewer than vertex_count() flips and
// p-incident edges (in particular re-constrained split halves) are never removed. This is
// the fallback for cavities the barrier-BFS cannot shape into a valid star; it always
// succeeds for p inside the hull (the standard CDT insertion result). All classification
// is exact (orient2d / incircle_2d).
bool Tin::insert_split_flip(VertexId p, TriId container) noexcept {
    const double x = vertices_[p].x;
    const double y = vertices_[p].y;
    const Tri t0 = tris_[container];

    // Exact containment re-verification: 0 collinear edges -> strictly inside (1->3);
    // exactly 1 -> on that edge's interior (2->4); otherwise p coincides with a vertex
    // (screened earlier) or lies outside: fail BEFORE mutating (insert() rolls back).
    int collinear_at = -1;
    int ncol = 0;
    for (int i = 0; i < 3; ++i) {
        const VertexId u = t0.v[static_cast<std::size_t>((i + 1) % 3)];
        const VertexId w = t0.v[static_cast<std::size_t>((i + 2) % 3)];
        const Orientation o = orient_vv(u, w, x, y);
        if (o == Orientation::RIGHT) return false;
        if (o == Orientation::COLLINEAR) {
            collinear_at = i;
            ++ncol;
        }
    }
    if (ncol > 1) return false;

    std::vector<TriId> suspects;  // p-incident finite triangles whose link edge to check
    if (ncol == 0) {
        // ---- 1 -> 3 split ------------------------------------------------------------
        const VertexId a = t0.v[0];
        const VertexId b = t0.v[1];
        const VertexId c = t0.v[2];
        const TriId nA = t0.n[0];     // across (b, c)
        const TriId nB = t0.n[1];     // across (c, a)
        const TriId nC = t0.n[2];     // across (a, b)
        const TriId tab = container;  // reuse: keeps nC's back-link valid
        const TriId tbc = alloc_tri();
        const TriId tca = alloc_tri();
        tris_[tab].v = {a, b, p};
        tris_[tab].n = {tbc, tca, nC};
        tris_[tbc].v = {b, c, p};
        tris_[tbc].n = {tca, tab, nA};
        tris_[tca].v = {c, a, p};
        tris_[tca].n = {tab, tbc, nB};
        replace_neighbor(nA, container, tbc);
        replace_neighbor(nB, container, tca);
        suspects = {tab, tbc, tca};
    } else {
        // ---- 2 -> 4 split (p exactly on edge (u, w); the edge is NOT constrained: an
        // exactly-on-constrained-edge point was pre-split by insert(), which un-flags the
        // edge before the cavity machinery runs) ----------------------------------------
        const std::size_t ia = static_cast<std::size_t>(collinear_at);
        const TriId m = t0.n[ia];  // across (u, w); never kNoTriangle post-bootstrap
        if (m == kNoTriangle || !tris_[m].alive) return false;
        split_edge(container, ia, p, suspects);
    }
    last_tri_ = suspects.front();
    lawson_restore(p, std::move(suspects));
    return true;
}

// 2->4 split of `container`'s edge opposite v[ia] at vertex p; see tin.h. The split is
// purely combinatorial: callers guarantee (exact orient2d) that the four resulting finite
// triangles are strictly CCW — either p is exactly on the shared edge (insert_split_flip)
// or p is strictly inside the adjacent quad (split_constraint).
void Tin::split_edge(TriId container, std::size_t ia, VertexId p, std::vector<TriId>& suspects) {
    const Tri t0 = tris_[container];
    const VertexId a = t0.v[ia];
    const VertexId u = t0.v[(ia + 1) % 3];
    const VertexId w = t0.v[(ia + 2) % 3];
    const TriId m = t0.n[ia];  // across (u, w)
    KERNEL_ASSERT(m != kNoTriangle && tris_[m].alive, "split_edge: dead mate");
    (void)a;
    const TriId t_au = container;  // becomes (a, u, p); keeps the (a,u) back-link
    const TriId t_aw = alloc_tri();
    tris_[t_au].v = {a, u, p};
    tris_[t_aw].v = {a, p, w};
    tris_[t_au].n[1] = t_aw;                // across (p, a)
    tris_[t_au].n[2] = t0.n[(ia + 2) % 3];  // across (a, u): unchanged neighbor
    tris_[t_aw].n[2] = t_au;                // across (a, p)
    tris_[t_aw].n[1] = t0.n[(ia + 1) % 3];  // across (w, a)
    replace_neighbor(t0.n[(ia + 1) % 3], container, t_aw);
    suspects.push_back(t_au);
    suspects.push_back(t_aw);

    const Tri m0 = tris_[m];
    if (!m0.ghost) {
        // Finite mate (d, w, u) in some rotation; split into (d, w, p) + (d, p, u).
        std::size_t id = 0;
        while (m0.v[id] == u || m0.v[id] == w) ++id;
        const VertexId d = m0.v[id];
        const TriId m_dw = m;  // becomes (d, w, p); keeps the (d,w) back-link
        const TriId m_du = alloc_tri();
        tris_[m_dw].v = {d, w, p};
        tris_[m_du].v = {d, p, u};
        tris_[m_dw].n = {t_aw, m_du, m0.n[(id + 2) % 3]};  // across (d,w) unchanged
        tris_[m_du].n = {t_au, m0.n[(id + 1) % 3], m_dw};  // across (u,d) re-linked
        replace_neighbor(m0.n[(id + 1) % 3], m, m_du);
        tris_[t_au].n[0] = m_du;  // across (u, p)
        tris_[t_aw].n[0] = m_dw;  // across (p, w)
        suspects.push_back(m_dw);
        suspects.push_back(m_du);
    } else {
        // Ghost mate: p is exactly on a hull edge. The ghost stores the hull edge
        // REVERSED (interior on the right of u' -> w'), i.e. (w, u); split it into
        // ghosts (w, p) and (p, u) so the hull gains p.
        std::size_t ig = 0;
        while (m0.v[ig] != kGhostVertex) ++ig;
        KERNEL_ASSERT(m0.v[(ig + 1) % 3] == w && m0.v[(ig + 2) % 3] == u,
                      "ghost finite edge does not match the split edge");
        const TriId g_wp = m;  // becomes (w, p, ghost); keeps the w-side ghost link
        const TriId g_pu = alloc_tri();
        tris_[g_wp].v = {w, p, kGhostVertex};
        tris_[g_wp].ghost = true;
        tris_[g_pu].v = {p, u, kGhostVertex};
        tris_[g_pu].ghost = true;
        // Ghost neighbor layout: n[ghost-index] = finite mate; the other two entries
        // link the adjacent ghosts around the hull (n[i] opposite v[i]).
        const TriId ghost_at_u = m0.n[(ig + 1) % 3];  // opposite w: ghost sharing u
        const TriId ghost_at_w = m0.n[(ig + 2) % 3];  // opposite u: ghost sharing w
        tris_[g_wp].n = {g_pu, ghost_at_w, t_aw};
        tris_[g_pu].n = {ghost_at_u, g_wp, t_au};
        replace_neighbor(ghost_at_u, m, g_pu);
        tris_[t_au].n[0] = g_pu;  // across (u, p)
        tris_[t_aw].n[0] = g_wp;  // across (p, w)
    }
}

// Lawson flips on the link of freshly inserted p; see tin.h.
void Tin::lawson_restore(VertexId p, std::vector<TriId> suspects) noexcept {
    const double x = vertices_[p].x;
    const double y = vertices_[p].y;
    while (!suspects.empty()) {
        const TriId t = suspects.back();
        suspects.pop_back();
        std::size_t ip = 0;
        while (ip < 3 && tris_[t].v[ip] != p) ++ip;
        KERNEL_ASSERT(ip < 3, "suspect triangle lost its p vertex");
        const VertexId u = tris_[t].v[(ip + 1) % 3];
        const VertexId w = tris_[t].v[(ip + 2) % 3];
        if (u == kGhostVertex || w == kGhostVertex) continue;  // ghost link edge
        const TriId m = tris_[t].n[ip];
        if (m == kNoTriangle || tris_[m].ghost) continue;     // hull edge: never flipped
        if (constrained_.contains(edge_key(u, w))) continue;  // barrier: never flipped
        std::size_t im = 0;
        while (tris_[m].v[im] == u || tris_[m].v[im] == w) ++im;
        const VertexId am = tris_[m].v[im];
        if (am == kGhostVertex) continue;
        const Point2 pp{x, y};
        const Point2 pu{vertices_[u].x, vertices_[u].y};
        const Point2 pw{vertices_[w].x, vertices_[w].y};
        const Point2 pm{vertices_[am].x, vertices_[am].y};
        if (incircle_2d(pp, pu, pw, pm) != Orientation::LEFT) continue;  // locally Delaunay
        // Flip validity (strictly convex quad p-u-am-w) is guaranteed by the classical
        // insertion theory when starting from a CDT and an EXACT incircle. Exact orient2d
        // guard regardless: never wire an inverted triangle. The skip path is reachable
        // today only via the known geometry_core incircle_2d Layer-A filter defect
        // (wrong sign certified on near-cocircular input; reported upstream, see
        // debug_audit) — the mesh stays a valid triangulation either way.
        if (orient2d(pp, pu, pm) != Orientation::LEFT ||
            orient2d(pp, pm, pw) != Orientation::LEFT) {
            continue;
        }
        // (u, w) -> (p, am): t becomes (p, u, am), m becomes (p, am, w).
        const TriId t_pu = tris_[t].n[(ip + 2) % 3];  // across (p, u): stays with t
        const TriId t_wp = tris_[t].n[(ip + 1) % 3];  // across (w, p): moves to m
        std::size_t iu = 0;                           // index of u in m
        while (tris_[m].v[iu] != u) ++iu;
        std::size_t iw = 0;  // index of w in m
        while (tris_[m].v[iw] != w) ++iw;
        const TriId m_ua = tris_[m].n[iw];  // across (u, am): moves to t
        const TriId m_aw = tris_[m].n[iu];  // across (am, w): stays with m
        tris_[t].v = {p, u, am};
        tris_[t].n = {m_ua, m, t_pu};
        tris_[m].v = {p, am, w};
        tris_[m].n = {m_aw, t_wp, t};
        replace_neighbor(t_wp, t, m);
        replace_neighbor(m_ua, m, t);
        suspects.push_back(t);
        suspects.push_back(m);
    }
}

// Split the constrained edge (c, d) at the H-6 Split crossing point; see tin.h. The
// classical segment-split primitive: the point is semantically ON the constraint (its
// rounded coordinates are usually a hair off it), so BOTH adjacent triangles are 2->4
// split, the halves are constrained immediately (flips never cross them), and Lawson
// flips restore the CDT on both sides. Inserting the point through the regular cavity
// machinery instead is incorrect either way: with the barrier up the far side keeps
// stale triangles whose circumcircles contain the new constraint endpoint (fuzz-found
// local-CDT violations); with the barrier down the mesh is no longer the CDT of the
// reduced constraint set and Bowyer-Watson's correctness premise collapses.
std::expected<VertexId, TinError> Tin::split_constraint(VertexId c, VertexId d, double x, double y,
                                                        double z) noexcept {
    KERNEL_ASSERT(constrained_.contains(edge_key(c, d)), "split_constraint: not constrained");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return std::unexpected(TinError{TinErrc::NonFiniteCoordinate});
    }
    if (!coordinate_in_domain(x) || !coordinate_in_domain(y) || !coordinate_in_domain(z)) {
        return std::unexpected(TinError{TinErrc::CoordinateOutOfDomain});
    }

    // Locate the finite triangle T1 with directed edge (c -> d). A constrained edge is
    // never on the hull-ghost boundary from both sides (its endpoints are mesh vertices
    // and a segment between interior points cannot properly cross the convex hull), so
    // T1 and its mate are finite.
    TriId t1 = kNoTriangle;
    std::size_t ia = 3;
    for (TriId t = 0; t < tris_.size() && t1 == kNoTriangle; ++t) {
        const Tri& tri = tris_[t];
        if (!tri.alive || tri.ghost) continue;
        for (std::size_t i = 0; i < 3; ++i) {
            if (tri.v[(i + 1) % 3] == c && tri.v[(i + 2) % 3] == d) {
                t1 = t;
                ia = i;
                break;
            }
        }
    }
    if (t1 == kNoTriangle) return std::unexpected(TinError{TinErrc::WalkOverflow});
    const TriId t2 = tris_[t1].n[ia];
    if (t2 == kNoTriangle || !tris_[t2].alive || tris_[t2].ghost) {
        return std::unexpected(TinError{TinErrc::WalkOverflow});
    }
    const VertexId a1 = tris_[t1].v[ia];
    std::size_t i2 = 0;
    while (tris_[t2].v[i2] == c || tris_[t2].v[i2] == d) ++i2;
    const VertexId a2 = tris_[t2].v[i2];

    // The rounded point must be strictly inside the quad (a1, c, a2, d) so that all four
    // split triangles are strictly CCW (exact orient2d). Outside it the crossing is
    // numerically unsplittable: fail loudly per the audited Split policy, BEFORE any
    // mutation (insert_breakline's snapshot rollback restores everything).
    if (orient_vv(a1, c, x, y) != Orientation::LEFT ||
        orient_vv(d, a1, x, y) != Orientation::LEFT ||
        orient_vv(a2, d, x, y) != Orientation::LEFT ||
        orient_vv(c, a2, x, y) != Orientation::LEFT) {
        return std::unexpected(TinError{TinErrc::ConstraintIntersection});
    }

    vertices_.push_back(TinVertex{x, y, z});
    const VertexId p = static_cast<VertexId>(vertices_.size() - 1);
    std::vector<TriId> suspects;
    split_edge(t1, ia, p, suspects);
    last_tri_ = suspects.front();
    constrained_.erase(edge_key(c, d));
    constrained_.insert(edge_key(c, p));
    constrained_.insert(edge_key(p, d));
    lawson_restore(p, std::move(suspects));
#if defined(INGENEER_KERNEL_DEBUG_AUDIT)
    debug_audit();
#endif
    return p;
}

// KERNEL_DEBUG_ASSERT-tier full-mesh audit; see tin.h for the contract.
void Tin::debug_audit() const noexcept {
    if (!initialized_) return;  // pre-bootstrap: no mesh to audit
    const std::size_t n = vertices_.size();

    struct Incidence {
        TriId tri;
        VertexId apex;
    };
    std::unordered_map<std::uint64_t, std::vector<Incidence>> finite_edges;
    std::size_t nfinite = 0;
    std::size_t nghost = 0;
    for (TriId t = 0; t < tris_.size(); ++t) {
        const Tri& tri = tris_[t];
        if (!tri.alive) continue;
        int ghosts = 0;
        for (int i = 0; i < 3; ++i) {
            const VertexId v = tri.v[static_cast<std::size_t>(i)];
            if (v == kGhostVertex) {
                ++ghosts;
            } else {
                KERNEL_ASSERT(v < n, "triangle references an out-of-range vertex");
            }
        }
        KERNEL_ASSERT(ghosts == (tri.ghost ? 1 : 0), "ghost flag / ghost vertex mismatch");
        if (tri.ghost) {
            ++nghost;
        } else {
            ++nfinite;
            KERNEL_ASSERT(
                orient2d(Point2{vertices_[tri.v[0]].x, vertices_[tri.v[0]].y},
                         Point2{vertices_[tri.v[1]].x, vertices_[tri.v[1]].y},
                         Point2{vertices_[tri.v[2]].x, vertices_[tri.v[2]].y}) == Orientation::LEFT,
                "finite triangle is not strictly CCW");
        }
        for (int i = 0; i < 3; ++i) {
            const VertexId u = tri.v[static_cast<std::size_t>((i + 1) % 3)];
            const VertexId w = tri.v[static_cast<std::size_t>((i + 2) % 3)];
            const TriId m = tri.n[static_cast<std::size_t>(i)];
            KERNEL_ASSERT(m != kNoTriangle && m < tris_.size() && tris_[m].alive,
                          "triangle has a dead or missing neighbor");
            // Neighbor symmetry: m must hold the same undirected edge and link back.
            bool back = false;
            for (int j = 0; j < 3; ++j) {
                const VertexId mu = tris_[m].v[static_cast<std::size_t>((j + 1) % 3)];
                const VertexId mw = tris_[m].v[static_cast<std::size_t>((j + 2) % 3)];
                if (((mu == u && mw == w) || (mu == w && mw == u)) &&
                    tris_[m].n[static_cast<std::size_t>(j)] == t) {
                    back = true;
                    break;
                }
            }
            KERNEL_ASSERT(back, "neighbor symmetry violated");
            if (!tri.ghost && u != kGhostVertex && w != kGhostVertex) {
                finite_edges[edge_key(u, w)].push_back(
                    Incidence{t, tri.v[static_cast<std::size_t>(i)]});
            }
        }
    }

    KERNEL_ASSERT(nfinite == 2 * n - 2 - nghost, "Euler invariant T = 2n - 2 - h violated");

    for (const std::uint64_t k : constrained_) {
        KERNEL_ASSERT(finite_edges.contains(k), "constrained edge missing from the mesh");
    }

    for (const auto& [key, inc] : finite_edges) {
        KERNEL_ASSERT(inc.size() <= 2, "edge valence exceeds 2");
        if (inc.size() != 2 || constrained_.contains(key)) continue;
        // Local constrained-Delaunay: neither apex strictly inside the other's
        // circumcircle (by the Delaunay lemma the local property implies the global
        // one). The two directional tests are mathematically EQUIVALENT statements about
        // the same quad; a violation is reported only when BOTH agree.
        //
        // CURRENTLY NON-FATAL (kCdtOptimalityFatal == false): geometry_core's
        // incircle_2d Layer-A filter under-estimates its error bound (it scales by
        // |minor| — the CANCELLED 2x2 difference — instead of Shewchuk's sum of absolute
        // products) and can certify a WRONG SIGN on near-cocircular input. The defect
        // was found BY this audit (Phase 6.5 fuzzing) and is reported upstream;
        // geometry_core is read-only for this phase. Because the ENGINE's own flip and
        // conflict decisions consume the same defective predicate, it can wire meshes
        // whose CDT-optimality a (correct) audit refutes on ulp-scale sliver quads, so
        // strict enforcement is impossible until the upstream fix lands — flip
        // kCdtOptimalityFatal to true at that moment. All STRUCTURAL invariants above
        // (exact orient2d is sound) remain fatal.
        Orientation dir[2];
        for (int k2 = 0; k2 < 2; ++k2) {
            const Tri& tri = tris_[inc[static_cast<std::size_t>(k2)].tri];
            const VertexId other = inc[static_cast<std::size_t>(1 - k2)].apex;
            dir[k2] = incircle_2d(Point2{vertices_[tri.v[0]].x, vertices_[tri.v[0]].y},
                                  Point2{vertices_[tri.v[1]].x, vertices_[tri.v[1]].y},
                                  Point2{vertices_[tri.v[2]].x, vertices_[tri.v[2]].y},
                                  Point2{vertices_[other].x, vertices_[other].y});
        }
        if (dir[0] == Orientation::LEFT && dir[1] == Orientation::LEFT) {
            constexpr bool kCdtOptimalityFatal = false;  // flip when upstream fix lands
            static unsigned long cdt_violations = 0;
            ++cdt_violations;
            if (cdt_violations <= 8) {
                // Round-trip-exact (%.17g) dump so findings are reproducible offline.
                const Tri& tri = tris_[inc[0].tri];
                const VertexId other = inc[1].apex;
                std::fprintf(stderr,
                             "debug_audit CDT-optimality violation #%lu: tri (%.17g,%.17g) "
                             "(%.17g,%.17g) (%.17g,%.17g) apex (%.17g,%.17g)\n",
                             cdt_violations, vertices_[tri.v[0]].x, vertices_[tri.v[0]].y,
                             vertices_[tri.v[1]].x, vertices_[tri.v[1]].y, vertices_[tri.v[2]].x,
                             vertices_[tri.v[2]].y, vertices_[other].x, vertices_[other].y);
            }
            KERNEL_ASSERT(!kCdtOptimalityFatal,
                          "local constrained-Delaunay property violated on a "
                          "non-constrained edge (confirmed by both directional tests)");
        }
    }
}

}  // namespace ingeneer::surface
