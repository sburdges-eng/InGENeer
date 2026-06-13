# coordinate_core — PROJ-backed CRS identity + transforms (Phase 7).
#
# Generic CRS layer with ZERO US hardcoding (ADR-0010): a Crs is built from an arbitrary
# authority code / WKT2 / PROJ string via system PROJ. Carries the TOTaLi "CRS change
# forces recompute" invariant (R-4.4) via CrsStamped<T> (header-only, src/crs.cpp wires
# the PROJ transforms).
#
# PROJ is a SYSTEM dependency (Homebrew: /opt/homebrew/opt/proj), MIT/X11-licensed —
# compatible with the Apache-2.0 open core (ADR-0021). It is NOT vendored into
# third_party/, so third_party governance + the license-allowlist scan do not apply.
# See README.md for the dependency note.
find_package(PROJ CONFIG REQUIRED)

add_library(coordinate_core
    src/crs.cpp)

target_include_directories(coordinate_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# PROJ::proj is PRIVATE: PROJ types never appear in our public headers (opaque void*
# handles only), so consumers of coordinate_core do not transitively see <proj.h>.
target_link_libraries(coordinate_core PUBLIC ingeneer_engine_flags PRIVATE PROJ::proj)

# --- tests (CTest; dependency-free harness) -------------------------------------------

# R-4.4 recompute invariant: pure CrsStamped value logic (the determinism-relevant half).
add_executable(test_crs_recompute tests/test_crs_recompute.cpp)
target_link_libraries(test_crs_recompute PRIVATE coordinate_core ingeneer_engine_flags)
add_test(NAME coord.crs_recompute COMMAND test_crs_recompute)

# PROJ-backed transforms: round-trip US + non-US (Europe) CRS, error + non-finite cases.
# PROJ is a normal C++ library and instruments fine under ASan/UBSan/TSan; run it under
# every preset. If a future PROJ build proves impractical to instrument, exclude this one
# test from the sanitizer lanes (the recompute test above is pure logic and always runs).
add_executable(test_crs_transform tests/test_crs_transform.cpp)
target_link_libraries(test_crs_transform PRIVATE coordinate_core ingeneer_engine_flags)
add_test(NAME coord.crs_transform COMMAND test_crs_transform)
