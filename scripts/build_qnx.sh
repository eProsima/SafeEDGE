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
: "${QNX_TOOLCHAIN_FILE:=${WORKSPACE_ROOT}/qnx/toolchains/qnx8.cmake}"
: "${COMMON_SERVER_BUILD_FOLDER:=${WORKSPACE_ROOT}/common_server/build/server-common-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}}"
: "${COMMON_SERVER_INSTALL_FOLDER:=${WORKSPACE_ROOT}/common_server/install/server-common-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}}"
: "${SERVER_BUILD_FOLDER:=${WORKSPACE_ROOT}/safe_dds/build/server-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}}"
: "${SERVER_INSTALL_FOLDER:=${WORKSPACE_ROOT}/safe_dds/install/server-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}}"
: "${EDGE_BUILD_FOLDER:=${WORKSPACE_ROOT}/safe_dds/build/edge-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}}"
: "${EDGE_INSTALL_FOLDER:=${WORKSPACE_ROOT}/safe_dds/install/edge-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}}"
: "${SAFE_DDS_IDL_GENERATOR:-}"
: "${SAFE_DDS_IDL_GENERATOR_ARGS:-}"
: "${SAFEDDS_DIR:=${WORKSPACE_ROOT}/qnx/install/safedds-qnx8-${QNX_ARCH}/safedds}"

COMMON_IDL_SOURCE_DIR="${WORKSPACE_ROOT}/idl"
SAFE_DDS_IDL_DIR="${WORKSPACE_ROOT}/safe_dds/idl"

REGEN_IDL=0
EXTRA_BUILD_ARGS=()

usage() {
    cat <<EOF
Usage: bash build_qnx.sh [-i|--idl] [-- <extra build args>]

Options:
  -i, --idl       regenerate safe_dds/idl from the shared idl/*.idl sources
  -h, --help      show this help message

Environment variables:
  SAFE_DDS_IDL_GENERATOR   command used to regenerate Safe DDS IDL artifacts
  SAFE_DDS_IDL_GENERATOR_ARGS extra args appended to the IDL generator command
  QNX_TOOLCHAIN_FILE       path to the QNX toolchain file
  SAFEDDS_DIR              path to cross-compiled Safe DDS CMake package for QNX
  QNX_ARCH                 x86_64 or aarch64le (default: x86_64)
  CMAKE_BUILD_TYPE         CMake configuration type (default: Release)

The script always configures and builds:
  - common_server
  - safe_dds/server
  - safe_dds/edge

If there are no source changes, CMake will skip recompilation internally.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -i|--idl)
            REGEN_IDL=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            EXTRA_BUILD_ARGS=("$@")
            break
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

find_idl_generator() {
    if [[ -n "${SAFE_DDS_IDL_GENERATOR:-}" ]]; then
        local generator_bin="${SAFE_DDS_IDL_GENERATOR%% *}"
        if command -v "${generator_bin}" >/dev/null 2>&1; then
            echo "${SAFE_DDS_IDL_GENERATOR}"
            return 0
        fi
    fi

    local candidates=(safeddsgen safedds-idl-gen safedds-gen eprosima_safeddsgen)
    for prog in "${candidates[@]}"; do
        if command -v "${prog}" >/dev/null 2>&1; then
            echo "${prog}"
            return 0
        fi
    done

    return 1
}

regenerate_idl() {
    local generator
    generator="$(find_idl_generator)" || {
        echo "No IDL generator found. Set SAFE_DDS_IDL_GENERATOR to a valid command." >&2
        exit 1
    }

    if [[ ! -d "${COMMON_IDL_SOURCE_DIR}" ]]; then
        echo "Shared IDL source directory not found at '${COMMON_IDL_SOURCE_DIR}'" >&2
        exit 1
    fi

    mkdir -p "${SAFE_DDS_IDL_DIR}"

    local cleanup_links=()
    local idl_file
    for idl_file in "${COMMON_IDL_SOURCE_DIR}"/*.idl; do
        if [[ ! -e "${idl_file}" ]]; then
            echo "No .idl files found in '${COMMON_IDL_SOURCE_DIR}'" >&2
            exit 1
        fi

        local link_target="${SAFE_DDS_IDL_DIR}/$(basename "${idl_file}")"
        ln -sfn "${idl_file}" "${link_target}"
        cleanup_links+=("${link_target}")
    done

    echo "Regenerating Safe DDS IDL artifacts"
    echo "  source   : ${COMMON_IDL_SOURCE_DIR}"
    echo "  generated: ${SAFE_DDS_IDL_DIR}"
    echo "  generator: ${generator} ${SAFE_DDS_IDL_GENERATOR_ARGS:-}"

    pushd "${SAFE_DDS_IDL_DIR}" >/dev/null
    if ! bash -lc "${generator} ${SAFE_DDS_IDL_GENERATOR_ARGS:-}"; then
        popd >/dev/null
        rm -f "${cleanup_links[@]}"
        exit 1
    fi
    popd >/dev/null

    rm -f "${cleanup_links[@]}"
}

configure_and_build() {
    local src_dir="$1"
    local build_dir="$2"
    local install_dir="$3"
    local name="$4"
    shift 4
    local extra_cmake_args=("$@")
    local cache_file="${build_dir}/CMakeCache.txt"

    if [[ -f "${cache_file}" ]]; then
        local cached_source
        cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${cache_file}")"
        if [[ -n "${cached_source}" && "${cached_source}" != "${src_dir}" ]]; then
            echo "Removing stale CMake cache for ${name}"
            echo "  cached source: ${cached_source}"
            rm -rf "${build_dir}"
            mkdir -p "${build_dir}"
        fi
    fi

    local cmake_args=(
        -S "${src_dir}"
        -B "${build_dir}"
        -G "${CMAKE_GENERATOR}"
        -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        -DCMAKE_TOOLCHAIN_FILE="${QNX_TOOLCHAIN_FILE}"
        -DQNX_ARCH="${QNX_ARCH}"
        -DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS:-${QNX_COMMON_CXXFLAGS}}"
        -DCMAKE_INSTALL_PREFIX="${install_dir}"
    )

    cmake_args+=("-Dsafedds_DIR=${SAFEDDS_DIR}")
    cmake_args+=("${extra_cmake_args[@]}")

    echo "Configuring ${name}"
    echo "  source : ${src_dir}"
    echo "  build  : ${build_dir}"
    echo "  install: ${install_dir}"
    cmake "${cmake_args[@]}"
    cmake --build "${build_dir}" --target install "${EXTRA_BUILD_ARGS[@]}"
    echo -e "Finished building ${name}\n"
}

case "${QNX_ARCH}" in
    x86_64|aarch64le) ;;
    *)
        echo "Unsupported QNX_ARCH='${QNX_ARCH}'. Use x86_64 or aarch64le." >&2
        exit 1
        ;;
esac

if [[ ! -d "${QNX_HOST}" || ! -d "${QNX_TARGET}" ]]; then
    echo "QNX SDK paths not found. QNX_HOST='${QNX_HOST}', QNX_TARGET='${QNX_TARGET}'" >&2
    exit 1
fi

if [[ ! -f "${QNX_TOOLCHAIN_FILE}" ]]; then
    echo "QNX toolchain file not found. QNX_TOOLCHAIN_FILE='${QNX_TOOLCHAIN_FILE}'" >&2
    exit 1
fi

if [[ ! -d "${SAFEDDS_DIR}" ]]; then
    echo "Safe DDS QNX install not found at '${SAFEDDS_DIR}'" >&2
    echo "Build it first with: bash build_safedds_qnx.sh -- -j2" >&2
    exit 1
fi

if [[ ! -d "${COMMON_IDL_SOURCE_DIR}" ]]; then
    echo "Shared IDL source directory not found at '${COMMON_IDL_SOURCE_DIR}'" >&2
    exit 1
fi

if [[ ! -d "${SAFE_DDS_IDL_DIR}" ]]; then
    echo "Safe DDS generated IDL directory not found at '${SAFE_DDS_IDL_DIR}'" >&2
    exit 1
fi

export QNX_HOST
export QNX_TARGET
export PATH="${QNX_HOST}/usr/bin:${PATH}"

if (( REGEN_IDL )); then
    regenerate_idl
fi

mkdir -p "${COMMON_SERVER_BUILD_FOLDER}" "${COMMON_SERVER_INSTALL_FOLDER}" \
         "${SERVER_BUILD_FOLDER}" "${SERVER_INSTALL_FOLDER}" \
         "${EDGE_BUILD_FOLDER}" "${EDGE_INSTALL_FOLDER}"

configure_and_build "${WORKSPACE_ROOT}/common_server" "${COMMON_SERVER_BUILD_FOLDER}" "${COMMON_SERVER_INSTALL_FOLDER}" "common_server"
configure_and_build "${WORKSPACE_ROOT}/safe_dds/server" "${SERVER_BUILD_FOLDER}" "${SERVER_INSTALL_FOLDER}" "safe_dds/server"
configure_and_build "${WORKSPACE_ROOT}/safe_dds/edge" "${EDGE_BUILD_FOLDER}" "${EDGE_INSTALL_FOLDER}" "safe_dds/edge"

echo "All SafeEDGE QNX targets built successfully."
