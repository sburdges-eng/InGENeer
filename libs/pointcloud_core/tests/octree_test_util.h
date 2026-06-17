// SPDX-License-Identifier: Apache-2.0
#ifndef INGENEER_POINTCLOUD_OCTREE_TEST_UTIL_H
#define INGENEER_POINTCLOUD_OCTREE_TEST_UTIL_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ingeneer/pointcloud/octree_format.h"

namespace ingeneer::pointcloud::test {

struct Lcg {
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    std::uint32_t next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(s >> 33);
    }
    double unit() { return static_cast<double>(next()) / 4294967296.0; }
};

inline std::vector<RawPoint> uniform_cloud(std::size_t n, double span = 1000.0, double ox = 2'000'000.0,
                                           double oy = 500'000.0, double oz = 100.0,
                                           std::uint64_t seed = 1) {
    Lcg r{seed};
    std::vector<RawPoint> pts;
    pts.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        RawPoint p{};
        p.x = ox + r.unit() * span;
        p.y = oy + r.unit() * span;
        p.z = oz + r.unit() * span;
        p.intensity = static_cast<std::uint16_t>(r.next() & 0xFFFF);
        p.classification = static_cast<std::uint8_t>(r.next() & 0x1F);
        pts.push_back(p);
    }
    return pts;
}

class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        path_ = std::filesystem::temp_directory_path() / ("ingeneer_octree_" + tag);
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace ingeneer::pointcloud::test

#endif  // INGENEER_POINTCLOUD_OCTREE_TEST_UTIL_H
