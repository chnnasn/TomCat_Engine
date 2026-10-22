#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${TC_BUILD_DIR:-$root/build/linux}"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE="${TC_BUILD_TYPE:-Release}" "$@"
cmake --build "$build" --parallel "${TC_BUILD_JOBS:-2}" -- -k 0
ctest --test-dir "$build" --output-on-failure
