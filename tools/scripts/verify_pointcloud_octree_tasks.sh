#!/usr/bin/env bash
# Point-cloud octree Task 6/7 verification lane (Phase 7).
# Static determinism/fuzz-safety checks + optional CTest presets.
#
# Usage (from repo root or any subdir):
#   ./tools/scripts/verify_pointcloud_octree_tasks.sh
#   ./tools/scripts/verify_pointcloud_octree_tasks.sh --asan
#   ./tools/scripts/verify_pointcloud_octree_tasks.sh --task6-only
#
# Intended branch: feat/phase7-pointcloud-octree
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

PC_ROOT="$REPO_ROOT/libs/pointcloud_core"
META_CPP="$PC_ROOT/src/octree_metadata.cpp"
META_H="$PC_ROOT/include/ingeneer/pointcloud/octree_metadata.h"
BUILD_CPP="$PC_ROOT/src/octree_build_incore.cpp"

RUN_ASAN=0
TASK6_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --asan) RUN_ASAN=1 ;;
    --task6-only) TASK6_ONLY=1 ;;
    --help|-h)
      sed -n '1,12p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown flag: $arg" >&2
      exit 2
      ;;
  esac
done

declare -a NAMES=()
declare -a RESULTS=()
OVERALL=0

record() {
  NAMES+=("$1")
  if [ "$2" -eq 0 ]; then
    RESULTS+=("PASS")
  else
    RESULTS+=("FAIL")
    OVERALL=1
  fi
}

run_step() {
  local name="$1"
  shift
  echo ">>> $name"
  if "$@"; then
    record "$name" 0
  else
    record "$name" "$?"
  fi
}

branch="$(git branch --show-current 2>/dev/null || true)"
if [ "$branch" != "feat/phase7-pointcloud-octree" ]; then
  echo "note: on branch '$branch' (expected feat/phase7-pointcloud-octree for octree commits)"
fi

# --- static: Task 6 metadata writer/parser -----------------------------------
static_metadata() {
  [ -f "$META_CPP" ] || { echo "skip: $META_CPP not found"; return 0; }

  if ! rg -q 'std::from_chars' "$META_CPP"; then
    echo "FAIL: octree_metadata.cpp must parse numbers with std::from_chars (no locale/printf)"
    return 1
  fi

  if rg -q '\b(printf|fprintf|sprintf|snprintf|std::stod|std::stof|strtod)\b' "$META_CPP"; then
    echo "FAIL: locale-dependent or printf-style parsing/formatting in octree_metadata.cpp"
    return 1
  fi

  # Writer key order is part of the audit anchor (metadata.json SHA-256).
  local expected_keys=(
    format hierarchy_sha256 max_level max_node_points min_node_points
    node_count origin point_count points_sha256 root_edge sampling_grid scale
    schema_has_gps_time schema_has_rgb version
  )
  local -a found_keys=()
  while IFS= read -r line; do
    if [[ "$line" =~ o\ +=\ \"\ \ \"\\\"\([a-z_]+\)\\\" ]]; then
      found_keys+=("${BASH_REMATCH[1]}")
    fi
  done < <(rg 'o \+= "  \\"' "$META_CPP" || true)

  if [ "${#found_keys[@]}" -eq 0 ]; then
    echo "FAIL: could not extract metadata writer keys from $META_CPP"
    return 1
  fi

  local i
  for i in "${!expected_keys[@]}"; do
    if [ "${found_keys[$i]:-}" != "${expected_keys[$i]}" ]; then
      echo "FAIL: metadata key order mismatch at index $i"
      echo "  expected: ${expected_keys[$i]}"
      echo "  found:    ${found_keys[$i]:-<missing>}"
      return 1
    fi
  done

  if ! rg -q 'unknown key' "$META_CPP"; then
    echo "WARN: no explicit unknown-key skip path found (forward-compat)"
  fi

  echo "OK: metadata static checks (${#found_keys[@]} keys, from_chars, no printf parse)"
}

# --- static: build-path determinism (Tasks 2–7) ------------------------------
static_build_determinism() {
  local files=()
  shopt -s nullglob
  for f in "$PC_ROOT"/src/octree*.cpp "$PC_ROOT"/src/morton.cpp; do
    [ -f "$f" ] && files+=("$f")
  done
  shopt -u nullglob

  if [ "${#files[@]}" -eq 0 ]; then
    echo "skip: no octree build sources yet"
    return 0
  fi

  if rg -n '\b(srand|random_device|std::random|rand\(|drand48|arc4random)\b' "${files[@]}"; then
    echo "FAIL: RNG usage in octree build path (plan §2.3 / C-4.6)"
    return 1
  fi

  if rg -n '\bthrow\b' "$PC_ROOT/include/ingeneer/pointcloud"/octree*.h 2>/dev/null; then
    echo "FAIL: exceptions in public octree headers (use std::expected)"
    return 1
  fi

  echo "OK: build-path static checks (${#files[@]} source file(s))"
}

# --- CTest targets -----------------------------------------------------------
run_ctest_preset() {
  local preset="$1"
  local label="$2"
  shift 2

  if ! cmake --list-presets configure 2>/dev/null | rg -q "$preset"; then
    echo "skip: cmake preset '$preset' not configured"
    return 0
  fi

  cmake --preset "$preset" >/dev/null
  cmake --build --preset "$preset" --target test_octree_format test_morton test_pointcloud_sha256 test_octree_checksum test_octree_metadata
  ctest --preset "$preset" -R 'pointcloud\.octree' --output-on-failure
}

run_step "static:metadata-task6" static_metadata
run_step "static:build-determinism" static_build_determinism

if [ -f "$META_CPP" ]; then
  run_step "ctest:dev:octree_metadata" bash -c \
    "cmake --preset dev >/dev/null && cmake --build --preset dev --target test_octree_metadata && ctest --preset dev -R pointcloud.octree_metadata --output-on-failure"
else
  echo ">>> ctest:dev:octree_metadata (skip — source not on disk)"
  record "ctest:dev:octree_metadata" 0
fi

if [ "$TASK6_ONLY" -eq 0 ] && [ -f "$BUILD_CPP" ]; then
  run_step "ctest:dev:octree_build_incore" bash -c \
    "cmake --preset dev >/dev/null && cmake --build --preset dev --target test_octree_build_incore && ctest --preset dev -R pointcloud.octree_build_incore --output-on-failure"
elif [ "$TASK6_ONLY" -eq 0 ]; then
  echo ">>> ctest:dev:octree_build_incore (skip — source not on disk)"
  record "ctest:dev:octree_build_incore" 0
fi

if [ "$RUN_ASAN" -eq 1 ] && [ -f "$META_CPP" ]; then
  run_step "ctest:asan-ubsan:octree" bash -c \
    "cmake --preset asan-ubsan >/dev/null && cmake --build --preset asan-ubsan --target test_octree_metadata && ctest --preset asan-ubsan -R pointcloud.octree --output-on-failure"
fi

echo ""
echo "=== pointcloud octree verification ==="
for i in "${!NAMES[@]}"; do
  printf "  %-32s %s\n" "${NAMES[$i]}" "${RESULTS[$i]}"
done
echo "======================================"

exit "$OVERALL"
