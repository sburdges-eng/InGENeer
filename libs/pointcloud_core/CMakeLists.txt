# pointcloud_core — in-memory point-cloud model + in-core nanoflann spatial index (Phase 7).
#
# Scope (per the agentic-work-memory-hardening plan §4.2(5)): the SoA-of-points container
# and a nanoflann L2_Simple_Adaptor kd-tree over it (bulk rebuild, k-NN + radius). The
# out-of-core octree / LOD / on-disk format (ADR-0028) and PDAL / LAS-LAZ ingestion are
# SEPARATE later tasks and are not built here.
#
# Float-precision spatial layer (renderer/LOD-facing): deliberately NOT routed through
# geometry_core's exact double predicates — that is the surface/geometry kernel's job.
add_library(pointcloud_core
    src/point_cloud.cpp
    src/point_index.cpp)

target_include_directories(pointcloud_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# nanoflann (BSD) is consumed ONLY inside src/point_index.cpp behind a PIMPL, so it is a
# PRIVATE link dependency — no consumer of pointcloud_core drags <nanoflann.hpp> in.
target_link_libraries(pointcloud_core PRIVATE ingeneer_engine_flags third_party_nanoflann)

# The vendored nanoflann header is mirrored verbatim and is NOT held to the engine's
# -Wconversion/-Wshadow bar (same policy as geometry_core's third_party smoke test, which
# simply omits ingeneer_engine_flags). Here we keep our own code under the full warning
# bar but mark nanoflann's include path SYSTEM for this target, so its internal
# signedness-conversion warnings are suppressed without touching third_party/ (read-only).
target_include_directories(pointcloud_core SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/nanoflann/include)

# --- tests (CTest; dependency-free harness, same pattern as geometry/surface) ----------
add_executable(test_point_cloud tests/test_point_cloud.cpp)
target_link_libraries(test_point_cloud PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.point_cloud COMMAND test_point_cloud)

add_executable(test_point_index tests/test_point_index.cpp)
target_link_libraries(test_point_index PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.point_index COMMAND test_point_index)
