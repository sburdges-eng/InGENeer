#!/usr/bin/env bash
# Phase 2.3 numeric-policy gate (CONSTRAINTS C-4.2; plan H-4/H-22).
#
# Fails if any engine translation unit is compiled with a flag that breaks IEEE-754
# determinism. Scans the CMake-exported compile_commands.json (the authoritative record of
# what each TU is actually compiled with) rather than source text.
#
# Usage: check_numeric_flags.sh [path/to/compile_commands.json ...]
#        (defaults to build/*/compile_commands.json)
set -euo pipefail

forbidden='(-ffast-math|-Ofast|-ffinite-math-only|-freciprocal-math|-fassociative-math|-funsafe-math-optimizations|-ffp-contract=fast)'

shopt -s nullglob
files=("$@")
if [ ${#files[@]} -eq 0 ]; then
    files=(build/*/compile_commands.json)
fi

if [ ${#files[@]} -eq 0 ]; then
    echo "check_numeric_flags: no compile_commands.json found (configure a CMake preset first)" >&2
    exit 0  # nothing to check is not a failure in a fresh checkout
fi

status=0
for f in "${files[@]}"; do
    if grep -nE "$forbidden" "$f" >/dev/null 2>&1; then
        echo "FAIL: forbidden numeric flag in $f (C-4.2):" >&2
        grep -noE "$forbidden" "$f" | sort -u >&2
        status=1
    else
        echo "ok: $f"
    fi
done
exit $status
