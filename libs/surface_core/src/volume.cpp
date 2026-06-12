// SPDX-License-Identifier: Apache-2.0
//
// Cut/fill volumes over the TIN (Phase 6.3). See volume.h for the method and the sign
// convention. Consumes the Tin strictly through its public read-only API.
//
// Independent cross-check: tools/oracle/extract_from_totali.py reimplements the positive-
// part clipping in numpy over the pinned TOTaLi survey corpus and pins cut/fill constants
// in tests/fixtures/oracle/totali-corpus-500pt-cv-v1.txt (TOTaLi itself has no volume
// pipeline — ADR-0023 fallback path).
#include "ingeneer/surface/volume.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ingeneer::surface {

namespace {

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

// Canonical triangle set for the shared-support check.
std::vector<std::array<VertexId, 3>> canonical_triangles(const Tin& tin) {
    auto tris = tin.triangles();
    for (auto& t : tris) std::sort(t.begin(), t.end());
    std::sort(tris.begin(), tris.end());
    return tris;
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
    }
    return acc;
}

std::expected<VolumeResult, VolumeError> volume_between(const Tin& design, const Tin& existing) {
    // Shared point support: identical vertex count, bit-identical xy per vertex id, and
    // identical triangle sets. The merge of independent triangulations is deferred.
    if (design.vertex_count() != existing.vertex_count()) {
        return std::unexpected(VolumeError{VolumeErrc::UnsharedSupport});
    }
    const std::size_t n = design.vertex_count();
    for (std::size_t i = 0; i < n; ++i) {
        const TinVertex& a = design.vertex(static_cast<VertexId>(i));
        const TinVertex& b = existing.vertex(static_cast<VertexId>(i));
        if (a.x != b.x || a.y != b.y) {
            return std::unexpected(VolumeError{VolumeErrc::UnsharedSupport});
        }
    }
    if (canonical_triangles(design) != canonical_triangles(existing)) {
        return std::unexpected(VolumeError{VolumeErrc::UnsharedSupport});
    }

    VolumeResult acc;
    for (const auto& tri : design.triangles()) {
        std::array<Pd, 3> t;
        for (std::size_t i = 0; i < 3; ++i) {
            const TinVertex& d = design.vertex(tri[i]);
            const TinVertex& e = existing.vertex(tri[i]);
            t[i] = Pd{d.x, d.y, e.z - d.z};  // cut where existing is above design
        }
        accumulate(t, acc);
    }
    return acc;
}

double prismoidal_volume(double area1, double area_mid, double area2, double length) {
    return length / 6.0 * (area1 + 4.0 * area_mid + area2);
}

double average_end_area_volume(double area1, double area2, double length) {
    // REPORT OPTION ONLY — overestimates tapering solids; see volume.h.
    return length / 2.0 * (area1 + area2);
}

}  // namespace ingeneer::surface
