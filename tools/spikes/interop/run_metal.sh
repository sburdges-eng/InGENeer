#!/usr/bin/env bash
# Phase 8 prerequisite spike build+run — Metal bytesNoCopy + realloc-under-render (R-11
# render path, H-27 lifetime contract). Headless: MTLDevice + runtime-compiled compute
# kernel, no window/app bundle. Requires clang++ (C++23), swiftc, and a Metal device.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

echo "[1/4] compile C++ page-aligned arena"
clang++ -std=c++23 -O2 -c cpp/metal_arena.cpp -o build/metal_arena.o

echo "[2/4] ASan gate: CPU-only arena lifetime exercise (Metal+ASan is unreliable, so the"
echo "      quarantine/realloc-under-read logic is sanitized here, GPU ordering below)"
clang++ -std=c++23 -g -O1 -fsanitize=address \
    cpp/metal_arena.cpp cpp/metal_arena_asan_main.cpp -o build/metal_arena_asan
./build/metal_arena_asan

echo "[3/4] build Swift/Metal consumer (C ABI import, link C++ object + libc++)"
swiftc -O \
    -import-objc-header cpp/metal_arena.h \
    -Xcc -I"$PWD/cpp" \
    build/metal_arena.o -lc++ \
    swift/metal_main.swift -o build/interop_metal_spike

echo "[4/4] run Metal test (headless)"
./build/interop_metal_spike
