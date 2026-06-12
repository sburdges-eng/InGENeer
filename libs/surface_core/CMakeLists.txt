# surface_core — TIN engine (Phase 6, ADR-0012 custom in-house engine).
# Built exclusively on geometry_core's exact predicates; oracle-validated against TOTaLi
# (ADR-0023) and cross-checked against the vendored CDT library in tests.
add_library(surface_core
    src/tin.cpp
    src/tin_breakline.cpp)

target_include_directories(surface_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(surface_core PUBLIC geometry_core PRIVATE ingeneer_engine_flags)

# KERNEL_DEBUG_ASSERT tier (plan §3.4, Phase 6.5): the full constrained-Delaunay audit
# runs automatically after every mutating Tin operation in Debug configurations — i.e.
# the dev, asan-ubsan and tsan presets — and is compiled out of hardened/release.
target_compile_definitions(surface_core PRIVATE $<$<CONFIG:Debug>:INGENEER_KERNEL_DEBUG_AUDIT=1>)

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

# --- Phase 6.5 fuzzing -----------------------------------------------------------------
# Same scheme as libs/audit_core: INGENEER_FUZZ=ON builds the real libFuzzer binary
# (needs a libFuzzer runtime — Apple clang ships none; use Homebrew LLVM with direct
# compilation, or Linux CI). Default OFF: build the deterministic standalone driver
# (200k op-stream inputs, full invariant audit after every op) as a CTest under whatever
# sanitizer the preset selects.
option(INGENEER_FUZZ "Build libFuzzer binaries (needs a libFuzzer runtime)" OFF)

if(INGENEER_FUZZ)
    add_executable(fuzz_tin fuzz/fuzz_tin.cpp)
    target_link_libraries(fuzz_tin PRIVATE surface_core)
    target_compile_options(fuzz_tin PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_tin PRIVATE -fsanitize=fuzzer,address,undefined)
else()
    add_executable(fuzz_tin_standalone fuzz/fuzz_tin.cpp fuzz/standalone_main.cpp)
    target_link_libraries(fuzz_tin_standalone PRIVATE surface_core ingeneer_engine_flags)
    # 200k inputs is the Phase 6.5 gate (dev / asan-ubsan). TSan is ~10x slower and adds
    # no value to this single-threaded target beyond build coverage: run a deterministic
    # prefix of the same stream there.
    set(_ingeneer_tin_fuzz_iters 200000)
    if(INGENEER_TSAN)
        set(_ingeneer_tin_fuzz_iters 20000)
    endif()
    add_test(NAME surface.fuzz_tin_standalone
             COMMAND fuzz_tin_standalone ${_ingeneer_tin_fuzz_iters})
endif()

# --- Phase 6.5 perf baselines (bench/BENCHMARKS.md) --------------------------------------
# Not a CTest: run manually from a hardened build (never a sanitizer build):
#   cmake --build --preset hardened --target bench_tin
#   build/hardened/libs/surface_core/bench_tin
add_executable(bench_tin bench/bench_tin.cpp)
target_link_libraries(bench_tin PRIVATE surface_core ingeneer_engine_flags)
