#!/usr/bin/env bash
# Phase 4 interop spike build+run. Compiles the C++ arena, links it into a Swift executable
# that consumes arena memory zero-copy, and runs the measurement. Requires clang++ + swiftc.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

echo "[1/3] compile C++ arena"
clang++ -std=c++23 -O2 -c cpp/buffer_source.cpp -o build/buffer_source.o

echo "[2/3] build Swift consumer (C ABI import, link C++ object + libc++)"
swiftc -O \
    -import-objc-header cpp/buffer_source.h \
    -Xcc -I"$PWD/cpp" \
    build/buffer_source.o -lc++ \
    swift/main.swift -o build/interop_spike

echo "[3/3] run"
./build/interop_spike

echo ""
echo "=== Phase 8 prereq: Metal bytesNoCopy + realloc-under-render (R-11 render path) ==="
./run_metal.sh
