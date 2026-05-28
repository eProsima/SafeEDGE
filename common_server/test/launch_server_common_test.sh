#!/usr/bin/env bash
# Build and run server_common unit tests on Linux/Ubuntu.
#
# Usage:
#   bash launch_server_common_test.sh
#
# Prerequisites:
#   - cmake >= 3.16
#   - libcurl development headers: sudo apt install libcurl4-openssl-dev
#   - Internet access at configure time (GTest fetched if not installed)
#
# Environment variables:
#   CMAKE_BUILD_TYPE   Release|Debug (default: Debug)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_COMMON_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

: "${CMAKE_BUILD_TYPE:=Debug}"

BUILD_DIR="${SERVER_COMMON_DIR}/build/linux-${CMAKE_BUILD_TYPE}"
CACHE_FILE="${BUILD_DIR}/CMakeCache.txt"

if [[ -f "${CACHE_FILE}" ]]; then
    CACHED_SOURCE_DIR="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${CACHE_FILE}")"
    if [[ -n "${CACHED_SOURCE_DIR}" && "${CACHED_SOURCE_DIR}" != "${SERVER_COMMON_DIR}" ]]; then
        echo "Removing stale CMake cache from copied build directory..."
        rm -rf "${BUILD_DIR}"
    fi
fi

echo "Configuring server_common tests..."
cmake \
    -S "${SERVER_COMMON_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"

echo "Building..."
cmake --build "${BUILD_DIR}" --parallel

echo ""
echo "Running test_server_common..."
echo "---------------------------------------------"

TEST_RC=0
"${BUILD_DIR}/test_server_common" || TEST_RC=$?

echo "---------------------------------------------"

if [[ "${TEST_RC}" -eq 0 ]]; then
    echo "PASSED"
else
    echo "FAILED (exit code ${TEST_RC})"
    exit "${TEST_RC}"
fi
