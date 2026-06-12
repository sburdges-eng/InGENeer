# surface_core — TIN engine (Phase 6, ADR-0012 custom in-house engine).
# Built exclusively on geometry_core's exact predicates; oracle-validated against TOTaLi
# (ADR-0023) and cross-checked against the vendored CDT library in tests.
add_library(surface_core
    src/tin.cpp
    src/tin_breakline.cpp)

target_include_directories(surface_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(surface_core PUBLIC geometry_core PRIVATE ingeneer_engine_flags)

# --- tests (CTest; dependency-free harness) -------------------------------------------
add_executable(test_tin_basic tests/test_tin_basic.cpp)
target_link_libraries(test_tin_basic PRIVATE surface_core ingeneer_engine_flags)
add_test(NAME surface.tin_basic COMMAND test_tin_basic)

# Audit test links the vendored CDT for independent cross-validation (test-only; the
# engine itself never depends on third_party). Not held to the engine warning bar.
add_executable(test_tin_audit tests/test_tin_audit.cpp)
target_link_libraries(test_tin_audit PRIVATE surface_core third_party_cdt)
add_test(NAME surface.tin_audit COMMAND test_tin_audit)

add_executable(test_tin_breakline tests/test_tin_breakline.cpp)
target_link_libraries(test_tin_breakline PRIVATE surface_core ingeneer_engine_flags)
add_test(NAME surface.tin_breakline COMMAND test_tin_breakline)

add_executable(test_tin_oracle tests/test_tin_oracle.cpp)
target_link_libraries(test_tin_oracle PRIVATE surface_core ingeneer_engine_flags)
add_test(NAME surface.tin_oracle
         COMMAND test_tin_oracle
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/oracle/totali-corpus-500pt-v1.txt)

# --- contours + volumes (Phase 6.3) ----------------------------------------------------
# Survey-deliverable quantities over the TIN: contour extraction (symbolic perturbation
# for vertex-on-level) and cut/fill volumes (TIN-prism with exact mixed-triangle split,
# prismoidal). Separate target consuming surface_core strictly via its public API.
add_library(surface_quantities
    src/contour.cpp
    src/volume.cpp)
target_include_directories(surface_quantities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(surface_quantities PUBLIC surface_core PRIVATE ingeneer_engine_flags)

add_executable(test_contour tests/test_contour.cpp)
target_link_libraries(test_contour PRIVATE surface_quantities ingeneer_engine_flags)
add_test(NAME surface.contour
         COMMAND test_contour
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/oracle/totali-corpus-500pt-v1.txt
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/oracle/totali-corpus-500pt-cv-v1.txt)

add_executable(test_volume tests/test_volume.cpp)
target_link_libraries(test_volume PRIVATE surface_quantities ingeneer_engine_flags)
add_test(NAME surface.volume
         COMMAND test_volume
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/oracle/totali-corpus-500pt-v1.txt
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/oracle/totali-corpus-500pt-cv-v1.txt)
