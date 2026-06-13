// SPDX-License-Identifier: Apache-2.0
//
// In-memory point-cloud container (Phase 7). See ingeneer/pointcloud/point_cloud.h.

#include "ingeneer/pointcloud/point_cloud.h"

#include <cmath>
#include <limits>

namespace ingeneer::pointcloud {

namespace {

inline bool finite3(float x, float y, float z) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

// In-core ceiling: PointId is uint32, and kNoPoint (0xFFFFFFFF) is reserved, so the last
// usable id is 0xFFFFFFFE -> at most 0xFFFFFFFF points by count. Adding a point is only
// permitted while point_count() < kMaxPoints.
constexpr std::size_t kMaxPoints = static_cast<std::size_t>(kNoPoint);  // 2^32 - 1

}  // namespace

void PointCloud::grow_present_channels() {
    const std::size_t n = positions_.size();
    if (!intensity_.empty()) {
        intensity_.resize(n, 0);
    }
    if (!classification_.empty()) {
        classification_.resize(n, 0);
    }
    if (!return_number_.empty()) {
        return_number_.resize(n, 0);
    }
    if (!rgb_.empty()) {
        rgb_.resize(n, std::array<std::uint16_t, 3>{0, 0, 0});
    }
}

std::expected<PointId, PointCloudError> PointCloud::add_point(float x, float y, float z) noexcept {
    if (!finite3(x, y, z)) {
        return std::unexpected(PointCloudError{PointCloudErrc::NonFiniteCoordinate});
    }
    if (positions_.size() >= kMaxPoints) {
        return std::unexpected(PointCloudError{PointCloudErrc::CapacityExceeded});
    }
    const auto id = static_cast<PointId>(positions_.size());
    positions_.push_back(Point3f{x, y, z});
    grow_present_channels();
    return id;
}

std::expected<PointId, PointCloudError> PointCloud::add_points(std::span<const Point3f> points) {
    if (points.empty()) {
        return kNoPoint;
    }
    if (positions_.size() > kMaxPoints - points.size()) {
        return std::unexpected(PointCloudError{PointCloudErrc::CapacityExceeded});
    }
    for (const Point3f& p : points) {
        if (!finite3(p.x, p.y, p.z)) {
            return std::unexpected(PointCloudError{PointCloudErrc::NonFiniteCoordinate});
        }
    }
    // All inputs validated up front -> the append below cannot fail, so the operation is
    // atomic: either every point is appended or none is (no partial-state rollback needed).
    const auto first = static_cast<PointId>(positions_.size());
    positions_.insert(positions_.end(), points.begin(), points.end());
    grow_present_channels();
    return first;
}

void PointCloud::clear() noexcept {
    positions_.clear();
    intensity_.clear();
    classification_.clear();
    return_number_.clear();
    rgb_.clear();
}

void PointCloud::reserve(std::size_t n) { positions_.reserve(n); }

std::expected<void, PointCloudError> PointCloud::set_intensity(PointId id,
                                                               std::uint16_t v) noexcept {
    if (id >= positions_.size()) {
        return std::unexpected(PointCloudError{PointCloudErrc::PointIdOutOfRange});
    }
    if (intensity_.empty()) {
        intensity_.assign(positions_.size(), 0);
    }
    intensity_[id] = v;
    return {};
}

std::expected<void, PointCloudError> PointCloud::set_classification(PointId id,
                                                                    std::uint8_t v) noexcept {
    if (id >= positions_.size()) {
        return std::unexpected(PointCloudError{PointCloudErrc::PointIdOutOfRange});
    }
    if (classification_.empty()) {
        classification_.assign(positions_.size(), 0);
    }
    classification_[id] = v;
    return {};
}

std::expected<void, PointCloudError> PointCloud::set_return_number(PointId id,
                                                                   std::uint8_t v) noexcept {
    if (id >= positions_.size()) {
        return std::unexpected(PointCloudError{PointCloudErrc::PointIdOutOfRange});
    }
    if (return_number_.empty()) {
        return_number_.assign(positions_.size(), 0);
    }
    return_number_[id] = v;
    return {};
}

std::expected<void, PointCloudError> PointCloud::set_rgb(PointId id, std::uint16_t r,
                                                         std::uint16_t g,
                                                         std::uint16_t b) noexcept {
    if (id >= positions_.size()) {
        return std::unexpected(PointCloudError{PointCloudErrc::PointIdOutOfRange});
    }
    if (rgb_.empty()) {
        rgb_.assign(positions_.size(), std::array<std::uint16_t, 3>{0, 0, 0});
    }
    rgb_[id] = std::array<std::uint16_t, 3>{r, g, b};
    return {};
}

std::optional<std::uint16_t> PointCloud::intensity(PointId id) const noexcept {
    if (intensity_.empty() || id >= intensity_.size()) {
        return std::nullopt;
    }
    return intensity_[id];
}

std::optional<std::uint8_t> PointCloud::classification(PointId id) const noexcept {
    if (classification_.empty() || id >= classification_.size()) {
        return std::nullopt;
    }
    return classification_[id];
}

std::optional<std::uint8_t> PointCloud::return_number(PointId id) const noexcept {
    if (return_number_.empty() || id >= return_number_.size()) {
        return std::nullopt;
    }
    return return_number_[id];
}

std::optional<std::array<std::uint16_t, 3>> PointCloud::rgb(PointId id) const noexcept {
    if (rgb_.empty() || id >= rgb_.size()) {
        return std::nullopt;
    }
    return rgb_[id];
}

}  // namespace ingeneer::pointcloud
