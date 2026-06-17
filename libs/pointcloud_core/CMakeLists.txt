# pointcloud_core — in-memory point-cloud model + in-core nanoflann spatial index (Phase 7).
#
# Out-of-core octree / LOD / on-disk format (ADR-0028) — see docs/superpowers/plans/2026-06-16-out-of-core-octree.md.
add_library(pointcloud_core
    src/point_cloud.cpp
    src/point_index.cpp
    src/morton.cpp
    src/sha256.cpp
    src/octree_checksum.cpp
    src/octree_metadata.cpp
    src/octree_build_incore.cpp)

target_compile_definitions(pointcloud_core PRIVATE $<$<CONFIG:Debug>:INGENEER_KERNEL_DEBUG_AUDIT=1>)

target_include_directories(pointcloud_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(pointcloud_core PRIVATE ingeneer_engine_flags third_party_nanoflann third_party_xxhash)

target_include_directories(pointcloud_core SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/nanoflann/include
    ${CMAKE_SOURCE_DIR}/third_party/xxhash/include)

# --- tests (CTest; dependency-free harness, same pattern as geometry/surface) ----------
add_executable(test_point_cloud tests/test_point_cloud.cpp)
target_link_libraries(test_point_cloud PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.point_cloud COMMAND test_point_cloud)

add_executable(test_point_index tests/test_point_index.cpp)
target_link_libraries(test_point_index PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.point_index COMMAND test_point_index)

add_executable(test_octree_format tests/test_octree_format.cpp)
target_link_libraries(test_octree_format PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.octree_format COMMAND test_octree_format)

add_executable(test_morton tests/test_morton.cpp)
target_link_libraries(test_morton PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.morton COMMAND test_morton)

add_executable(test_pointcloud_sha256 tests/test_sha256.cpp)
target_link_libraries(test_pointcloud_sha256 PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.sha256 COMMAND test_pointcloud_sha256)

add_executable(test_octree_checksum tests/test_octree_checksum.cpp)
target_link_libraries(test_octree_checksum PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.octree_checksum COMMAND test_octree_checksum)

add_executable(test_octree_metadata tests/test_octree_metadata.cpp)
target_link_libraries(test_octree_metadata PRIVATE pointcloud_core ingeneer_engine_flags)
add_test(NAME pointcloud.octree_metadata COMMAND test_octree_metadata)

add_executable(test_octree_build_incore tests/test_octree_build_incore.cpp)
target_link_libraries(test_octree_build_incore PRIVATE pointcloud_core ingeneer_engine_flags)
target_include_directories(test_octree_build_incore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME pointcloud.octree_build_incore COMMAND test_octree_build_incore)
