# geometry_core — robust geometric predicates + numeric policy (Phase 5).
# Ported from auracad geom (branch agentic/predicates-adaptive-3d-2026-04-24) per ADR-0024;
# Shewchuk robust predicates (public domain, https://www.cs.cmu.edu/~quake/robust.html).
add_library(geometry_core
    src/predicates.cpp)

target_include_directories(geometry_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(geometry_core PRIVATE ingeneer_engine_flags)

# FP contraction MUST be off for exact-predicate arithmetic (plan H-22, C-4.2):
# Apple clang defaults to -ffp-contract=on, and FMA contraction breaks the
# two_product/two_diff error terms AND the Layer-A filter determinants (observed:
# incircle returned nonzero for three identical points). predicates.cpp also
# carries `#pragma STDC FP_CONTRACT OFF` as defense in depth for ad-hoc builds.
target_compile_options(geometry_core PRIVATE -ffp-contract=off)

# --- tests (CTest; dependency-free harness) -------------------------------------------
add_executable(test_geom_numeric_policy tests/test_numeric_policy.cpp)
target_link_libraries(test_geom_numeric_policy PRIVATE geometry_core ingeneer_engine_flags)
add_test(NAME geom.numeric_policy COMMAND test_geom_numeric_policy)

add_executable(test_geom_predicates tests/test_predicates.cpp)
target_link_libraries(test_geom_predicates PRIVATE geometry_core ingeneer_engine_flags)
add_test(NAME geom.predicates COMMAND test_geom_predicates)

# Deterministic fixed-seed fuzz (C-4.6): 1e6 generator iterations by default (< 60 s).
# For the full Phase 5 soak, run the binary with GEOM_FUZZ_TOTAL_ITERATIONS=10000000.
add_executable(test_geom_predicates_fuzz tests/test_predicates_fuzz.cpp)
target_link_libraries(test_geom_predicates_fuzz PRIVATE geometry_core ingeneer_engine_flags)
add_test(NAME geom.predicates_fuzz COMMAND test_geom_predicates_fuzz)

# Phase 5.3: vendored third_party integration smoke (CDT MPL-2.0, nanoflann BSD).
# NOTE: not linked against ingeneer_engine_flags — vendored headers are not held to the
# engine's -Wconversion/-Wshadow bar (mirrored cleanly, never modified).
add_executable(test_geom_third_party_smoke tests/test_third_party_smoke.cpp)
target_link_libraries(test_geom_third_party_smoke PRIVATE
    geometry_core third_party_cdt third_party_nanoflann)
add_test(NAME geom.third_party_smoke COMMAND test_geom_third_party_smoke)
