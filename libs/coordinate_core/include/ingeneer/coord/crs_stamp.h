// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/coord/crs_stamp.h
//
// R-4.4 recompute invariant ("CRS changes MUST force recomputation"; carried
// TOTaLi invariant) — the load-bearing deliverable of coordinate_core.
//
// A derived/cached result (a survey computation, a certified output, a tessellated
// surface, ...) is only valid relative to the CRS it was computed under. If the
// active CRS changes, the cached result is STALE and must not be silently reused.
//
// CrsStamped<T> binds a value to the Crs it was produced under and refuses to hand
// back the value under a different CRS without an explicit recompute. needs_recompute
// makes the staleness query explicit so a downstream consumer cannot accidentally
// keep a stale result after a CRS change: the only way to read a stamped value is to
// present the CRS it must match, or to ask needs_recompute first.
//
// This is pure value logic (no PROJ, no I/O) so it instruments cleanly under every
// sanitizer and is the determinism-relevant half of the module.

#ifndef INGENEER_COORD_CRS_STAMP_H
#define INGENEER_COORD_CRS_STAMP_H

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include "ingeneer/coord/crs.h"

namespace ingeneer::coord {

// A value `T` stamped with the Crs it was computed under.
//
// Usage pattern (enforcing R-4.4):
//
//     CrsStamped<SurveyResult> cache = stamp(crsA, compute(crsA));
//     ...
//     if (cache.needs_recompute(activeCrs)) {            // crs changed -> stale
//         cache = stamp(activeCrs, compute(activeCrs));   // forced recompute
//     }
//     const SurveyResult& r = cache.value_for(activeCrs).value();  // CRS-checked read
//
template <typename T>
class CrsStamped {
public:
    CrsStamped(Crs crs, T value) : crs_(std::move(crs)), value_(std::move(value)) {}

    // The CRS this result was computed under.
    [[nodiscard]] const Crs& crs() const noexcept { return crs_; }

    // R-4.4 core query: true iff `current` differs from the stamped CRS, i.e. the
    // cached value is stale and a downstream consumer MUST recompute before use.
    // Re-stating the original CRS returns false (no spurious recompute).
    [[nodiscard]] bool needs_recompute(const Crs& current) const noexcept {
        return crs_ != current;
    }

    // Unchecked accessors. Use only when the caller has already established the CRS
    // matches (e.g. immediately after construction). Prefer value_for for the guard.
    [[nodiscard]] const T& value() const noexcept { return value_; }
    [[nodiscard]] T& value() noexcept { return value_; }

    // CRS-checked read: returns the value ONLY if `current` matches the stamped CRS,
    // otherwise std::nullopt (forcing the caller to recompute). This is the safe
    // accessor that makes a silent stale read impossible.
    [[nodiscard]] std::optional<std::reference_wrapper<const T>> value_for(
        const Crs& current) const noexcept {
        if (needs_recompute(current)) {
            return std::nullopt;
        }
        return std::cref(value_);
    }

private:
    Crs crs_;
    T value_;
};

// Convenience factory (CTAD-friendly): stamp a freshly computed value with its CRS.
template <typename T>
[[nodiscard]] CrsStamped<std::decay_t<T>> stamp(Crs crs, T&& value) {
    return CrsStamped<std::decay_t<T>>(std::move(crs), std::forward<T>(value));
}

}  // namespace ingeneer::coord

#endif  // INGENEER_COORD_CRS_STAMP_H
