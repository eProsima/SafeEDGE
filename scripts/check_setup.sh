#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

QNX_USER="${USER:-$(id -un)}"
: "${QNX_SDP_ROOT:=/home/${QNX_USER}/qnx800}"
: "${QNX_HOST:=${QNX_SDP_ROOT}/host/linux/x86_64}"
: "${QNX_TARGET:=${QNX_SDP_ROOT}/target/qnx}"
: "${QNX_ARCH:=x86_64}"

LINUX_ONLY=0

usage() {
    cat <<EOF
Usage: bash check_setup.sh [--linux-only]

Options:
  --linux-only   check only Linux/common_server prerequisites
  -h, --help     show this help message
EOF
}

for arg in "$@"; do
    case "${arg}" in
        --linux-only) LINUX_ONLY=1 ;;
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

require_cmd() {
    local cmd="$1"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "Missing command: ${cmd}" >&2
        echo "For Ubuntu/Debian host packages, run: bash install_host_deps.sh" >&2
        return 1
    fi
}

require_path() {
    local path="$1"
    if [[ ! -e "${path}" ]]; then
        echo "Missing path: ${path}" >&2
        return 1
    fi
}

echo "Checking common prerequisites..."
require_cmd bash
require_cmd cmake
require_cmd tee
require_cmd curl-config
require_path "${WORKSPACE_ROOT}/README.md"
require_path "${WORKSPACE_ROOT}/common_server/CMakeLists.txt"
require_path "${WORKSPACE_ROOT}/safe_dds/server/CMakeLists.txt"
require_path "${WORKSPACE_ROOT}/safe_dds/edge/CMakeLists.txt"
require_path "${WORKSPACE_ROOT}/safe_dds/safety/CMakeLists.txt"
require_path "${WORKSPACE_ROOT}/safe_dds/non_safety/CMakeLists.txt"
require_path "${WORKSPACE_ROOT}/scripts/build_qnx.sh"
require_path "${WORKSPACE_ROOT}/scripts/build_safedds_qnx.sh"
require_path "${WORKSPACE_ROOT}/scripts/launch_tpi_2_1_test.sh"
require_path "${WORKSPACE_ROOT}/scripts/launch_tpi_2_2_test.sh"
require_path "${WORKSPACE_ROOT}/scripts/launch_tpi_2_3_test.sh"
require_path "${WORKSPACE_ROOT}/scripts/launch_tpi_2_5_test.sh"
require_path "${WORKSPACE_ROOT}/scripts/aux_vehicle_nodes.sh"

if (( LINUX_ONLY )); then
    echo "Linux-only setup check completed."
    exit 0
fi

echo "Checking QNX SDK..."
if [[ ! -e "${QNX_SDP_ROOT}/qnxsdp-env.sh" ]]; then
    echo "Missing QNX SDK environment file: ${QNX_SDP_ROOT}/qnxsdp-env.sh" >&2
    echo "Install QNX SDP 8 and/or set QNX_SDP_ROOT to its installation path." >&2
    exit 1
fi
require_path "${QNX_HOST}"
require_path "${QNX_TARGET}"

echo "Checking QNX host tools..."
require_cmd qemu-system-x86_64
require_cmd sshpass
require_cmd brctl
require_cmd file

echo "Checking repo-local QNX assets..."
require_path "${WORKSPACE_ROOT}/qnx/toolchains/qnx8.cmake"
require_path "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}"
require_path "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/mkqnximage-wrapper.sh"
require_path "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/local/options"
require_path "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/local/valgrind.files"
require_path "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/local/snippets"
require_path "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/local/snippets/data_files.custom"
require_path "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/local/snippets/ifs_files.custom"
require_path "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/local/snippets/profile.custom"

if [[ ! -d "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/local/misc_files" ]]; then
    echo "QNX local misc_files directory not found yet. Test launchers create it when needed."
fi

if [[ -e "${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}/local/ssh-ident" ]]; then
    echo "Found local QNX ssh-ident file. It is local/generated and intentionally ignored by git."
fi

echo "Checking Safe-DDS source configuration..."
if [[ -z "${SAFE_DDS_PATH:-}" ]]; then
    echo "SAFE_DDS_PATH is not defined." >&2
    echo "Set it to the Safe-DDS source tree before building SafeDDS for QNX." >&2
    echo "Example: export SAFE_DDS_PATH=/path/to/Safe-DDS-source-release" >&2
    exit 1
fi
require_path "${SAFE_DDS_PATH}"

if [[ ! -d "${WORKSPACE_ROOT}/qnx/install/safedds-qnx8-${QNX_ARCH}/safedds" ]]; then
    echo "Safe DDS QNX install not found yet."
    echo "Build it with: bash scripts/build_safedds_qnx.sh -- -j2"
fi

echo "Setup check completed."
echo "QNX_SDP_ROOT=${QNX_SDP_ROOT}"
echo "QNX_HOST=${QNX_HOST}"
echo "QNX_TARGET=${QNX_TARGET}"
echo "SAFE_DDS_PATH=${SAFE_DDS_PATH}"
echo "QNX target=${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}"
