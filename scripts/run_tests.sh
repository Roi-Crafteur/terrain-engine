#!/usr/bin/env bash
# Configures, builds and runs all unit tests. Usage: scripts/run_tests.sh [build-dir]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-${root}/build}"

cmake -S "${root}" -B "${build}"
cmake --build "${build}" --parallel
ctest --test-dir "${build}" --output-on-failure
