// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/coord/crs.cpp — PROJ-backed implementation of crs.h.
//
// PROJ (https://proj.org/) is a SYSTEM dependency (Homebrew, MIT/X11; see README),
// NOT vendored into third_party/. All PROJ types are confined to this TU; the public
// header exposes only opaque void* handles owned by RAII.
//
// Thread-safety: each Transform owns its own PJ_CONTEXT (PROJ contexts are NOT
// thread-safe). A Transform is therefore not shareable across threads, but distinct
// Transform objects on distinct threads are fully independent.

#include "ingeneer/coord/crs.h"

#include <proj.h>

#include <cmath>
#include <string>
#include <utility>

namespace ingeneer::coord {
namespace {

// RAII for a PROJ context used transiently during CRS canonicalization.
struct ContextGuard {
    PJ_CONTEXT* ctx = nullptr;
    explicit ContextGuard(PJ_CONTEXT* c) noexcept : ctx(c) {}
    ~ContextGuard() {
        if (ctx != nullptr) {
            proj_context_destroy(ctx);
        }
    }
    ContextGuard(const ContextGuard&) = delete;
    ContextGuard& operator=(const ContextGuard&) = delete;
};

struct PjGuard {
    PJ* pj = nullptr;
    explicit PjGuard(PJ* p) noexcept : pj(p) {}
    ~PjGuard() {
        if (pj != nullptr) {
            proj_destroy(pj);
        }
    }
    PjGuard(const PjGuard&) = delete;
    PjGuard& operator=(const PjGuard&) = delete;
};

bool all_finite(const Coordinate& c) noexcept {
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

std::string context_error(PJ_CONTEXT* ctx) {
    const int err = proj_context_errno(ctx);
    const char* msg = proj_context_errno_string(ctx, err);
    return msg != nullptr ? std::string(msg) : std::string("unknown PROJ error");
}

}  // namespace

std::expected<Crs, CrsError> make_crs(std::string_view definition) {
    if (definition.empty()) {
        return std::unexpected(CrsError{CrsErrc::InvalidDefinition, "empty CRS definition"});
    }

    PJ_CONTEXT* ctx = proj_context_create();
    if (ctx == nullptr) {
        return std::unexpected(
            CrsError{CrsErrc::ProjInitFailure, "proj_context_create returned null"});
    }
    ContextGuard ctx_guard(ctx);

    // PROJ accepts authority codes ("EPSG:6340"), WKT2, and PROJ strings here, all via
    // the same entry point — no per-form branching, no US-specific handling.
    const std::string def_str(definition);
    PJ* obj = proj_create(ctx, def_str.c_str());
    if (obj == nullptr) {
        return std::unexpected(CrsError{CrsErrc::InvalidDefinition, context_error(ctx)});
    }
    PjGuard obj_guard(obj);

    // Canonicalize to PROJ's own PROJ-string serialization so equivalent definitions
    // (e.g. "EPSG:4326" supplied twice, or a code vs its expanded form) compare equal.
    const char* canonical = proj_as_proj_string(ctx, obj, PJ_PROJ_5, nullptr);
    if (canonical == nullptr || canonical[0] == '\0') {
        return std::unexpected(
            CrsError{CrsErrc::InvalidDefinition,
                     "PROJ could not serialize a canonical definition (not a CRS?): " + def_str});
    }

    return Crs(std::string(canonical));
}

// ---- Transform ----------------------------------------------------------

Transform::Transform(void* ctx, void* pj, Crs source, Crs target) noexcept
    : ctx_(ctx), pj_(pj), source_(std::move(source)), target_(std::move(target)) {}

Transform::Transform(Transform&& other) noexcept
    : ctx_(other.ctx_),
      pj_(other.pj_),
      source_(std::move(other.source_)),
      target_(std::move(other.target_)) {
    other.ctx_ = nullptr;
    other.pj_ = nullptr;
}

Transform& Transform::operator=(Transform&& other) noexcept {
    if (this != &other) {
        if (pj_ != nullptr) {
            proj_destroy(static_cast<PJ*>(pj_));
        }
        if (ctx_ != nullptr) {
            proj_context_destroy(static_cast<PJ_CONTEXT*>(ctx_));
        }
        ctx_ = other.ctx_;
        pj_ = other.pj_;
        source_ = std::move(other.source_);
        target_ = std::move(other.target_);
        other.ctx_ = nullptr;
        other.pj_ = nullptr;
    }
    return *this;
}

Transform::~Transform() {
    if (pj_ != nullptr) {
        proj_destroy(static_cast<PJ*>(pj_));
    }
    if (ctx_ != nullptr) {
        proj_context_destroy(static_cast<PJ_CONTEXT*>(ctx_));
    }
}

std::expected<Transform, CrsError> Transform::create(const Crs& source, const Crs& target) {
    if (!source.is_set() || !target.is_set()) {
        return std::unexpected(
            CrsError{CrsErrc::InvalidDefinition, "transform endpoint CRS is unset"});
    }

    PJ_CONTEXT* ctx = proj_context_create();
    if (ctx == nullptr) {
        return std::unexpected(
            CrsError{CrsErrc::ProjInitFailure, "proj_context_create returned null"});
    }

    PJ* pj = proj_create_crs_to_crs(ctx, source.definition().c_str(), target.definition().c_str(),
                                    nullptr);
    if (pj == nullptr) {
        std::string msg = context_error(ctx);
        proj_context_destroy(ctx);
        return std::unexpected(CrsError{CrsErrc::UndefinedTransform, std::move(msg)});
    }

    // Normalize so coordinates are always presented in visualization order
    // (easting/northing, lon/lat) regardless of the CRS's native axis order — this
    // is what makes the (x, y, z) contract in the header hold for every CRS.
    PJ* norm = proj_normalize_for_visualization(ctx, pj);
    if (norm == nullptr) {
        std::string msg = context_error(ctx);
        proj_destroy(pj);
        proj_context_destroy(ctx);
        return std::unexpected(CrsError{CrsErrc::ProjInitFailure, std::move(msg)});
    }
    proj_destroy(pj);

    return Transform(ctx, norm, source, target);
}

std::expected<Coordinate, CrsError> Transform::forward(const Coordinate& in) const {
    if (!all_finite(in)) {
        return std::unexpected(CrsError{CrsErrc::NonFiniteInput, "non-finite input coordinate"});
    }

    PJ_COORD c = proj_coord(in.x, in.y, in.z, 0.0);
    const PJ_COORD r = proj_trans(static_cast<PJ*>(pj_), PJ_FWD, c);

    if (!std::isfinite(r.xyz.x) || !std::isfinite(r.xyz.y) || !std::isfinite(r.xyz.z)) {
        const int err = proj_errno(static_cast<PJ*>(pj_));
        const char* m = proj_context_errno_string(static_cast<PJ_CONTEXT*>(ctx_), err);
        return std::unexpected(
            CrsError{CrsErrc::NonFiniteResult,
                     m != nullptr ? std::string(m) : std::string("non-finite transform result")});
    }

    return Coordinate{r.xyz.x, r.xyz.y, r.xyz.z};
}

std::expected<std::vector<Coordinate>, CrsError> Transform::forward(
    std::span<const Coordinate> in) const {
    std::vector<Coordinate> out;
    out.reserve(in.size());
    for (const Coordinate& c : in) {
        auto r = forward(c);
        if (!r.has_value()) {
            return std::unexpected(r.error());
        }
        out.push_back(*r);
    }
    return out;
}

std::string proj_version() {
    const PJ_INFO info = proj_info();
    return std::to_string(info.major) + "." + std::to_string(info.minor) + "." +
           std::to_string(info.patch);
}

}  // namespace ingeneer::coord
