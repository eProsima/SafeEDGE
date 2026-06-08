#!/usr/bin/env bash
# Run TPI 2.5 smoke test for SafeDDS safety and non-safety QNX nodes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${SCRIPT_DIR}/logs"
LOG_FILE="${LOG_DIR}/launch_tpi_2_5.log"

QNX_USER="${USER:-$(id -un)}"
: "${QNX_SDP_ROOT:=/home/${QNX_USER}/qnx800}"
: "${QNX_ARCH:=x86_64}"
: "${CMAKE_BUILD_TYPE:=Release}"
: "${TARGET_DIR:=${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}}"
: "${SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${NON_SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/non-safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${SMOKE_TEST_SECONDS:=10}"

_SSH_PASS="root"
_SSH_USER="root"
_SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=30 -o LogLevel=ERROR"

mkdir -p "${LOG_DIR}"
exec > >(tee "${LOG_FILE}") 2>&1

OPT_NO_REBUILD=0
OPT_STOP=0

VEHICLE_NODE_BINS=(
    safe_edge_safety_io_adapters
    safe_edge_policy_engine
    safe_edge_vehicle_mock
    safe_edge_cloud_gateway
    safe_edge_ota_service
    safe_edge_infotainment
)

usage() {
    cat <<EOF
Usage: bash scripts/launch_tpi_2_5_test.sh [--no-rebuild] [--stop] [--duration seconds]

Options:
  --duration seconds  smoke-test wait before collecting logs (default: ${SMOKE_TEST_SECONDS})
  --no-rebuild        skip image rebuild and reuse existing VM artifacts
  --stop              stop a running VM and exit
  -h, --help          show this help message

Environment variables:
  QNX_SDP_ROOT        path to the QNX SDP root
  QNX_ARCH            x86_64 or aarch64le
  CMAKE_BUILD_TYPE    Release or Debug
  TARGET_DIR          mkqnximage target directory
  SAFETY_BIN_DIR      directory containing safety domain binaries
  NON_SAFETY_BIN_DIR  directory containing non-safety domain binaries
  SMOKE_TEST_SECONDS  default smoke-test duration
EOF
}

while [[ $# -gt 0 ]]; do
    case "${1}" in
        --duration)
            if [[ $# -lt 2 ]]; then
                echo "--duration requires a value" >&2
                exit 1
            fi
            SMOKE_TEST_SECONDS="$2"
            shift 2
            ;;
        --no-rebuild)
            OPT_NO_REBUILD=1
            shift
            ;;
        --stop)
            OPT_STOP=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: ${1}" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ ! "${SMOKE_TEST_SECONDS}" =~ ^[0-9]+$ || "${SMOKE_TEST_SECONDS}" -eq 0 ]]; then
    echo "Invalid smoke-test duration: ${SMOKE_TEST_SECONDS}" >&2
    exit 1
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

_print_section() {
    local title="$1"
    echo
    echo "===== ${title} ====="
}

_find_conflicting_qemu_targets() {
    local proc_dir
    local pid
    local cwd
    local cmdline

    for proc_dir in /proc/[0-9]*; do
        pid="${proc_dir##*/}"
        [[ -r "${proc_dir}/cmdline" ]] || continue

        cmdline="$(tr '\0' ' ' < "${proc_dir}/cmdline" 2>/dev/null || true)"
        [[ "${cmdline}" == qemu-system-x86_64* ]] || continue

        cwd="$(readlink "${proc_dir}/cwd" 2>/dev/null || true)"
        [[ -n "${cwd}" ]] || continue

        case "${cwd}" in
            "${WORKSPACE_ROOT}"/qnx/targets/*)
                if [[ "${cwd}" != "${TARGET_DIR}" ]]; then
                    printf '%s\t%s\n' "${pid}" "${cwd}"
                fi
                ;;
        esac
    done
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

_host_bin_path() {
    local name="$1"
    case "${name}" in
        safe_edge_safety_io_adapters|safe_edge_policy_engine|safe_edge_vehicle_mock)
            echo "${SAFETY_BIN_DIR}/${name}"
            ;;
        safe_edge_cloud_gateway|safe_edge_ota_service|safe_edge_infotainment)
            echo "${NON_SAFETY_BIN_DIR}/${name}"
            ;;
        *)
            echo "Unknown vehicle node binary: ${name}" >&2
            return 1
            ;;
    esac
}

_validate_vehicle_binaries() {
    local name
    local bin

    for name in "${VEHICLE_NODE_BINS[@]}"; do
        bin="$(_host_bin_path "${name}")"
        if [[ ! -f "${bin}" ]]; then
            echo "Binary not found: ${bin}" >&2
            echo "Build with: bash scripts/build_qnx.sh" >&2
            exit 1
        fi
        _validate_qnx_binary "${bin}"
    done
}

_refresh_vehicle_system_files_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/system_files.custom"
    local name
    local bin

    mkdir -p "$(dirname "${snippet}")"
    {
        echo "# local/snippets/system_files.custom"
        echo "# Generated by scripts/launch_tpi_2_5_test.sh"
        for name in "${VEHICLE_NODE_BINS[@]}"; do
            bin="$(_host_bin_path "${name}")"
            echo "[perms=555] bin/${name}=${bin}"
        done
    } > "${snippet}"
}

_refresh_vehicle_ifs_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/ifs_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/ifs_start.custom
# Generated by scripts/launch_tpi_2_5_test.sh
EOF
}

_refresh_vehicle_post_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/post_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/post_start.custom
# Generated by scripts/launch_tpi_2_5_test.sh
route add -net 224.0.0.0/4 vtnet0
EOF
}

_reset_generated_target_output() {
    rm -rf "${TARGET_DIR}/output"
}

_prepare_local_target_dirs() {
    mkdir -p "${TARGET_DIR}/local/misc_files" "${TARGET_DIR}/local/snippets"
}

_remote_log_path_for_bin() {
    local bin="$1"
    echo "/tmp/safe_edge_vehicle_nodes/${bin}.log"
}

_escape_single_quotes() {
    local value="$1"
    printf "%s" "${value//\'/\'\\\'\'}"
}

_remote_pid_is_running() {
    local ip="$1"
    local bin="$2"
    local pid_file="/tmp/safe_edge_vehicle_nodes/${bin}.pid"
    local escaped_pid_file

    escaped_pid_file="$(_escape_single_quotes "${pid_file}")"
    _ssh_run "${ip}" "test -f '${escaped_pid_file}' && pid=\$(cat '${escaped_pid_file}') && kill -0 \"\$pid\" 2>/dev/null" >/dev/null 2>&1
}

_remote_file_nonempty() {
    local ip="$1"
    local file="$2"
    local escaped_file

    escaped_file="$(_escape_single_quotes "${file}")"
    _ssh_run "${ip}" "test -s '${escaped_file}'" >/dev/null 2>&1
}

_remote_file_contains() {
    local ip="$1"
    local file="$2"
    local pattern="$3"
    local escaped_file escaped_pattern

    escaped_file="$(_escape_single_quotes "${file}")"
    escaped_pattern="$(_escape_single_quotes "${pattern}")"
    _ssh_run "${ip}" "grep -Fq -- '${escaped_pattern}' '${escaped_file}'" >/dev/null 2>&1
}

_wait_for_remote_file_contains() {
    local ip="$1"
    local file="$2"
    local pattern="$3"
    local max_tries="${4:-8}"
    local sleep_seconds="${5:-1}"
    local i

    for ((i = 1; i <= max_tries; i++)); do
        if _remote_file_contains "${ip}" "${file}" "${pattern}"; then
            return 0
        fi
        sleep "${sleep_seconds}"
    done

    return 1
}

_print_remote_log_tail() {
    local ip="$1"
    local file="$2"
    local escaped_file

    escaped_file="$(_escape_single_quotes "${file}")"
    _ssh_run "${ip}" "if [ -f '${escaped_file}' ]; then echo '--- ${file} ---'; tail -40 '${escaped_file}'; fi" || true
}

_start_vehicle_nodes() {
    local ip="$1"
    local cmd
    local name

    cmd='set -e; mkdir -p /tmp/safe_edge_vehicle_nodes /data/safe-edge-stage2;'
    cmd+=' printf "soc=50.0\nemergency_stop=0\nadas_fault=0\navailable_charge_kw=50.0\navailable_discharge_kw=50.0\nv2g_ready=1\nspeed_mps=0.0\nbraking_available=1\nsteering_available=1\n" > /data/safe-edge-stage2/input.txt;'
    cmd+=' rm -f /tmp/safe_edge_vehicle_nodes/*.pid /tmp/safe_edge_vehicle_nodes/*.log;'
    cmd+=' /system/bin/safe_edge_infotainment >/tmp/safe_edge_vehicle_nodes/safe_edge_infotainment.log 2>&1 & echo $! >/tmp/safe_edge_vehicle_nodes/safe_edge_infotainment.pid;'
    cmd+=' sleep 2;'
    for name in "${VEHICLE_NODE_BINS[@]}"; do
        [[ "${name}" == "safe_edge_infotainment" ]] && continue
        cmd+=" /system/bin/${name} >/tmp/safe_edge_vehicle_nodes/${name}.log 2>&1 & echo \$! >/tmp/safe_edge_vehicle_nodes/${name}.pid;"
    done

    _ssh_run "${ip}" "${cmd}"
}

_stop_vehicle_nodes() {
    local ip="$1"
    _ssh_run "${ip}" \
        "for f in /tmp/safe_edge_vehicle_nodes/*.pid; do [ -f \"\$f\" ] || continue; pid=\$(cat \"\$f\"); kill \"\$pid\" 2>/dev/null || true; done" \
        >/dev/null 2>&1 || true
}

_print_vehicle_node_logs() {
    local ip="$1"
    _ssh_run "${ip}" \
        "for f in /tmp/safe_edge_vehicle_nodes/*.log; do [ -f \"\$f\" ] || continue; echo \"--- \$f ---\"; tail -40 \"\$f\"; done" \
        || true
}

_service_name_for_bin() {
    local bin="$1"
    case "${bin}" in
        safe_edge_safety_io_adapters) echo "safety_io_adapters" ;;
        safe_edge_policy_engine) echo "policy_engine" ;;
        safe_edge_vehicle_mock) echo "vehicle_mock" ;;
        safe_edge_cloud_gateway) echo "cloud_gateway" ;;
        safe_edge_ota_service) echo "ota_service" ;;
        safe_edge_infotainment) echo "infotainment" ;;
        *)
            echo "Unknown service name for binary: ${bin}" >&2
            return 1
            ;;
    esac
}

_test_name_for_bin() {
    local bin="$1"
    case "${bin}" in
        safe_edge_infotainment) echo "InfotainmentLiveliness" ;;
        safe_edge_safety_io_adapters) echo "SafetyIoAdaptersLiveliness" ;;
        safe_edge_policy_engine) echo "PolicyEngineLiveliness" ;;
        safe_edge_vehicle_mock) echo "VehicleMockLiveliness" ;;
        safe_edge_cloud_gateway) echo "CloudGatewayLiveliness" ;;
        safe_edge_ota_service) echo "OtaServiceLiveliness" ;;
        *)
            echo "Unknown test name for binary: ${bin}" >&2
            return 1
            ;;
    esac
}

_run_liveliness_test_case() {
    local ip="$1"
    local name="$2"
    local test_name="$3"
    local infotainment_log="/tmp/safe_edge_vehicle_nodes/safe_edge_infotainment.log"
    local node_log service_name hb_line failed=0

    node_log="$(_remote_log_path_for_bin "${name}")"
    echo "[ RUN      ] ${test_name}"

    if _remote_pid_is_running "${ip}" "${name}"; then
        echo "    [qemu] pid exists and process is alive"
    else
        echo "    [qemu] FAIL pid missing or process not alive"
        failed=1
    fi

    if [[ "${name}" == "safe_edge_infotainment" ]]; then
        if _wait_for_remote_file_contains "${ip}" "${infotainment_log}" "Published ServiceHeartbeat" 8 1; then
            echo "    [dds]  Published ServiceHeartbeat found"
        else
            echo "    [dds]  FAIL Published ServiceHeartbeat not found"
            failed=1
        fi
    elif [[ "${name}" == "safe_edge_vehicle_mock" ]]; then
        if _remote_file_nonempty "${ip}" "${node_log}"; then
            echo "    [dds]  node log is not empty"
        else
            echo "    [dds]  FAIL node log is empty"
            failed=1
        fi
    else
        service_name="$(_service_name_for_bin "${name}")"
        hb_line="Received ServiceHeartbeat service=${service_name} status=HEALTH_OK detail=running"

        if _wait_for_remote_file_contains "${ip}" "${node_log}" "Published ServiceHeartbeat" 8 1; then
            echo "    [dds]  Published ServiceHeartbeat found"
        else
            echo "    [dds]  FAIL Published ServiceHeartbeat not found"
            failed=1
        fi

        if _wait_for_remote_file_contains "${ip}" "${infotainment_log}" "${hb_line}" 8 1; then
            echo "    [dds]  infotainment received heartbeat from ${service_name}"
        else
            echo "    [dds]  FAIL infotainment did not receive heartbeat from ${service_name}"
            failed=1
        fi
    fi

    if [[ "${failed}" -eq 0 ]]; then
        echo "[       OK ] ${test_name}"
        return 0
    fi

    echo "[  FAILED  ] ${test_name}"
    return 1
}

_test_nodes_launch_and_stay_alive() {
    local ip="$1"
    local name failed=0
    local total_tests=6
    local passed_tests=0
    local test_name

    _print_section "TEST: node launch and liveness"
    echo "infotainment starts first so it can register the heartbeats from the rest of nodes."
    echo "[==========] Running ${total_tests} liveliness tests."
    echo "[----------] ${total_tests} tests"

    for name in safe_edge_infotainment safe_edge_safety_io_adapters safe_edge_policy_engine safe_edge_vehicle_mock safe_edge_cloud_gateway safe_edge_ota_service; do
        test_name="$(_test_name_for_bin "${name}")"
        if _run_liveliness_test_case "${ip}" "${name}" "${test_name}"; then
            passed_tests=$((passed_tests + 1))
        else
            failed=1
        fi
    done

    echo "[----------] ${total_tests} tests"
    echo "[  PASSED  ] ${passed_tests} tests."
    if [[ "${passed_tests}" -lt "${total_tests}" ]]; then
        echo "[  FAILED  ] $((total_tests - passed_tests)) tests."
    fi

    return "${failed}"
}

_test_smoke_logs() {
    local ip="$1"
    local name failed=0

    _print_section "LOGS: startup smoke test"
    echo "Waiting ${SMOKE_TEST_SECONDS}s before collecting node logs..."
    sleep "${SMOKE_TEST_SECONDS}"

    for name in "${VEHICLE_NODE_BINS[@]}"; do
        if ! _remote_file_nonempty "${ip}" "$(_remote_log_path_for_bin "${name}")"; then
            echo "Log file is missing or empty for node: ${name}"
            failed=1
        fi
    done

    _print_vehicle_node_logs "${ip}"
    return "${failed}"
}

_prepare_local_target_dirs

CONFLICTING_QEMU_TARGETS="$(_find_conflicting_qemu_targets)"
if [[ -n "${CONFLICTING_QEMU_TARGETS}" ]]; then
    echo "A QNX VM from another repo target is still running." >&2
    echo "Stop it first, otherwise SSH may connect to the wrong guest image." >&2
    echo >&2
    echo "Conflicts detected:" >&2
    while IFS=$'\t' read -r pid cwd; do
        [[ -n "${pid}" ]] || continue
        echo "  PID ${pid}: ${cwd}" >&2
    done <<< "${CONFLICTING_QEMU_TARGETS}"
    exit 1
fi

cd "${TARGET_DIR}"

if [[ "${OPT_STOP}" -eq 1 ]]; then
    echo "Stopping QNX VM..."
    mkqnximage --stop 2>/dev/null || true
    kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true
    echo "VM stopped."
    exit 0
fi

kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true

if [[ "${OPT_NO_REBUILD}" -eq 0 ]]; then
    _validate_vehicle_binaries
    _refresh_vehicle_ifs_start_snippet
    _refresh_vehicle_post_start_snippet
    _refresh_vehicle_system_files_snippet
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

echo "Starting vehicle nodes..."
_start_vehicle_nodes "${VM_IP}"
echo "Vehicle nodes launched."

TEST_RC=0
_test_nodes_launch_and_stay_alive "${VM_IP}" || TEST_RC=1
_test_smoke_logs "${VM_IP}" || TEST_RC=1

_stop_vehicle_nodes "${VM_IP}"
echo -e "\nStopping QNX VM..."
mkqnximage --stop 2>/dev/null || true

exit "${TEST_RC}"
