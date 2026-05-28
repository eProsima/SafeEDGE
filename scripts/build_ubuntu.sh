#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

: "${FASTDDS_BASE_IMAGE:=eprosima/vulcanexus:kilted-base}"
: "${CMAKE_BUILD_TYPE:=Release}"

BUILD_TESTS=0

usage() {
    cat <<EOF
Usage: bash build_ubuntu.sh [--tests] [-h|--help]

Builds the FastDDS-based server and edge Docker images using the
Dockerfiles under fast_dds/docker/. The build context is the repository root.

Options:
  --tests    Also build the test images (server.test and edge.test)
  -h, --help Show this help message

Environment variables:
  FASTDDS_BASE_IMAGE   Base image for FastDDS (default: eprosima/vulcanexus:kilted-base)
  CMAKE_BUILD_TYPE     CMake build type (default: Release)

Resulting images:
  safe-edge-server:fast
  safe-edge-edge:fast
  safe-edge-server:fast-test   (only with --tests)
  safe-edge-edge:fast-test     (only with --tests)

To run the server and edge after building:
  bash fast_dds/docker/run.sh
EOF
}

for arg in "$@"; do
    case "${arg}" in
        --tests) BUILD_TESTS=1 ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: ${arg}" >&2
            usage >&2
            exit 1
            ;;
    esac
done

cd "${WORKSPACE_ROOT}"

echo "Building safe-edge-server:fast ..."
docker build \
    --build-arg FASTDDS_BASE_IMAGE="${FASTDDS_BASE_IMAGE}" \
    --build-arg CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -f fast_dds/docker/server.Dockerfile \
    -t safe-edge-server:fast \
    .

echo "Building safe-edge-edge:fast ..."
docker build \
    --build-arg FASTDDS_BASE_IMAGE="${FASTDDS_BASE_IMAGE}" \
    --build-arg CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -f fast_dds/docker/edge.Dockerfile \
    -t safe-edge-edge:fast \
    .

if (( BUILD_TESTS )); then
    echo "Building safe-edge-server:fast-test ..."
    docker build \
        --build-arg FASTDDS_BASE_IMAGE="${FASTDDS_BASE_IMAGE}" \
        --build-arg CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
        -f fast_dds/docker/server.test.Dockerfile \
        -t safe-edge-server:fast-test \
        .

    echo "Building safe-edge-edge:fast-test ..."
    docker build \
        --build-arg FASTDDS_BASE_IMAGE="${FASTDDS_BASE_IMAGE}" \
        --build-arg CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
        -f fast_dds/docker/edge.test.Dockerfile \
        -t safe-edge-edge:fast-test \
        .
fi

echo "Done. Images built:"
echo "  safe-edge-server:fast"
echo "  safe-edge-edge:fast"
if (( BUILD_TESTS )); then
    echo "  safe-edge-server:fast-test"
    echo "  safe-edge-edge:fast-test"
fi
