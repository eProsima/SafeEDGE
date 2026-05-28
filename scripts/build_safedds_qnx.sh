#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

QNX_USER="${USER:-$(id -un)}"
: "${QNX_SDP_ROOT:=/home/${QNX_USER}/qnx800}"
: "${QNX_HOST:=${QNX_SDP_ROOT}/host/linux/x86_64}"
: "${QNX_TARGET:=${QNX_SDP_ROOT}/target/qnx}"
: "${QNX_ARCH:=x86_64}"
: "${CMAKE_BUILD_TYPE:=Release}"
: "${CMAKE_GENERATOR:=Unix Makefiles}"
: "${QNX_COMMON_CXXFLAGS:=-D_QNX_SOURCE}"
: "${QNX_BUILD_SAFEDDS_FOLDER:=${WORKSPACE_ROOT}/qnx/build/safedds-qnx8-${QNX_ARCH}}"
: "${QNX_INSTALL_SAFEDDS_FOLDER:=${WORKSPACE_ROOT}/qnx/install/safedds-qnx8-${QNX_ARCH}}"

if [[ -z "${SAFE_DDS_PATH:-}" ]]; then
    echo "SAFE_DDS_PATH is not defined." >&2
    echo "Set it to the Safe-DDS source tree, for example:" >&2
    echo "  export SAFE_DDS_PATH=/path/to/Safe-DDS-source-release" >&2
    exit 1
fi

if [[ ! -d "${SAFE_DDS_PATH}" ]]; then
    echo "Safe-DDS source path not found: ${SAFE_DDS_PATH}" >&2
    exit 1
fi

if [[ ! -d "${QNX_HOST}" || ! -d "${QNX_TARGET}" ]]; then
    echo "QNX SDK paths not found. QNX_HOST='${QNX_HOST}', QNX_TARGET='${QNX_TARGET}'" >&2
    exit 1
fi

case "${QNX_ARCH}" in
    x86_64|aarch64le) ;;
    *)
        echo "Unsupported QNX_ARCH='${QNX_ARCH}'. Use x86_64 or aarch64le." >&2
        exit 1
        ;;
esac

export QNX_HOST
export QNX_TARGET
export PATH="${QNX_HOST}/usr/bin:${PATH}"

mkdir -p "${QNX_BUILD_SAFEDDS_FOLDER}" "${QNX_INSTALL_SAFEDDS_FOLDER}"

echo "Configuring Safe-DDS for QNX 8"
echo "  source : ${SAFE_DDS_PATH}"
echo "  build  : ${QNX_BUILD_SAFEDDS_FOLDER}"
echo "  install: ${QNX_INSTALL_SAFEDDS_FOLDER}"
echo "  arch   : ${QNX_ARCH}"

cmake \
    -S "${SAFE_DDS_PATH}" \
    -B "${QNX_BUILD_SAFEDDS_FOLDER}" \
    -G "${CMAKE_GENERATOR}" \
    -DCMAKE_TOOLCHAIN_FILE="${WORKSPACE_ROOT}/qnx/toolchains/qnx8.cmake" \
    -DQNX_ARCH="${QNX_ARCH}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS:-${QNX_COMMON_CXXFLAGS}}" \
    -DCMAKE_INSTALL_PREFIX="${QNX_INSTALL_SAFEDDS_FOLDER}" \
    -DSAFEDDS_LOG_LEVEL="WARNING"

cmake --build "${QNX_BUILD_SAFEDDS_FOLDER}" --target install "$@"
