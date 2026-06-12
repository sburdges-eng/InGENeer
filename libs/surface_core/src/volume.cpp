// SPDX-License-Identifier: Apache-2.0
//
// Cut/fill volumes over the TIN (Phase 6.3). See volume.h for the method and the sign
// convention. Consumes the Tin strictly through its public read-only API.
//
// Two-surface volumes between INDEPENDENT triangulations use a pairwise triangle overlay:
// exact-predicate (orient2d) separating-axis rejection of zero-area contacts, exact-
// classified Sutherland–Hodgman clipping of each properly-overlapping pair, then the same
// positive-part prism integration as volume_to_plane over the fan of the intersection
// polygon. All TOPOLOGY decisions (vertex sidedness, overlap rejection) go through exact
// predicates; intersection coordinates and the linear-field measure arithmetic are plain
// double per the numeric policy. The pairing prefilter is sort-by-bbox-xmin with an early
// break; an asymptotically optimal plane-sweep/DCEL overlay is future work.
//
// Independent cross-check: tools/oracle/extract_from_totali.py reimplements the positive-
// part clipping in numpy over the pinned TOTaLi survey corpus and pins cut/fill constants
// in tests/fixtures/oracle/totali-corpus-500pt-cv-v1.txt (TOTaLi itself has no volume
// pipeline — ADR-0023 fallback path).
#include "ingeneer/surface/volume.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ingeneer::surface {

namespace {

using geom::predicates::AABB2;
using geom::predicates::bbox_overlap_2d;
using geom::predicates::orient2d;
using geom::predicates::Orientation;
using geom::predicates::Point2;

struct Pd {
    double x;
    double y;
    double d;  // linear difference field at this plan position
};

// Volume of the positive part of the linear field d over one triangle: clip the triangle
// against d >= 0 (vertices with d >= 0 kept; strict sign changes insert a d == 0 point),
// then integrate the linear field over the clipped convex polygon by fanning. Exact for a
// linear TIN up to double rounding; a vertex with d == 0 contributes zero volume either
// way, so the classification needs no perturbation.
double positive_part_volume(const std::array<Pd, 3>& t) noexcept {
    Pd poly[5];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const Pd& a = t[static_cast<std::size_t>(i)];
        const Pd& b = t[static_cast<std::size_t>((i + 1) % 3)];
        if (a.d >= 0.0) poly[n++] = a;
        if ((a.d > 0.0 && b.d < 0.0) || (a.d < 0.0 && b.d > 0.0)) {
            const double s = a.d / (a.d - b.d);
            poly[n++] = Pd{a.x + s * (b.x - a.x), a.y + s * (b.y - a.y), 0.0};
        }
    }
    if (n < 3) return 0.0;
    double v = 0.0;
    for (int k = 1; k + 1 < n; ++k) {
        const double ax = poly[k].x - poly[0].x;
        const double ay = poly[k].y - poly[0].y;
        const double bx = poly[k + 1].x - poly[0].x;
        const double by = poly[k + 1].y - poly[0].y;
        const double area = 0.5 * std::fabs(ax * by - ay * bx);
        v += area * (poly[0].d + poly[k].d + poly[k + 1].d) / 3.0;
    }
    return v;
}

void accumulate(const std::array<Pd, 3>& tri, VolumeResult& acc) noexcept {
    acc.cut += positive_part_volume(tri);
    acc.fill +=
        positive_part_volume({Pd{tri[0].x, tri[0].y, -tri[0].d}, Pd{tri[1].x, tri[1].y, -tri[1].d},
                              Pd{tri[2].x, tri[2].y, -tri[2].d}});
}

// Plan area of the triangle (p0, p1, p2).
double plan_area(const Pd& p0, const Pd& p1, const Pd& p2) noexcept {
    return 0.5 * std::fabs((p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x));
}

// Canonical triangle set for the shared-support check.
std::vector<std::array<VertexId, 3>> canonical_triangles(const Tin& tin) {
    auto tris = tin.triangles();
    for (auto& t : tris) std::sort(t.begin(), t.end());
    std::sort(tris.begin(), tris.end());
    return tris;
}

// True iff both TINs have identical point support: same vertex count, bit-identical xy
// per vertex id, identical triangle sets. Enables the O(n) shared-support fast path.
bool shared_support(const Tin& design, const Tin& existing) {
    if (design.vertex_count() != existing.vertex_count()) return false;
    const std::size_t n = design.vertex_count();
    for (std::size_t i = 0; i < n; ++i) {
        const TinVertex& a = design.vertex(static_cast<VertexId>(i));
        const TinVertex& b = existing.vertex(static_cast<VertexId>(i));
        if (a.x != b.x || a.y != b.y) return false;
    }
    return canonical_triangles(design) == canonical_triangles(existing);
}

// ---- independent-triangulation overlay --------------------------------------------------

struct OverlayTri {
    std::array<Point2, 3> p;  // CCW plan vertices (Tin::triangles() yields CCW triples)
    std::array<double, 3> z;
    AABB2 box;
};

std::vector<OverlayTri> overlay_tris(const Tin& tin) {
    std::vector<OverlayTri> out;
    out.reserve(tin.triangle_count());
    for (const auto& tri : tin.triangles()) {
        OverlayTri t;
        for (std::size_t i = 0; i < 3; ++i) {
            const TinVertex& v = tin.vertex(tri[i]);
            t.p[i] = Point2{v.x, v.y};
            t.z[i] = v.z;
        }
        t.box = AABB2{
            std::min({t.p[0].x, t.p[1].x, t.p[2].x}), std::min({t.p[0].y, t.p[1].y, t.p[2].y}),
            std::max({t.p[0].x, t.p[1].x, t.p[2].x}), std::max({t.p[0].y, t.p[1].y, t.p[2].y})};
        out.push_back(t);
    }
    return out;
}

// z of the triangle's supporting plane at (x, y): barycentric interpolation in plain
// double (measure arithmetic; never a topology decision). Exact at the triangle's own
// vertices (the weight ratios reduce to exactly 1 and 0 there).
double plane_z_at(const OverlayTri& t, double x, double y) noexcept {
    const double area2 = (t.p[1].x - t.p[0].x) * (t.p[2].y - t.p[0].y) -
                         (t.p[1].y - t.p[0].y) * (t.p[2].x - t.p[0].x);
    const double wa = ((t.p[1].x - x) * (t.p[2].y - y) - (t.p[1].y - y) * (t.p[2].x - x)) / area2;
    const double wb = ((t.p[2].x - x) * (t.p[0].y - y) - (t.p[2].y - y) * (t.p[0].x - x)) / area2;
    return wa * t.z[0] + wb * t.z[1] + (1.0 - wa - wb) * t.z[2];
}

// Exact separating-axis test for two CCW triangles: true iff their OPEN intersection is
// nonempty (positive plan area). Pairs touching at a point or along a segment return
// false, so degenerate contacts contribute exactly zero — no clipping, no rounding.
// For convex shapes a separating line, if one exists, can always be chosen through an
// edge of one of them: an edge (p, q) separates iff no vertex of the other triangle is
// strictly LEFT of it (the triangle owning the edge lies in the closed left half-plane).
bool properly_overlap(const OverlayTri& a, const OverlayTri& b) noexcept {
    const auto separates = [](const Point2& p, const Point2& q,
                              const std::array<Point2, 3>& other) noexcept {
        for (const Point2& r : other) {
            if (orient2d(p, q, r) == Orientation::LEFT) return false;
        }
        return true;
    };
    for (std::size_t i = 0; i < 3; ++i) {
        if (separates(a.p[i], a.p[(i + 1) % 3], b.p)) return false;
        if (separates(b.p[i], b.p[(i + 1) % 3], a.p)) return false;
    }
    return true;
}

// Convex clip polygon: a triangle cut by up to three half-planes has at most 6 vertices;
// 16 leaves headroom for near-degenerate sign patterns on computed points.
struct ClipPoly {
    std::array<Point2, 16> pt;
    std::size_t n = 0;
};

// One Sutherland–Hodgman pass: keep the part of `poly` in the closed LEFT half-plane of
// the directed clip edge c1 -> c2. Vertex sidedness is classified by the exact orient2d
// predicate (a vertex exactly ON the clip line is kept and never spawns a crossing, so
// collinear/touching boundaries cannot double-count); crossing points between strictly
// opposite sides are computed by double interpolation (measure arithmetic).
void clip_half_plane(ClipPoly& poly, const Point2& c1, const Point2& c2) noexcept {
    std::array<Point2, 16> out;
    std::size_t m = 0;
    const double ex = c2.x - c1.x;
    const double ey = c2.y - c1.y;
    for (std::size_t i = 0; i < poly.n && m + 2 <= out.size(); ++i) {
        const Point2& s = poly.pt[i];
        const Point2& e = poly.pt[(i + 1) % poly.n];
        const Orientation os = orient2d(c1, c2, s);
        const Orientation oe = orient2d(c1, c2, e);
        if (os != Orientation::RIGHT) out[m++] = s;
        if ((os == Orientation::LEFT && oe == Orientation::RIGHT) ||
            (os == Orientation::RIGHT && oe == Orientation::LEFT)) {
            // Strict crossing: interpolate along s -> e. The doubles cs/ce carry the
            // exact predicate's signs except in extreme underflow; guard the ratio.
            const double cs = ex * (s.y - c1.y) - ey * (s.x - c1.x);
            const double ce = ex * (e.y - c1.y) - ey * (e.x - c1.x);
            const double denom = cs - ce;
            const double t = denom != 0.0 ? cs / denom : 0.5;
            out[m++] = Point2{s.x + t * (e.x - s.x), s.y + t * (e.y - s.y)};
        }
    }
    poly.pt = out;
    poly.n = m;
}

// General two-surface volume: pairwise triangle overlay over the hull intersection.
VolumeResult overlay_volume(const Tin& design, const Tin& existing) {
    VolumeResult acc;
    const std::vector<OverlayTri> design_tris = overlay_tris(design);
    std::vector<OverlayTri> existing_tris = overlay_tris(existing);
    std::sort(existing_tris.begin(), existing_tris.end(),
              [](const OverlayTri& u, const OverlayTri& v) { return u.box.xmin < v.box.xmin; });

    for (const OverlayTri& a : design_tris) {
        for (const OverlayTri& b : existing_tris) {
            if (b.box.xmin > a.box.xmax) break;  // sorted bbox prefilter
            if (!bbox_overlap_2d(a.box, b.box)) continue;
            if (!properly_overlap(a, b)) continue;  // point/segment contact: exactly zero

            ClipPoly poly;
            poly.n = 3;
            poly.pt[0] = a.p[0];
            poly.pt[1] = a.p[1];
            poly.pt[2] = a.p[2];
            for (std::size_t k = 0; k < 3 && poly.n >= 3; ++k) {
                clip_half_plane(poly, b.p[k], b.p[(k + 1) % 3]);
            }
            if (poly.n < 3) continue;

            // Both surfaces are linear over the intersection polygon, so the difference
            // field d = z_existing - z_design is linear there: evaluate it at the polygon
            // vertices and integrate its positive/negative parts over the fan with the
            // same prism clipping used by volume_to_plane.
            std::array<Pd, 16> v{};
            for (std::size_t i = 0; i < poly.n; ++i) {
                const Point2& q = poly.pt[i];
                v[i] = Pd{q.x, q.y, plane_z_at(b, q.x, q.y) - plane_z_at(a, q.x, q.y)};
            }
            for (std::size_t k = 1; k + 1 < poly.n; ++k) {
                accumulate({v[0], v[k], v[k + 1]}, acc);
                acc.area += plan_area(v[0], v[k], v[k + 1]);
            }
        }
    }
    return acc;
}

}  // namespace

std::expected<VolumeResult, VolumeError> volume_to_plane(const Tin& existing, double plane_z) {
    if (!std::isfinite(plane_z)) {
        return std::unexpected(VolumeError{VolumeErrc::NonFiniteInput});
    }
    VolumeResult acc;
    for (const auto& tri : existing.triangles()) {
        std::array<Pd, 3> t;
        for (std::size_t i = 0; i < 3; ++i) {
            const TinVertex& v = existing.vertex(tri[i]);
            t[i] = Pd{v.x, v.y, v.z - plane_z};
        }
        accumulate(t, acc);
        acc.area += plan_area(t[0], t[1], t[2]);
    }
    return acc;
}

std::expected<VolumeResult, VolumeError> volume_between(const Tin& design, const Tin& existing) {
    // Shared point support (identical vertex count, bit-identical xy per vertex id,
    // identical triangle sets): the difference field is linear per design triangle, so
    // integrate directly — O(n) and bit-for-bit what the overlay would produce.
    if (shared_support(design, existing)) {
        VolumeResult acc;
        for (const auto& tri : design.triangles()) {
            std::array<Pd, 3> t;
            for (std::size_t i = 0; i < 3; ++i) {
                const TinVertex& d = design.vertex(tri[i]);
                const TinVertex& e = existing.vertex(tri[i]);
                t[i] = Pd{d.x, d.y, e.z - d.z};  // cut where existing is above design
            }
            accumulate(t, acc);
            acc.area += plan_area(t[0], t[1], t[2]);
        }
        return acc;
    }
    // Independent triangulations: pairwise overlay over the intersection of the hulls.
    return overlay_volume(design, existing);
}

double prismoidal_volume(double area1, double area_mid, double area2, double length) {
    return length / 6.0 * (area1 + 4.0 * area_mid + area2);
}

double average_end_area_volume(double area1, double area2, double length) {
    // REPORT OPTION ONLY — overestimates tapering solids; see volume.h.
    return length / 2.0 * (area1 + area2);
}

}  // namespace ingeneer::surface
