#!/bin/bash
# Offline source build using the local CMake + g++ toolchain; no downloads or package installation.
# Kept under its historical filename so existing local workflows continue to work.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${OUT:-build_gpp}
command -v cmake >/dev/null 2>&1 || { echo "ERROR: CMake is required for this build path." >&2; exit 2; }
command -v g++ >/dev/null 2>&1 || { echo "ERROR: g++ is required for this build path." >&2; exit 2; }
cmake -S . -B "$OUT" -DCMAKE_BUILD_TYPE=Release -DHUSH_WARNINGS_AS_ERRORS=ON -DBUILD_TESTING=ON
cmake --build "$OUT" --parallel "${BUILD_JOBS:-2}"
echo "built HUSH and test binaries in $OUT"
echo "run: ctest --test-dir $OUT --output-on-failure"
