// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/surface/contour.h — TIN contour extraction (Phase 6.3).
//
// Algorithm (plan §4.2 item 3): for a level z, each finite triangle whose vertices straddle
// the level yields exactly one segment via linear interpolation on the two crossed edges;
// segments are chained into polylines by hashing the crossed mesh edge (vertex-id pair), so
// chaining is purely combinatorial and never compares floating-point coordinates.
//
// Degeneracy rule (symbolic perturbation): a vertex whose z is EXACTLY the contour level is
// treated as lying at z + ε, i.e. strictly ABOVE the level. Classification is therefore the
// exact double comparison `z >= level` — no rounding occurs in the comparison itself, so
// every topological decision is robust (no FP tie-breaking hacks). Consequences:
//   * an edge with one endpoint exactly on the level is crossed iff the other endpoint is
//     below; the intersection is then the vertex itself (emitted bit-exactly, so duplicate
//     squashing during chaining is exact);
//   * a contour through an isolated peak vertex sitting exactly on the level degenerates to
//     a single point and is dropped (the ε-limit of the loop is empty);
//   * a flat region exactly at the level is "above" and produces no interior segments.
//
// Closed contours close (the chain returns to its starting mesh edge; `closed == true`,
// last point connects implicitly back to the first). Open contours terminate on hull edges
// (a crossed edge owned by a single finite triangle).
//
// Interpolation arithmetic is plain double (allowed; only classification must be exact).
// The crossed edge's endpoints are ordered by vertex id before interpolating so both
// triangles sharing the edge compute the bit-identical point.
#ifndef INGENEER_SURFACE_CONTOUR_H
#define INGENEER_SURFACE_CONTOUR_H

#include <expected>
#include <vector>

#include "ingeneer/surface/tin.h"

namespace ingeneer::surface {

enum class ContourErrc {
    NonFiniteLevel,  // NaN or +-inf contour level
};

struct ContourError {
    ContourErrc code;
};

struct ContourPoint {
    double x;
    double y;
};

// One raw (authoritative) contour polyline at a fixed level. For closed contours the last
// point connects implicitly back to the first (no duplicated closing point).
struct Contour {
    std::vector<ContourPoint> points;
    bool closed = false;
};

// All contours of one level. A level below/above the whole surface yields an empty
// `contours` vector — that is a valid result, not an error.
struct ContourLevel {
    double level = 0.0;
    std::vector<Contour> contours;
};

// Extract the raw contours of `tin` at `level`. This output is the AUTHORITATIVE survey
// deliverable; any smoothed variant is derived (see SmoothedContour).
std::expected<ContourLevel, ContourError> extract_contours(const Tin& tin, double level);

// DERIVED / NON-AUTHORITATIVE smoothed contour (C-1.3 analog): cosmetic output for plots
// only. Never feed back into quantities, never replaces the raw Contour of record. The
// distinct type (plus `kDerived`) makes accidental substitution a compile-time mismatch.
struct SmoothedContour {
    static constexpr bool kDerived = true;  // non-authoritative marker
    std::vector<ContourPoint> points;
    bool closed = false;
};

// Chaikin corner-cutting. Open polylines keep their exact endpoints (hull termination is
// preserved); closed polylines smooth across the wrap. `iterations < 1` returns the input
// points unchanged (still tagged derived).
SmoothedContour chaikin_smooth(const Contour& raw, int iterations = 2);

}  // namespace ingeneer::surface

#endif  // INGENEER_SURFACE_CONTOUR_H
