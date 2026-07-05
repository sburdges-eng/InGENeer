// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/coord/crs.h
//
// Coordinate reference system (CRS) identity + PROJ-backed transforms (Phase 7).
//
// Design constraints (enforced by this header / its tests):
//
//   * GENERIC, NOT US-only. Per ADR-0010 the v1 PRODUCT scope is US-only, but
//     this CRS LAYER bakes in ZERO US assumptions: a Crs is constructed from an
//     arbitrary authority code (e.g. "EPSG:6340"), WKT2, or a PROJ string, all
//     resolved through system PROJ. Widening the product later is configuration,
//     not a rewrite. No State-Plane / NAD83 / US-survey-foot literals live here.
//
//   * No raw PROJ types in the public surface. PROJ owns `PJ*` / `PJ_CONTEXT*`;
//     those are NOT exact-predicate-safe and are NOT thread-safe. They are held
//     by RAII inside the .cpp only. A Crs is a cheap, copyable value type that
//     carries the canonical (PROJ-normalized) definition string as its identity.
//
//   * Identity drives the R-4.4 recompute invariant. Two Crs values are equal
//     iff PROJ normalizes them to the same definition. `CrsStamped<T>` binds a
//     derived/cached result to the Crs it was produced under so that a CRS change
//     MUST be observed by downstream consumers (see crs_stamp.h). This is the
//     carried TOTaLi invariant ("CRS changes MUST force recomputation", R-4.4).
//
//   * Determinism: PROJ is deterministic for a fixed version. proj_version() is
//     exposed for provenance so audit/oracle layers can pin it. Transform math is
//     PROJ's own IEEE-754 — this module is NOT exact-predicate territory and does
//     NOT route through geometry_core.
//
//   * Thread-safety policy: PROJ contexts are not thread-safe, so each Transform
//     owns its own PJ_CONTEXT and PJ. A Transform is therefore NOT shareable
//     across threads without external synchronization; distinct Transform objects
//     on distinct threads are independent. Crs values (plain strings) are freely
//     copyable and comparable across threads. Documented again on the types below.

#ifndef INGENEER_COORD_CRS_H
#define INGENEER_COORD_CRS_H

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ingeneer::coord {

// ---- Error model (C-4.5: std::expected<T, *Error>) ----------------------

enum class CrsErrc {
    InvalidDefinition,   // PROJ could not build a CRS from the given string
    UndefinedTransform,  // no transform pipeline exists between source and target
    NonFiniteInput,      // a coordinate was NaN or +-inf
    NonFiniteResult,     // PROJ produced a non-finite coordinate (out of domain)
    ProjInitFailure,     // PROJ context / object could not be created (allocation, etc.)
};

struct CrsError {
    CrsErrc code;
    std::string message;  // PROJ diagnostic text where available; never US-specific
};

// ---- Coordinate value type ----------------------------------------------

// A coordinate triple in the units/axis-order of whichever Crs it belongs to.
// PROJ normalizes axis order to easting/northing (x/y) and longitude/latitude
// (x=lon, y=lat) via proj_normalize_for_visualization, so callers always pass
// (x, y, z) in that visualization order regardless of the CRS's native axes.
struct Coordinate {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;  // ellipsoidal height / orthometric height per CRS; 0 for 2D CRS

    friend bool operator==(const Coordinate&, const Coordinate&) = default;
};

// ---- Crs: a coordinate reference system identity ------------------------

// Cheap, copyable, comparable value type. Equality == CRS identity: two Crs are
// equal iff PROJ normalizes their definitions to the same canonical string. This
// is what the R-4.4 recompute guard keys on.
//
// Construct via the factory functions below (they validate through PROJ);
// a default-constructed Crs is the empty/"unset" CRS, equal only to itself.
class Crs {
public:
    Crs() = default;  // unset CRS; not usable as a transform endpoint

    // Canonical PROJ-normalized definition string; "" for an unset Crs.
    [[nodiscard]] const std::string& definition() const noexcept { return definition_; }
    [[nodiscard]] bool is_set() const noexcept { return !definition_.empty(); }

    // Identity comparison (the load-bearing operation for R-4.4).
    friend bool operator==(const Crs& a, const Crs& b) noexcept {
        return a.definition_ == b.definition_;
    }
    friend bool operator!=(const Crs& a, const Crs& b) noexcept { return !(a == b); }

private:
    friend std::expected<Crs, CrsError> make_crs(std::string_view);
    explicit Crs(std::string canonical) : definition_(std::move(canonical)) {}

    std::string definition_;
};

// Build a Crs from ANY PROJ-acceptable definition: an authority code such as
// "EPSG:6340", a full WKT2 string, or a PROJ pipeline/proj-string. The input is
// resolved and re-serialized to PROJ's canonical form so that equivalent
// definitions compare equal. Returns InvalidDefinition on a string PROJ rejects.
[[nodiscard]] std::expected<Crs, CrsError> make_crs(std::string_view definition);

// ---- Transform: source -> target, PROJ-backed ---------------------------

// Owns one PROJ context + transformation object (RAII). Built once for a
// (source, target) pair and reused for many points. NOT thread-safe: do not
// share one Transform across threads. Move-only.
class Transform {
public:
    // Construct a transform between two set CRS. Returns UndefinedTransform if
    // PROJ knows of no pipeline, ProjInitFailure on context/object allocation
    // failure, InvalidDefinition if either Crs is unset.
    [[nodiscard]] static std::expected<Transform, CrsError> create(const Crs& source,
                                                                   const Crs& target);

    Transform(Transform&&) noexcept;
    Transform& operator=(Transform&&) noexcept;
    Transform(const Transform&) = delete;
    Transform& operator=(const Transform&) = delete;
    ~Transform();

    [[nodiscard]] const Crs& source() const noexcept { return source_; }
    [[nodiscard]] const Crs& target() const noexcept { return target_; }

    // Transform a single coordinate. NonFiniteInput if any input is non-finite;
    // NonFiniteResult if PROJ yields a non-finite coordinate (out of domain).
    [[nodiscard]] std::expected<Coordinate, CrsError> forward(const Coordinate& in) const;

    // Transform a batch. On the first failing point the whole call fails with that
    // point's error (deterministic, no partial output). `out` must be empty or it
    // is cleared. Output preserves input order and count on success.
    [[nodiscard]] std::expected<std::vector<Coordinate>, CrsError> forward(
        std::span<const Coordinate> in) const;

private:
    Transform(void* ctx, void* pj, Crs source, Crs target) noexcept;

    void* ctx_ = nullptr;  // PJ_CONTEXT* (opaque here; owned)
    void* pj_ = nullptr;   // PJ* (opaque here; owned)
    Crs source_;
    Crs target_;
};

// ---- Provenance ----------------------------------------------------------

// PROJ runtime version string (e.g. "9.8.1"). Recorded by audit/oracle layers so
// a certified output can be pinned to the exact PROJ that produced it.
[[nodiscard]] std::string proj_version();

}  // namespace ingeneer::coord

#endif  // INGENEER_COORD_CRS_H
