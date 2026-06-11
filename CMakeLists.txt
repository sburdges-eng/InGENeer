# InGENeer C++ engine superbuild (Phase 2.2).
#
# Top-level entry for the C++23 engines under libs/. UI-free, Apple-framework-free
# (CONSTRAINTS C-4.1). Sanitizer/hardening configuration is selected via CMakePresets.json
# (presets: dev, hardened, asan-ubsan, tsan). Numeric policy (C-4.2): -ffast-math and
# equivalents are forbidden in engine translation units; the CI grep gate enforces this.
cmake_minimum_required(VERSION 3.24)

project(ingeneer_engines LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Export compile_commands.json for clang-tidy and the fast-math/ffp-contract grep gate.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Forbidden flag guard (C-4.2, H-4/H-22) -------------------------------------------
# A defense-in-depth check: fail configuration if a caller injected -ffast-math etc.
set(_ingeneer_forbidden_flags "-ffast-math" "-Ofast" "-ffinite-math-only" "-freciprocal-math")
foreach(_f IN LISTS _ingeneer_forbidden_flags)
    if(CMAKE_CXX_FLAGS MATCHES "${_f}")
        message(FATAL_ERROR "Forbidden numeric flag '${_f}' in CMAKE_CXX_FLAGS (C-4.2).")
    endif()
endforeach()

# --- Sanitizer / hardening options (driven by presets) --------------------------------
option(INGENEER_ASAN_UBSAN "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(INGENEER_TSAN "Enable ThreadSanitizer" OFF)
option(INGENEER_HARDENED "Enable OpenSSF hardening flag set" OFF)

add_library(ingeneer_engine_flags INTERFACE)

target_compile_options(ingeneer_engine_flags INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(INGENEER_ASAN_UBSAN)
    target_compile_options(ingeneer_engine_flags INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(ingeneer_engine_flags INTERFACE -fsanitize=address,undefined)
endif()

if(INGENEER_TSAN)
    target_compile_options(ingeneer_engine_flags INTERFACE
        -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(ingeneer_engine_flags INTERFACE -fsanitize=thread)
endif()

if(INGENEER_HARDENED)
    # OpenSSF Compiler Hardening Guide consensus subset (plan §3.5).
    target_compile_options(ingeneer_engine_flags INTERFACE
        -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS
        -fstack-protector-strong
        -Wformat -Wformat-security -Werror=format-security)
    # Apple clang on arm64 accepts but ignores -fstack-clash-protection (warns); only add
    # it where the compiler actually implements it (Linux gcc/clang).
    include(CheckCXXCompilerFlag)
    set(CMAKE_REQUIRED_FLAGS "-Werror=unused-command-line-argument")
    check_cxx_compiler_flag("-fstack-clash-protection" INGENEER_HAS_STACK_CLASH)
    unset(CMAKE_REQUIRED_FLAGS)
    if(INGENEER_HAS_STACK_CLASH)
        target_compile_options(ingeneer_engine_flags INTERFACE -fstack-clash-protection)
    endif()
endif()

enable_testing()
add_subdirectory(libs)
