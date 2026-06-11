# audit_core — append-only hash-chained event store + Entity Authority System (Phase 3).
# Spec: docs/superpowers/specs/2026-06-11-audit-core-storage-schema-spec.md
find_package(SQLite3 REQUIRED)

add_library(audit_core
    src/sha256.cpp
    src/canonical.cpp
    src/jsonl.cpp
    src/store.cpp)

target_include_directories(audit_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(audit_core PUBLIC SQLite3::SQLite3 PRIVATE ingeneer_engine_flags)

# --- tests (CTest; dependency-free harness) -------------------------------------------
add_executable(test_sha256 tests/test_sha256.cpp)
target_link_libraries(test_sha256 PRIVATE audit_core ingeneer_engine_flags)
add_test(NAME audit.sha256 COMMAND test_sha256)

add_executable(test_canonical tests/test_canonical.cpp)
target_link_libraries(test_canonical PRIVATE audit_core ingeneer_engine_flags)
add_test(NAME audit.canonical COMMAND test_canonical)

add_executable(test_chain tests/test_chain.cpp)
target_link_libraries(test_chain PRIVATE audit_core ingeneer_engine_flags)
add_test(NAME audit.chain COMMAND test_chain)

add_executable(test_authority tests/test_authority.cpp)
target_link_libraries(test_authority PRIVATE audit_core ingeneer_engine_flags)
add_test(NAME audit.authority COMMAND test_authority)

add_executable(test_agent_chain tests/test_agent_chain.cpp)
target_link_libraries(test_agent_chain PRIVATE audit_core ingeneer_engine_flags)
add_test(NAME audit.agent_chain COMMAND test_agent_chain)

add_executable(test_jsonl tests/test_jsonl.cpp)
target_link_libraries(test_jsonl PRIVATE audit_core ingeneer_engine_flags)
add_test(NAME audit.jsonl COMMAND test_jsonl)

add_executable(test_property_authority tests/test_property_authority.cpp)
target_link_libraries(test_property_authority PRIVATE audit_core ingeneer_engine_flags)
add_test(NAME audit.property_authority COMMAND test_property_authority)

add_executable(test_concurrency tests/test_concurrency.cpp)
target_link_libraries(test_concurrency PRIVATE audit_core ingeneer_engine_flags)
add_test(NAME audit.concurrency COMMAND test_concurrency)

# --- §6 cross-language round trip vs the REAL Python AuditLogger ----------------------
add_executable(chain_roundtrip_tool tools/chain_roundtrip_tool.cpp)
target_link_libraries(chain_roundtrip_tool PRIVATE audit_core ingeneer_engine_flags)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME audit.roundtrip_python
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_roundtrip_python.py
                     $<TARGET_FILE:chain_roundtrip_tool>
                     ${CMAKE_SOURCE_DIR})
endif()

# --- Phase 3.6 fuzzing ----------------------------------------------------------------
# INGENEER_FUZZ=ON builds the real libFuzzer binary (needs a libFuzzer-capable toolchain,
# e.g. Homebrew LLVM or Linux CI). Default OFF: build the deterministic standalone driver
# that exercises the same target under whatever sanitizer the preset selects (CTest-runnable
# today; Apple clang ships no libFuzzer runtime).
option(INGENEER_FUZZ "Build the libFuzzer binary for audit_core (needs libFuzzer runtime)" OFF)

if(INGENEER_FUZZ)
    foreach(tgt IN ITEMS canonical jsonl)
        add_executable(fuzz_${tgt} fuzz/fuzz_${tgt}.cpp)
        target_link_libraries(fuzz_${tgt} PRIVATE audit_core)
        target_compile_options(fuzz_${tgt} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_${tgt} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
else()
    foreach(tgt IN ITEMS canonical jsonl)
        add_executable(fuzz_${tgt}_standalone fuzz/fuzz_${tgt}.cpp fuzz/standalone_main.cpp)
        target_link_libraries(fuzz_${tgt}_standalone PRIVATE audit_core ingeneer_engine_flags)
        add_test(NAME audit.fuzz_${tgt}_standalone COMMAND fuzz_${tgt}_standalone)
    endforeach()
endif()
