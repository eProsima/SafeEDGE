#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

: "${CMAKE_BUILD_TYPE:=Release}"
: "${CMAKE_GENERATOR:=Unix Makefiles}"
: "${NATIVE_COMMON_CXXFLAGS:=}"
: "${SAFE_DDS_IDL_GENERATOR:-}"
: "${SAFE_DDS_IDL_GENERATOR_ARGS:-}"
: "${SAFEDDS_DIR:=}"
: "${COMMON_SERVER_BUILD_FOLDER:=${WORKSPACE_ROOT}/common_server/build/server-common-native-${CMAKE_BUILD_TYPE}}"
: "${COMMON_SERVER_INSTALL_FOLDER:=${WORKSPACE_ROOT}/common_server/install/server-common-native-${CMAKE_BUILD_TYPE}}"
: "${SERVER_BUILD_FOLDER:=${WORKSPACE_ROOT}/safe_dds/build/server-native-${CMAKE_BUILD_TYPE}}"
: "${SERVER_INSTALL_FOLDER:=${WORKSPACE_ROOT}/safe_dds/install/server-native-${CMAKE_BUILD_TYPE}}"
: "${EDGE_BUILD_FOLDER:=${WORKSPACE_ROOT}/safe_dds/build/edge-native-${CMAKE_BUILD_TYPE}}"
: "${EDGE_INSTALL_FOLDER:=${WORKSPACE_ROOT}/safe_dds/install/edge-native-${CMAKE_BUILD_TYPE}}"
: "${SAFETY_BUILD_FOLDER:=${WORKSPACE_ROOT}/safe_dds/build/safety-native-${CMAKE_BUILD_TYPE}}"
: "${SAFETY_INSTALL_FOLDER:=${WORKSPACE_ROOT}/safe_dds/install/safety-native-${CMAKE_BUILD_TYPE}}"
: "${NON_SAFETY_BUILD_FOLDER:=${WORKSPACE_ROOT}/safe_dds/build/non-safety-native-${CMAKE_BUILD_TYPE}}"
: "${NON_SAFETY_INSTALL_FOLDER:=${WORKSPACE_ROOT}/safe_dds/install/non-safety-native-${CMAKE_BUILD_TYPE}}"

COMMON_IDL_SOURCE_DIR="${WORKSPACE_ROOT}/idl"
SAFE_DDS_IDL_DIR="${WORKSPACE_ROOT}/safe_dds/idl"

REGEN_IDL=0
EXTRA_BUILD_ARGS=()

usage() {
    cat <<EOF
Usage: bash build_native.sh [-i|--idl] [-- <extra build args>]

Builds the same CMake targets as build_qnx.sh, but natively for Ubuntu/Linux.

Options:
  -i, --idl       regenerate safe_dds/idl from the shared idl/*.idl sources
  -h, --help      show this help message

Environment variables:
  SAFEDDS_DIR                 path to the native Safe DDS CMake package
  SAFE_DDS_IDL_GENERATOR      command used to regenerate Safe DDS IDL artifacts
  SAFE_DDS_IDL_GENERATOR_ARGS extra args appended to the IDL generator command
  CMAKE_BUILD_TYPE            CMake configuration type (default: Release)

The script always configures and builds:
  - common_server
  - safe_dds/server
  - safe_dds/edge
  - safe_dds/safety
  - safe_dds/non_safety
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
            usage >&2
            exit 1
            ;;
    esac
done

find_idl_generator() {
    if [[ -n "${SAFE_DDS_IDL_GENERATOR:-}" ]]; then
        local generator_bin="${SAFE_DDS_IDL_GENERATOR%% *}"
        if [[ -x "${generator_bin}" ]] || command -v "${generator_bin}" >/dev/null 2>&1; then
            echo "${SAFE_DDS_IDL_GENERATOR}"
            return 0
        fi
    fi

    if [[ -n "${SAFE_DDS_PATH:-}" ]]; then
        local safedds_source_generator="${SAFE_DDS_PATH}/code-gen/scripts/safeddsgen"
        if [[ -x "${safedds_source_generator}" ]]; then
            echo "${safedds_source_generator}"
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
    echo "  generator: ${generator} -D . ${SAFE_DDS_IDL_GENERATOR_ARGS:-} *.idl"

    pushd "${SAFE_DDS_IDL_DIR}" >/dev/null
    if ! bash -lc "${generator} -D . ${SAFE_DDS_IDL_GENERATOR_ARGS:-} *.idl"; then
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
        -DCMAKE_INSTALL_PREFIX="${install_dir}"
    )

    if [[ -n "${CMAKE_CXX_FLAGS:-${NATIVE_COMMON_CXXFLAGS}}" ]]; then
        cmake_args+=("-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS:-${NATIVE_COMMON_CXXFLAGS}}")
    fi
    if [[ -n "${SAFEDDS_DIR:-}" ]]; then
        cmake_args+=("-Dsafedds_DIR=${SAFEDDS_DIR}")
    fi
    cmake_args+=("${extra_cmake_args[@]}")

    echo "Configuring ${name}"
    echo "  source : ${src_dir}"
    echo "  build  : ${build_dir}"
    echo "  install: ${install_dir}"
    cmake "${cmake_args[@]}"
    cmake --build "${build_dir}" --target install "${EXTRA_BUILD_ARGS[@]}"
    echo -e "Finished building ${name}\n"
}

if [[ ! -d "${COMMON_IDL_SOURCE_DIR}" ]]; then
    echo "Shared IDL source directory not found at '${COMMON_IDL_SOURCE_DIR}'" >&2
    exit 1
fi

if [[ ! -d "${SAFE_DDS_IDL_DIR}" ]]; then
    echo "Safe DDS generated IDL directory not found at '${SAFE_DDS_IDL_DIR}'" >&2
    exit 1
fi

if (( REGEN_IDL )); then
    regenerate_idl
fi

if [[ -f "${COMMON_IDL_SOURCE_DIR}/internal.idl" && ! -f "${SAFE_DDS_IDL_DIR}/internal.hpp" ]]; then
    echo "Generated Safe DDS IDL header missing: ${SAFE_DDS_IDL_DIR}/internal.hpp" >&2
    echo "Regenerate IDL with: bash scripts/build_native.sh --idl" >&2
    echo "If the generator is not in PATH, set SAFE_DDS_IDL_GENERATOR first." >&2
    exit 1
fi

mkdir -p "${COMMON_SERVER_BUILD_FOLDER}" "${COMMON_SERVER_INSTALL_FOLDER}" \
         "${SERVER_BUILD_FOLDER}" "${SERVER_INSTALL_FOLDER}" \
         "${EDGE_BUILD_FOLDER}" "${EDGE_INSTALL_FOLDER}" \
         "${SAFETY_BUILD_FOLDER}" "${SAFETY_INSTALL_FOLDER}" \
         "${NON_SAFETY_BUILD_FOLDER}" "${NON_SAFETY_INSTALL_FOLDER}"

configure_and_build "${WORKSPACE_ROOT}/common_server" "${COMMON_SERVER_BUILD_FOLDER}" "${COMMON_SERVER_INSTALL_FOLDER}" "common_server"
configure_and_build "${WORKSPACE_ROOT}/safe_dds/server" "${SERVER_BUILD_FOLDER}" "${SERVER_INSTALL_FOLDER}" "safe_dds/server"
configure_and_build "${WORKSPACE_ROOT}/safe_dds/edge" "${EDGE_BUILD_FOLDER}" "${EDGE_INSTALL_FOLDER}" "safe_dds/edge"
configure_and_build "${WORKSPACE_ROOT}/safe_dds/safety" "${SAFETY_BUILD_FOLDER}" "${SAFETY_INSTALL_FOLDER}" "safe_dds/safety"
configure_and_build "${WORKSPACE_ROOT}/safe_dds/non_safety" "${NON_SAFETY_BUILD_FOLDER}" "${NON_SAFETY_INSTALL_FOLDER}" "safe_dds/non_safety"

echo "All SafeEDGE native targets built successfully."
