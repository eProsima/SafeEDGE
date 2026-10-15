#!/usr/bin/env bash
# Run the Safe DDS server integration test on a QNX VM.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/test_output_common.sh"
LOG_DIR="${SCRIPT_DIR}/logs"
LOG_FILE="${LOG_DIR}/launch_tpi_2_1.log"

QNX_USER="${USER:-$(id -un)}"
: "${QNX_SDP_ROOT:=/home/${QNX_USER}/qnx800}"
: "${QNX_ARCH:=x86_64}"
: "${CMAKE_BUILD_TYPE:=Release}"
: "${TARGET_DIR:=${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}}"
: "${SERVER_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/server-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${SERVER_NATIVE_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/server-native-${CMAKE_BUILD_TYPE}/bin}"

_SSH_PASS="root"
_SSH_USER="root"
_SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=30 -o LogLevel=ERROR"

mkdir -p "${LOG_DIR}"
exec > >(tee "${LOG_FILE}") 2>&1

OPT_NO_REBUILD=0
OPT_STOP=0
OPT_LINUX=0

usage() {
    cat <<EOF
Usage: bash scripts/launch_tpi_2_1_test.sh [--no-rebuild] [--linux|--ubuntu] [--stop]

Options:
  --no-rebuild   Skip image rebuild and reuse existing VM artifacts
  --linux        Run the native Linux test binaries instead of a QNX VM
  --ubuntu       Alias for --linux
  --stop         Stop a running VM and exit

Environment variables:
  QNX_SDP_ROOT      path to the QNX SDP root
  QNX_ARCH          x86_64 or aarch64le
  CMAKE_BUILD_TYPE  Release or Debug
  TARGET_DIR        mkqnximage target directory
  SERVER_BIN_DIR    directory containing safe_edge_server and test_server_integration
  SERVER_NATIVE_BIN_DIR  directory containing native safe_edge_server and test_server_integration
EOF
}

for arg in "$@"; do
    case "${arg}" in
        --no-rebuild) OPT_NO_REBUILD=1 ;;
        --linux|--ubuntu) OPT_LINUX=1 ;;
        --stop) OPT_STOP=1 ;;
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

if [[ "${OPT_LINUX}" -eq 1 ]]; then
    SERVER_BIN_DIR="${SERVER_NATIVE_BIN_DIR}"
fi

_validate_linux_binary() {
    local bin="$1"
    local description

    if ! description="$(file "${bin}")"; then
        echo "Failed to inspect binary: ${bin}" >&2
        return 1
    fi

    if ! grep -Fq "GNU/Linux" <<<"${description}"; then
        echo "Binary does not look like a Linux executable:" >&2
        echo "  ${description}" >&2
        echo "Rebuild with: bash scripts/build_native.sh" >&2
        return 1
    fi
}

if [[ "${OPT_LINUX}" -eq 1 ]]; then
    if [[ "${OPT_STOP}" -eq 1 ]]; then
        echo "No QNX VM is used in --linux mode. Nothing to stop."
        exit 0
    fi

    test_banner_open "TPI 2.1 - Server Integration"
    test_banner_context "linux" "${LOG_FILE}"

    pkill -f "safe_edge_server" 2>/dev/null || true

    for bin in \
        "${SERVER_BIN_DIR}/safe_edge_server" \
        "${SERVER_BIN_DIR}/test_server_integration"; do
        if [[ ! -f "${bin}" ]]; then
            echo "Binary not found: ${bin}" >&2
            echo "Build with: bash scripts/build_native.sh" >&2
            exit 1
        fi
        _validate_linux_binary "${bin}"
    done

    test_section "Running test_server_integration on Linux"

    TEST_RC=0
    SAFE_EDGE_SERVER_BIN="${SERVER_BIN_DIR}/safe_edge_server" \
        "${SERVER_BIN_DIR}/test_server_integration" || TEST_RC=$?

    test_footer "TPI 2.1 - Server Integration" "${TEST_RC}" "${LOG_FILE}"
    exit "${TEST_RC}"
fi

if [[ ! -f "${QNX_SDP_ROOT}/qnxsdp-env.sh" ]]; then
    echo "QNX SDK not found at QNX_SDP_ROOT='${QNX_SDP_ROOT}'" >&2
    exit 1
fi

if [[ ! -d "${TARGET_DIR}" ]]; then
    echo "QNX target directory not found: ${TARGET_DIR}" >&2
    exit 1
fi

if ! command -v sshpass >/dev/null 2>&1; then
    echo "sshpass not found. Install with: sudo apt install sshpass" >&2
    exit 1
fi

# shellcheck source=/dev/null
source "${QNX_SDP_ROOT}/qnxsdp-env.sh" >/dev/null 2>&1

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "qemu-system-x86_64 not found. Install with: sudo apt install qemu-system-x86" >&2
    exit 1
fi

if ! command -v brctl >/dev/null 2>&1; then
    echo "brctl not found. Install with: sudo apt install bridge-utils" >&2
    exit 1
fi

_get_ip_address() {
    local max_tries=20
    local ip
    local i

    for ((i = 0; i < max_tries; i++)); do
        set +e
        ip="$(mkqnximage --getip 2>/dev/null)"
        set -e
        if [[ -n "${ip}" ]]; then
            echo "${ip}"
            return 0
        fi
        sleep 2
    done

    echo "Timed out waiting for VM IP address." >&2
    return 1
}

_ssh_run() {
    local ip="$1"
    local cmd="$2"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" ssh ${_SSH_OPTS} "${_SSH_USER}@${ip}" "${cmd}"
}

_wait_for_ssh() {
    local ip="$1"
    local max_tries=45
    local i

    for ((i = 1; i <= max_tries; i++)); do
        if _ssh_run "${ip}" "true" >/dev/null 2>&1; then
            return 0
        fi
        if (( i == 1 || i % 5 == 0 )); then
            echo "  waiting for SSH (${i}/${max_tries})..."
        fi
        sleep 2
    done

    echo "Timed out waiting for SSH on ${ip}." >&2
    return 1
}

_validate_qnx_binary() {
    local bin="$1"
    local description

    if ! description="$(file "${bin}")"; then
        echo "Failed to inspect binary: ${bin}" >&2
        return 1
    fi

    if grep -Fq "GNU/Linux" <<<"${description}"; then
        echo "Binary appears to be a Linux executable, not a QNX executable:" >&2
        echo "  ${description}" >&2
        echo "Rebuild with: bash scripts/build_qnx.sh" >&2
        return 1
    fi
}

_refresh_test_system_files_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/system_files.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<EOF
# local/snippets/system_files.custom
# Generated by scripts/launch_tpi_2_1_test.sh
[perms=555] bin/safe_edge_server=${SERVER_BIN_DIR}/safe_edge_server
[perms=555] bin/test_server_integration=${SERVER_BIN_DIR}/test_server_integration
EOF
}

_refresh_test_ifs_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/ifs_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/ifs_start.custom
# Generated by scripts/launch_tpi_2_1_test.sh
EOF
}

_refresh_test_post_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/post_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/post_start.custom
# Generated by scripts/launch_tpi_2_1_test.sh
route add -net 224.0.0.0/4 vtnet0
EOF
}

_reset_generated_target_output() {
    rm -rf "${TARGET_DIR}/output"
}

_prepare_local_target_dirs() {
    mkdir -p "${TARGET_DIR}/local/misc_files" "${TARGET_DIR}/local/snippets"
}

_prepare_local_target_dirs
cd "${TARGET_DIR}"

test_banner_open "TPI 2.1 - Server Integration"
test_banner_context "qnx" "${LOG_FILE}"

if [[ "${OPT_STOP}" -eq 1 ]]; then
    echo "Stopping QNX VM..."
    mkqnximage --stop 2>/dev/null || true
    kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true
    echo "VM stopped."
    exit 0
fi

kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true

if [[ "${OPT_NO_REBUILD}" -eq 0 ]]; then
    for bin in \
        "${SERVER_BIN_DIR}/safe_edge_server" \
        "${SERVER_BIN_DIR}/test_server_integration"; do
        if [[ ! -f "${bin}" ]]; then
            echo "Binary not found: ${bin}" >&2
            echo "Build with: bash scripts/build_qnx.sh" >&2
            exit 1
        fi
        _validate_qnx_binary "${bin}"
    done

    _refresh_test_ifs_start_snippet
    _refresh_test_post_start_snippet
    _refresh_test_system_files_snippet
    _reset_generated_target_output
fi

if [[ "${OPT_NO_REBUILD}" -eq 0 ]]; then
    echo "Building QNX image and starting QEMU..."
    mkqnximage --noprompt --run=-h --clean >/dev/null 2>&1
else
    echo "Starting QNX QEMU (skipping rebuild)..."
    mkqnximage --noprompt --run=-h >/dev/null 2>&1
fi

echo "Waiting for VM IP..."
VM_IP="$(_get_ip_address)"
echo "VM is up: ${VM_IP}"
echo "Waiting for SSH..."
_wait_for_ssh "${VM_IP}"
echo "VM is reachable."

test_section "Running test_server_integration on QNX"

TEST_RC=0
_ssh_run "${VM_IP}" \
    "SAFE_EDGE_SERVER_BIN=/system/bin/safe_edge_server /system/bin/test_server_integration" \
    || TEST_RC=$?

echo "Stopping QNX VM..."
mkqnximage --stop 2>/dev/null || true

test_footer "TPI 2.1 - Server Integration" "${TEST_RC}" "${LOG_FILE}"
exit "${TEST_RC}"
