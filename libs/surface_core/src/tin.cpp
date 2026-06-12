#include "ingeneer/surface/tin.h"

#include <cmath>
#include <unordered_map>
#include <vector>

#include "ingeneer/geom/predicates.h"

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

    // Fallback: exhaustive conflict scan (exact and always correct; reachable only if
    // the walk cycles on adversarial degeneracies).
    for (TriId t = 0; t < tris_.size(); ++t) {
        if (tris_[t].alive && conflicts(tris_[t], px, py)) return t;
    }
    return kNoTriangle;
}

std::expected<VertexId, TinError> Tin::insert(double x, double y, double z) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return std::unexpected(TinError{TinErrc::NonFiniteCoordinate});
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
        return fresh;
    }

    VertexId dup = kGhostVertex;
    const TriId seed = locate(x, y, dup);
    if (dup != kGhostVertex) return dup;
    if (seed == kNoTriangle) return std::unexpected(TinError{TinErrc::WalkOverflow});

    vertices_.push_back(TinVertex{x, y, z});
    const VertexId p = static_cast<VertexId>(vertices_.size() - 1);
    if (!cavity_insert(p, seed)) {
        return std::unexpected(TinError{TinErrc::WalkOverflow});
    }
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
        // path is reachable only via exotic walk terminations. Exhaustive recovery scan.
        start = kNoTriangle;
        for (TriId t = 0; t < tris_.size(); ++t) {
            if (tris_[t].alive && conflicts(tris_[t], x, y)) {
                start = t;
                break;
            }
        }
        if (start == kNoTriangle) return false;
    }

    // ---- conflict BFS -------------------------------------------------------------
    std::vector<TriId> cavity;
    std::vector<TriId> stack{start};
    std::vector<bool> in_cavity(tris_.size(), false);
    in_cavity[start] = true;
    while (!stack.empty()) {
        const TriId t = stack.back();
        stack.pop_back();
        cavity.push_back(t);
        for (int i = 0; i < 3; ++i) {
            const TriId m = tris_[t].n[static_cast<std::size_t>(i)];
            if (m == kNoTriangle || in_cavity[m] || !tris_[m].alive) continue;
            if (conflicts(tris_[m], x, y)) {
                in_cavity[m] = true;
                stack.push_back(m);
            }
        }
    }

    // ---- boundary edges -> new triangles (u, w, p) ----------------------------------
    struct NewTri {
        VertexId u, w;
        TriId id, outside;
    };
    std::vector<NewTri> fresh;
    for (const TriId t : cavity) {
        for (int i = 0; i < 3; ++i) {
            const TriId m = tris_[t].n[static_cast<std::size_t>(i)];
            if (m != kNoTriangle && in_cavity[m]) continue;
            const VertexId u = tris_[t].v[static_cast<std::size_t>((i + 1) % 3)];
            const VertexId w = tris_[t].v[static_cast<std::size_t>((i + 2) % 3)];
            fresh.push_back(NewTri{u, w, kNoTriangle, m});
        }
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

}  // namespace ingeneer::surface
