// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/surface/volume.h — cut/fill volumes over the TIN (Phase 6.3).
//
// TIN-prism method (plan §4.2 item 4): per triangle, the signed prism volume is
// V = A_plan * (d1 + d2 + d3) / 3 with d_i the per-vertex elevation difference — exact for
// linear TINs. Triangles where d changes sign are split exactly along the d = 0 line
// (clipping the linear field's positive part), so `cut` and `fill` are individually exact,
// not just their difference. Zero-d vertices contribute zero volume regardless of side, so
// the sign classification needs no symbolic perturbation.
//
// Sign convention (cut/fill to grade):
//   d = z_existing - z_reference
//   cut  = ∫ max(d, 0)  — existing surface ABOVE the reference: material to remove;
//   fill = ∫ max(-d, 0) — existing surface BELOW the reference: material to add.
// Both are reported non-negative; net() = cut - fill (positive => net excavation).
//
// Two-surface volumes: `volume_between` handles two INDEPENDENT triangulations (the real
// design-vs-existing surveying case) by pairwise triangle overlay. For every triangle pair
// (tA, tB) whose xy projections properly overlap (exact-predicate separating-axis test;
// point/segment contacts contribute exactly zero), the convex intersection polygon is
// computed (Sutherland–Hodgman with orient2d vertex classification; intersection points by
// double interpolation). Both surfaces are linear over that polygon, so the difference
// field d = z_existing - z_design is linear there and its positive/negative parts integrate
// exactly via the same prism clipping as volume_to_plane — exact for piecewise-linear
// surfaces up to double rounding, no sampling or rasterization. Volumes are accumulated
// over the INTERSECTION of the two convex hulls; `VolumeResult::area` reports that overlap
// plan area so callers can detect partial coverage. When both TINs share identical point
// support (identical vertex xy per id, identical triangle set) an O(n) fast path is taken;
// it produces the same result as the overlay. Pairing uses a sort-by-bbox prefilter;
// an asymptotically optimal plane-sweep/DCEL overlay is noted as future work.
//
// Prismoidal formula for corridor/alignment reports (DOT manuals): V = L/6 (A1 + 4 Am + A2),
// exact for prismatoids (area varying quadratically along the axis). Average-end-area is
// provided ONLY as an explicitly labeled report option (it overestimates tapering solids).
//
// Interpolation/summation arithmetic is plain double; no classification here affects mesh
// topology.
#ifndef INGENEER_SURFACE_VOLUME_H
#define INGENEER_SURFACE_VOLUME_H

#include <expected>

#include "ingeneer/surface/tin.h"

namespace ingeneer::surface {

enum class VolumeErrc {
    NonFiniteInput,  // NaN or +-inf reference plane
    // UnsharedSupport was removed when the independent-triangulation overlay landed:
    // volume_between now handles arbitrary supports, so the condition no longer exists.
};

struct VolumeError {
    VolumeErrc code;
};

// Earthwork quantities in cubic metres (both non-negative; see sign convention above).
// `area` is the plan area (square metres) of the region the volumes were integrated over:
// the TIN footprint for volume_to_plane, the intersection of the two convex hulls for
// volume_between. Callers compare it against their expected footprint to detect partial
// hull coverage.
struct VolumeResult {
    double cut = 0.0;
    double fill = 0.0;
    double area = 0.0;
    double net() const noexcept { return cut - fill; }
};

// Cut/fill between the TIN (existing surface) and a horizontal reference plane, over the
// TIN's plan footprint (its convex hull).
std::expected<VolumeResult, VolumeError> volume_to_plane(const Tin& existing, double plane_z);

// Cut/fill between two surfaces over the intersection of their convex hulls. The
// triangulations may be fully INDEPENDENT (no shared point support) — see the overlay
// description in the file header. d = z_existing - z_design; cut is existing above design.
// A TIN with no finite triangles yields an empty overlap (all-zero result). Shared-support
// inputs (identical vertex xy per id and identical triangle sets) take an O(n) fast path
// with identical semantics. Never fails today; the error channel is kept for API symmetry
// with volume_to_plane and future diagnostics.
std::expected<VolumeResult, VolumeError> volume_between(const Tin& design, const Tin& existing);

// Prismoidal volume of one corridor interval: V = length/6 * (area1 + 4*area_mid + area2).
double prismoidal_volume(double area1, double area_mid, double area2, double length);

// AVERAGE END AREA — REPORT OPTION ONLY (labeled per plan; overestimates tapering solids;
// prefer prismoidal_volume). V = length/2 * (area1 + area2).
double average_end_area_volume(double area1, double area2, double length);

}  // namespace ingeneer::surface

#endif  // INGENEER_SURFACE_VOLUME_H
