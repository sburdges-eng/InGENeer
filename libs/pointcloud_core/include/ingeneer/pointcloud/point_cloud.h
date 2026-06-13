// SPDX-License-Identifier: Apache-2.0
//
// ingeneer/pointcloud/point_cloud.h — in-memory point-cloud container (Phase 7).
//
// The in-CORE data model for point clouds: an SoA-of-points position store plus a lean,
// optional set of per-point attributes. This is the structure the in-core spatial index
// (point_index.h) builds over, and the structure the later out-of-core octree
// (ADR-0028) and the renderer LOD layer will consume. It is deliberately NOT the
// on-disk / out-of-core format, nor the LAS attribute zoo — full ingestion attribute
// modelling belongs to the PDAL ingestion task (next campaign).
//
// Layout choice — `std::vector<std::array<float, 3>>` (SoA-of-points / AoS-of-component):
//   * float precision is intentional: this is the renderer/LOD-facing spatial layer,
//     distinct from geometry_core's exact double predicates. Point clouds carry millions
//     of points where float halves memory and matches GPU vertex buffers (xyz packed) and
//     nanoflann's L2_Simple_Adaptor element type. Survey-grade exactness lives in
//     geometry_core/coordinate_core, not here.
//   * Contiguous `array<float,3>` packs xyz adjacently, so a single point's coordinates
//     are cache-local and the buffer can be uploaded to a GPU / mmap'd to the octree's
//     on-disk node payload verbatim, with no gather. Three parallel `vector<float>`
//     arrays would be true component-SoA but force a 3-way gather per point in both the
//     kd-tree adaptor (which reads one point at a time) and the renderer (which wants
//     interleaved xyz) — the wrong trade for this access pattern. We therefore use
//     SoA-of-points: an array of fixed-width point records.
//   * Index handles are plain point indices (PointId = std::uint32_t), matching the
//     index-handle discipline of geometry_core/surface_core (no pointers in the public
//     surface; trivially serializable). 2^32 points (~48 GB of xyz) is the in-core ceiling
//     by design — beyond it is the out-of-core octree's job, not this container's.
//
// Determinism (CONSTRAINTS C-4.6 / plan H-22): the container performs no floating-point
// arithmetic and no ordering beyond append order, so its observable state is a pure
// function of the insertion sequence. No RNG, no wall-clock.
#ifndef INGENEER_POINTCLOUD_POINT_CLOUD_H
#define INGENEER_POINTCLOUD_POINT_CLOUD_H

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace ingeneer::pointcloud {

// Index handle: a point's position in the cloud (append order). No pointers cross the
// public surface (geometry_core/surface_core idiom).
using PointId = std::uint32_t;

inline constexpr PointId kNoPoint = 0xFFFFFFFFu;

enum class PointCloudErrc : std::uint8_t {
    NonFiniteCoordinate,      // NaN or +-inf coordinate in an inserted point
    CapacityExceeded,         // would exceed the 2^32 - 1 in-core point ceiling (PointId space)
    AttributeLengthMismatch,  // bulk attribute span length != point_count()
    PointIdOutOfRange,        // a queried/assigned PointId is >= point_count()
};

struct PointCloudError {
    PointCloudErrc code;
};

// A single point's position. Plain xyz floats; see the layout rationale in the file
// header. This is the element of the SoA-of-points position store.
struct Point3f {
    float x;
    float y;
    float z;
};

// Optional per-point attribute channels. Lean by design (CONSTRAINTS C-5.4: no
// speculative modelling) — the LAS attribute zoo is ingestion's concern, not this
// container's. Each channel is either absent (the cloud carries no such data) or a dense
// array exactly point_count() long. Channels are allocated lazily on first assignment so
// a positions-only cloud pays no per-point attribute memory.
//
// Channels provided: intensity (uint16, LAS-native range), classification (uint8, ASPRS
// class code), return number (uint8), and RGB (three uint16 per point). These cover the
// renderer/LOD colouring + filtering needs; richer channels are added by ingestion when a
// concrete consumer requires them.
class PointCloud {
public:
    PointCloud() = default;

    // --- positions (the SoA-of-points store) ------------------------------------------

    // Append one point. Returns its PointId. Rejects non-finite coordinates
    // (NonFiniteCoordinate) and a full PointId space (CapacityExceeded). Any already
    // allocated attribute channel is extended by one default-valued slot so channel
    // length stays == point_count().
    std::expected<PointId, PointCloudError> add_point(float x, float y, float z) noexcept;

    std::expected<PointId, PointCloudError> add_point(const Point3f& p) noexcept {
        return add_point(p.x, p.y, p.z);
    }

    // Bulk append. Returns the PointId of the FIRST appended point (or kNoPoint when
    // `points` is empty). On any non-finite coordinate or capacity overflow the cloud is
    // rolled back to its exact pre-call state (atomic bulk insert).
    std::expected<PointId, PointCloudError> add_points(std::span<const Point3f> points);

    std::size_t point_count() const noexcept { return positions_.size(); }
    bool empty() const noexcept { return positions_.empty(); }

    // The contiguous SoA-of-points position buffer. Stable, cache-local xyz records —
    // this is exactly what the spatial index's nanoflann adaptor and (later) the renderer
    // / octree read. Indexed by PointId.
    std::span<const Point3f> positions() const noexcept { return positions_; }

    const Point3f& position(PointId id) const noexcept { return positions_[id]; }

    // Drop all points and attribute channels (keeps allocated capacity).
    void clear() noexcept;
    void reserve(std::size_t n);

    // --- optional attribute channels --------------------------------------------------
    // A channel is present() once any of its values has been set (or it was bulk-assigned).
    // Reading an unset channel returns std::nullopt; reading an out-of-range PointId from a
    // present channel returns std::nullopt as well (callers use point_count() to bound).

    bool has_intensity() const noexcept { return !intensity_.empty(); }
    bool has_classification() const noexcept { return !classification_.empty(); }
    bool has_return_number() const noexcept { return !return_number_.empty(); }
    bool has_rgb() const noexcept { return !rgb_.empty(); }

    std::expected<void, PointCloudError> set_intensity(PointId id, std::uint16_t v) noexcept;
    std::expected<void, PointCloudError> set_classification(PointId id, std::uint8_t v) noexcept;
    std::expected<void, PointCloudError> set_return_number(PointId id, std::uint8_t v) noexcept;
    std::expected<void, PointCloudError> set_rgb(PointId id, std::uint16_t r, std::uint16_t g,
                                                 std::uint16_t b) noexcept;

    std::optional<std::uint16_t> intensity(PointId id) const noexcept;
    std::optional<std::uint8_t> classification(PointId id) const noexcept;
    std::optional<std::uint8_t> return_number(PointId id) const noexcept;
    std::optional<std::array<std::uint16_t, 3>> rgb(PointId id) const noexcept;

private:
    // SoA-of-points: contiguous fixed-width xyz records (see file-header rationale).
    std::vector<Point3f> positions_;

    // Lazily allocated attribute channels; when non-empty each is exactly
    // positions_.size() long (the add_point/add_points paths grow every present channel).
    std::vector<std::uint16_t> intensity_;
    std::vector<std::uint8_t> classification_;
    std::vector<std::uint8_t> return_number_;
    std::vector<std::array<std::uint16_t, 3>> rgb_;

    void grow_present_channels();
};

}  // namespace ingeneer::pointcloud

#endif  // INGENEER_POINTCLOUD_POINT_CLOUD_H
