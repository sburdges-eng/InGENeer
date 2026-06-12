# surface_core — TIN engine (Phase 6, ADR-0012 custom in-house engine).
# Built exclusively on geometry_core's exact predicates; oracle-validated against TOTaLi
# (ADR-0023) and cross-checked against the vendored CDT library in tests.
add_library(surface_core
    src/tin.cpp)

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

add_executable(test_tin_oracle tests/test_tin_oracle.cpp)
target_link_libraries(test_tin_oracle PRIVATE surface_core ingeneer_engine_flags)
add_test(NAME surface.tin_oracle
         COMMAND test_tin_oracle
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/oracle/totali-corpus-500pt-v1.txt)
