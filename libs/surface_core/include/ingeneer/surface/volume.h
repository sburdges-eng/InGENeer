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
// Two-surface volumes (v1 scope): supported when both TINs share the same point support
// (identical vertex xy per id, identical triangle set), e.g. design and existing sampled on
// the same grid. The general merge of two INDEPENDENT triangulations is deferred (Phase 6.3
// design note): it requires overlaying both TINs into a combined planar subdivision so the
// difference field is piecewise linear on every output face — tracked as deferred work, not
// approximated here.
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
    NonFiniteInput,   // NaN or +-inf reference plane
    UnsharedSupport,  // two-surface volume requested for TINs without identical support
};

struct VolumeError {
    VolumeErrc code;
};

// Earthwork quantities in cubic metres (both non-negative; see sign convention above).
struct VolumeResult {
    double cut = 0.0;
    double fill = 0.0;
    double net() const noexcept { return cut - fill; }
};

// Cut/fill between the TIN (existing surface) and a horizontal reference plane, over the
// TIN's plan footprint (its convex hull).
std::expected<VolumeResult, VolumeError> volume_to_plane(const Tin& existing, double plane_z);

// Cut/fill between two surfaces with SHARED point support: `design` and `existing` must
// have identical vertex count, bit-identical xy per vertex id, and identical triangle
// sets (z may differ). d = z_existing - z_design per vertex; cut is existing above design.
// Returns VolumeErrc::UnsharedSupport otherwise (independent-triangulation merge deferred).
std::expected<VolumeResult, VolumeError> volume_between(const Tin& design, const Tin& existing);

// Prismoidal volume of one corridor interval: V = length/6 * (area1 + 4*area_mid + area2).
double prismoidal_volume(double area1, double area_mid, double area2, double length);

// AVERAGE END AREA — REPORT OPTION ONLY (labeled per plan; overestimates tapering solids;
// prefer prismoidal_volume). V = length/2 * (area1 + area2).
double average_end_area_volume(double area1, double area2, double length);

}  // namespace ingeneer::surface

#endif  // INGENEER_SURFACE_VOLUME_H
