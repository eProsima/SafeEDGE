#!/usr/bin/env bash
# TPI 3.4 — Graphical Interface and Infotainment Data Bridge
# Starts the full SafeDDS stack (QNX VM or --linux native), server+edge Docker
# containers, and the web dashboard. Verifies infotainment writes a status file.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/test_output_common.sh"

LOG_DIR="${SCRIPT_DIR}/logs"
RUNTIME_DIR="${LOG_DIR}/tpi_3_4_runtime"

QNX_USER="${USER:-$(id -un)}"
: "${QNX_SDP_ROOT:=/home/${QNX_USER}/qnx800}"
: "${QNX_ARCH:=x86_64}"
: "${CMAKE_BUILD_TYPE:=Release}"
: "${TARGET_DIR:=${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}}"
: "${SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${NON_SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/non-safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${SAFETY_NATIVE_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/safety-native-${CMAKE_BUILD_TYPE}/bin}"
: "${NON_SAFETY_NATIVE_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/non-safety-native-${CMAKE_BUILD_TYPE}/bin}"

_SSH_PASS="root"
_SSH_USER="root"
_SSH_CTL="/tmp/safe-edge-ssh-%h"
_SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=30 -o LogLevel=ERROR -o ControlMaster=auto -o ControlPath=${_SSH_CTL} -o ControlPersist=120"

OPT_NO_REBUILD=0
OPT_STOP=0
OPT_LINUX=0
OPT_LOCAL=0

TEST_PLATFORM="qnx"
STATUS_FILE_HOST="/tmp/safe-edge-status.json"
STATUS_FILE_VM="/data/safe-edge-stage2/status.json"
INPUT_FILE="${RUNTIME_DIR}/input.txt"
WEB_PORT="${SAFE_EDGE_WEB_PORT:-8080}"
STATUS_TIMEOUT=120

SERVER_CONTAINER="safe-edge-server"
EDGE_CONTAINER="safe-edge-edge"

VEHICLE_NODE_BINS=(
    safe_edge_safety_io_adapters
    safe_edge_policy_engine
    safe_edge_vehicle_mock
    safe_edge_cloud_gateway
    safe_edge_ota_service
    safe_edge_infotainment
)

INPUT_FILE_CONTENT='soc=50.0
emergency_stop=0
adas_fault=0
available_charge_kw=50.0
available_discharge_kw=50.0
v2g_ready=0
speed_mps=0.0
braking_available=1
steering_available=1'

usage() {
    cat <<EOF
Usage: bash scripts/launch_tpi_3_4_test.sh [options]

Options:
  --no-rebuild   skip QNX image rebuild
  --linux        run native Linux binaries instead of a QNX VM
  --ubuntu       alias for --linux
  --local        do not start the web dashboard (launch it manually)
  --stop         stop a running VM and exit
  -h, --help

Environment variables:
  QNX_SDP_ROOT               path to the QNX SDP root
  QNX_ARCH                   x86_64 or aarch64le
  CMAKE_BUILD_TYPE            Release or Debug
  TARGET_DIR                 mkqnximage target directory
  SAFETY_BIN_DIR             QNX safety domain binaries
  NON_SAFETY_BIN_DIR         QNX non-safety domain binaries
  SAFETY_NATIVE_BIN_DIR      native Linux safety domain binaries
  NON_SAFETY_NATIVE_BIN_DIR  native Linux non-safety domain binaries
  SAFE_EDGE_WEB_PORT         dashboard port (default: 8080)
EOF
}

while [[ $# -gt 0 ]]; do
    case "${1}" in
        --no-rebuild)   OPT_NO_REBUILD=1; shift ;;
        --linux|--ubuntu) OPT_LINUX=1; shift ;;
        --local)        OPT_LOCAL=1; shift ;;
        --stop)         OPT_STOP=1; shift ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "Unknown option: ${1}" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ "${OPT_LINUX}" -eq 1 ]]; then
    TEST_PLATFORM="linux"
    SAFETY_BIN_DIR="${SAFETY_NATIVE_BIN_DIR}"
    NON_SAFETY_BIN_DIR="${NON_SAFETY_NATIVE_BIN_DIR}"
fi

# ── Helpers ──────────────────────────────────────────────────────────────────

_ssh_run() {
    local ip="$1" cmd="$2"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" ssh ${_SSH_OPTS} "${_SSH_USER}@${ip}" "${cmd}"
}

_scp_put() {
    local ip="$1" local_src="$2" remote_dst="$3"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" scp ${_SSH_OPTS} "${local_src}" "${_SSH_USER}@${ip}:${remote_dst}"
}

_scp_get() {
    local ip="$1" remote_src="$2" local_dst="$3"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" scp ${_SSH_OPTS} "${_SSH_USER}@${ip}:${remote_src}" "${local_dst}"
}

_wait_for_ssh() {
    local ip="$1" max_tries=45 i
    for ((i = 1; i <= max_tries; i++)); do
        if _ssh_run "${ip}" "true" >/dev/null 2>&1; then return 0; fi
        if (( i == 1 || i % 5 == 0 )); then test_info "waiting for SSH (${i}/${max_tries})..."; fi
        sleep 2
    done
    echo "Timed out waiting for SSH on ${ip}." >&2; return 1
}

_get_ip_address() {
    local max_tries=20 ip i
    for ((i = 0; i < max_tries; i++)); do
        set +e; ip="$(mkqnximage --getip 2>/dev/null)"; set -e
        [[ -n "${ip}" ]] && echo "${ip}" && return 0
        sleep 2
    done
    echo "Timed out waiting for VM IP address." >&2; return 1
}

_get_linux_host_ip() {
    local ip=""
    set +e
    ip="$(ip route get 1.1.1.1 2>/dev/null | awk '{for (i=1;i<=NF;++i) if ($i=="src"){print $(i+1);exit}}')"
    set -e
    [[ -z "${ip}" ]] && { set +e; ip="$(hostname -I 2>/dev/null | awk '{print $1}')"; set -e; }
    [[ -n "${ip}" ]] && echo "${ip}"
}

_find_conflicting_qemu_targets() {
    local proc_dir pid cwd cmdline
    for proc_dir in /proc/[0-9]*; do
        pid="${proc_dir##*/}"
        [[ -r "${proc_dir}/cmdline" ]] || continue
        cmdline="$(tr '\0' ' ' < "${proc_dir}/cmdline" 2>/dev/null || true)"
        [[ "${cmdline}" == qemu-system-x86_64* ]] || continue
        cwd="$(readlink "${proc_dir}/cwd" 2>/dev/null || true)"
        [[ -n "${cwd}" ]] || continue
        case "${cwd}" in
            "${WORKSPACE_ROOT}"/qnx/targets/*)
                [[ "${cwd}" != "${TARGET_DIR}" ]] && printf '%s\t%s\n' "${pid}" "${cwd}" ;;
        esac
    done
    return 0
}

_validate_linux_binary() {
    local bin="$1" description
    description="$(file "${bin}")" || { echo "Failed to inspect: ${bin}" >&2; return 1; }
    grep -Fq "GNU/Linux" <<<"${description}" || {
        echo "Not a Linux binary: ${description}" >&2
        echo "Rebuild with: bash scripts/build_native.sh" >&2
        return 1
    }
}

_validate_qnx_binary() {
    local bin="$1" description
    description="$(file "${bin}")" || { echo "Failed to inspect: ${bin}" >&2; return 1; }
    grep -Fq "GNU/Linux" <<<"${description}" && {
        echo "Binary is Linux, not QNX: ${description}" >&2
        echo "Rebuild with: bash scripts/build_qnx.sh" >&2
        return 1
    }
    return 0
}

_host_bin_path() {
    case "${1}" in
        safe_edge_safety_io_adapters|safe_edge_policy_engine|safe_edge_vehicle_mock)
            echo "${SAFETY_BIN_DIR}/${1}" ;;
        safe_edge_cloud_gateway|safe_edge_ota_service|safe_edge_infotainment)
            echo "${NON_SAFETY_BIN_DIR}/${1}" ;;
        *) echo "Unknown binary: ${1}" >&2; return 1 ;;
    esac
}

_validate_vehicle_binaries() {
    local name bin
    for name in "${VEHICLE_NODE_BINS[@]}"; do
        bin="$(_host_bin_path "${name}")"
        if [[ ! -f "${bin}" ]]; then
            echo "Binary not found: ${bin}" >&2
            [[ "${TEST_PLATFORM}" == "linux" ]] \
                && echo "Build with: bash scripts/build_native.sh" >&2 \
                || echo "Build with: bash scripts/build_qnx.sh" >&2
            exit 1
        fi
        [[ "${TEST_PLATFORM}" == "linux" ]] \
            && _validate_linux_binary "${bin}" \
            || _validate_qnx_binary "${bin}"
    done
}

_refresh_vehicle_system_files_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/system_files.custom" name bin
    mkdir -p "$(dirname "${snippet}")"
    {
        echo "# Generated by scripts/launch_tpi_3_4_test.sh"
        for name in "${VEHICLE_NODE_BINS[@]}"; do
            bin="$(_host_bin_path "${name}")"
            echo "[perms=555] bin/${name}=${bin}"
        done
    } > "${snippet}"
}

_refresh_vehicle_ifs_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/ifs_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    echo "# Generated by scripts/launch_tpi_3_4_test.sh" > "${snippet}"
}

_refresh_vehicle_post_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/post_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# Generated by scripts/launch_tpi_3_4_test.sh
route add -net 224.0.0.0/4 vtnet0
EOF
}

_escape_single_quotes() { printf "%s" "${1//\'/\'\\\'\'}"; }

# ── Docker (server + edge) ────────────────────────────────────────────────────

_start_fast_stack() {
    local host_ip="$1" guest_ip="$2"
    test_info "Starting server + edge Docker containers"
    rm -f /dev/shm/fastdds_* /dev/shm/sem.fastdds_* 2>/dev/null || true
    docker rm -f "${SERVER_CONTAINER}" "${EDGE_CONTAINER}" 2>/dev/null || true

    local server_extra_args=()
    if [[ -d /etc/safe-edge ]]; then
        server_extra_args+=(-v /etc/safe-edge:/etc/safe-edge:ro)
    fi

    docker run -d \
        --name "${SERVER_CONTAINER}" \
        --network host \
        -e "SAFE_EDGE_OWN_IP=${host_ip}" \
        -e "SAFE_EDGE_NON_SAFETY_IP=${guest_ip}" \
        -e "SAFE_EDGE_INITIAL_PEERS=${guest_ip}:8011,${host_ip}:8030" \
        "${server_extra_args[@]+"${server_extra_args[@]}"}" \
        safe-edge-server:fast >/dev/null

    docker run -d \
        --name "${EDGE_CONTAINER}" \
        --network host \
        -e "SAFE_EDGE_OWN_IP=${host_ip}" \
        -e "SAFE_EDGE_SAFETY_IP=${guest_ip}" \
        -e "SAFE_EDGE_NON_SAFETY_IP=${guest_ip}" \
        -e "SAFE_EDGE_INITIAL_PEERS=${guest_ip}:8001,${guest_ip}:8002,${guest_ip}:8003,${guest_ip}:8011,${guest_ip}:8012,${guest_ip}:8013,${host_ip}:8020" \
        safe-edge-edge:fast >/dev/null
}

_wait_fast_stack() {
    local max_tries=20 i container
    for container in "${SERVER_CONTAINER}" "${EDGE_CONTAINER}"; do
        test_info "Waiting for ${container}"
        for ((i = 1; i <= max_tries; i++)); do
            docker inspect -f '{{.State.Running}}' "${container}" 2>/dev/null | grep -q true && break || true
            sleep 1
        done
        if (( i > max_tries )); then test_warn "${container} did not reach running state"; fi
    done
}

_stop_fast_stack() {
    docker rm -f "${SERVER_CONTAINER}" "${EDGE_CONTAINER}" 2>/dev/null || true
}

# ── Vehicle nodes ─────────────────────────────────────────────────────────────

_start_vehicle_nodes() {
    local ip="$1"
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        local safety_env non_safety_env
        safety_env=(
            "SAFE_EDGE_OWN_IP=${PEER_IP}"
            "SAFE_EDGE_CROSS_DOMAIN_IP=${PEER_IP}"
            "SAFE_EDGE_HOST_IP=${PEER_IP}"
            "SAFE_EDGE_INITIAL_PEERS=${PEER_IP}:8001,${PEER_IP}:8002,${PEER_IP}:8003,${PEER_IP}:8011,${PEER_IP}:8012,${PEER_IP}:8013,${PEER_IP}:8020,${PEER_IP}:8030"
        )
        non_safety_env=(
            "SAFE_EDGE_OWN_IP=${PEER_IP}"
            "SAFE_EDGE_CROSS_DOMAIN_IP=${PEER_IP}"
            "SAFE_EDGE_HOST_IP=${PEER_IP}"
            "SAFE_EDGE_INITIAL_PEERS=${PEER_IP}:8001,${PEER_IP}:8002,${PEER_IP}:8003,${PEER_IP}:8011,${PEER_IP}:8012,${PEER_IP}:8013,${PEER_IP}:8020,${PEER_IP}:8030"
            "SAFE_EDGE_STATUS_FILE=${STATUS_FILE_HOST}"
            "SAFE_EDGE_INPUT_FILE=${INPUT_FILE}"
        )
        mkdir -p "${RUNTIME_DIR}"
        printf '%s\n' "${INPUT_FILE_CONTENT}" > "${INPUT_FILE}"
        rm -f "${RUNTIME_DIR}"/*.pid "${RUNTIME_DIR}"/*.log
        local _ts="taskset -c 0"
        env "${non_safety_env[@]}" ${_ts} "${NON_SAFETY_BIN_DIR}/safe_edge_infotainment" \
            > "${RUNTIME_DIR}/safe_edge_infotainment.log" 2>&1 &
        echo $! > "${RUNTIME_DIR}/safe_edge_infotainment.pid"
        sleep 2
        for name in "${VEHICLE_NODE_BINS[@]}"; do
            [[ "${name}" == "safe_edge_infotainment" ]] && continue
            case "${name}" in
                safe_edge_safety_io_adapters|safe_edge_policy_engine)
                    env "${safety_env[@]}" ${_ts} "$(_host_bin_path "${name}")" \
                        > "${RUNTIME_DIR}/${name}.log" 2>&1 & ;;
                safe_edge_vehicle_mock)
                    env "${safety_env[@]}" "SAFE_EDGE_INPUT_FILE=${INPUT_FILE}" \
                        ${_ts} "${SAFETY_BIN_DIR}/${name}" > "${RUNTIME_DIR}/${name}.log" 2>&1 & ;;
                *)
                    env "${non_safety_env[@]}" ${_ts} "$(_host_bin_path "${name}")" \
                        > "${RUNTIME_DIR}/${name}.log" 2>&1 & ;;
            esac
            echo $! > "${RUNTIME_DIR}/${name}.pid"
        done
        return 0
    fi

    # QNX mode
    local input_escaped; input_escaped="$(_escape_single_quotes "${INPUT_FILE_CONTENT}")"
    local _peers="${ip}:8001,${ip}:8002,${ip}:8003,${ip}:8011,${ip}:8012,${ip}:8013,${BRIDGE_IP}:8020,${BRIDGE_IP}:8030"
    local _qnx_env="SAFE_EDGE_OWN_IP=${ip} SAFE_EDGE_HOST_IP=${BRIDGE_IP} SAFE_EDGE_CROSS_DOMAIN_IP=${BRIDGE_IP}"
    _qnx_env+=" SAFE_EDGE_INITIAL_PEERS=${_peers}"
    _qnx_env+=" SAFE_EDGE_STATUS_FILE=${STATUS_FILE_VM}"
    _qnx_env+=" SAFE_EDGE_INPUT_FILE=/data/safe-edge-stage2/input.txt"

    local cmd="set -e; mkdir -p /tmp/safe_edge_vehicle_nodes /data/safe-edge-stage2;"
    cmd+=" printf '${input_escaped}' > /data/safe-edge-stage2/input.txt;"
    cmd+=" rm -f /tmp/safe_edge_vehicle_nodes/*.pid /tmp/safe_edge_vehicle_nodes/*.log;"
    cmd+=" ${_qnx_env} /system/bin/safe_edge_infotainment > /tmp/safe_edge_vehicle_nodes/safe_edge_infotainment.log 2>&1 & echo \$! > /tmp/safe_edge_vehicle_nodes/safe_edge_infotainment.pid;"
    cmd+=" sleep 2;"
    for name in "${VEHICLE_NODE_BINS[@]}"; do
        [[ "${name}" == "safe_edge_infotainment" ]] && continue
        cmd+=" ${_qnx_env} /system/bin/${name} > /tmp/safe_edge_vehicle_nodes/${name}.log 2>&1 & echo \$! > /tmp/safe_edge_vehicle_nodes/${name}.pid;"
    done
    _ssh_run "${ip}" "${cmd}"
}

_stop_vehicle_nodes() {
    local ip="$1"
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        local pid_file
        for pid_file in "${RUNTIME_DIR}"/*.pid; do
            [[ -f "${pid_file}" ]] || continue
            kill "$(cat "${pid_file}")" 2>/dev/null || true
        done
        return 0
    fi
    _ssh_run "${ip}" \
        "for f in /tmp/safe_edge_vehicle_nodes/*.pid; do [ -f \"\$f\" ] || continue; kill \"\$(cat \"\$f\")\" 2>/dev/null || true; done" \
        >/dev/null 2>&1 || true
}

# ── File sync (QNX only) ──────────────────────────────────────────────────────

_start_file_sync() {
    local ip="$1"
    [[ "${TEST_PLATFORM}" == "qnx" ]] || return 0

    # Status: VM → host (ControlMaster reuses connection, no handshake per call)
    (
        while true; do
            _scp_get "${ip}" "${STATUS_FILE_VM}" "${STATUS_FILE_HOST}" \
                >> "${RUNTIME_DIR}/status_sync.log" 2>&1 || true
            sleep 0.5
        done
    ) &
    echo $! > "${RUNTIME_DIR}/status_sync.pid"
    test_info "File sync started (log: ${RUNTIME_DIR}/status_sync.log)"

    # Latency logs: VM → host — 1s between pairs (files can be large)
    (
        while true; do
            for _log_name in safe_edge_vehicle_mock safe_edge_policy_engine; do
                _scp_get "${ip}" \
                    "/tmp/safe_edge_vehicle_nodes/${_log_name}.log" \
                    "${RUNTIME_DIR}/${_log_name}.log" \
                    >> "${RUNTIME_DIR}/latency_sync.log" 2>&1 || true
            done
            sleep 1
        done
    ) &
    echo $! > "${RUNTIME_DIR}/latency_sync.pid"

    # Input: host → VM (vehicle_mock picks up dashboard writes)
    (
        local last_mtime=""
        while true; do
            if [[ -f "${INPUT_FILE}" ]]; then
                local mtime
                mtime="$(stat -c %Y "${INPUT_FILE}" 2>/dev/null || true)"
                if [[ "${mtime}" != "${last_mtime}" ]]; then
                    _scp_put "${ip}" "${INPUT_FILE}" "/data/safe-edge-stage2/input.txt" 2>/dev/null || true
                    last_mtime="${mtime}"
                fi
            fi
            sleep 1
        done
    ) &
    echo $! > "${RUNTIME_DIR}/input_sync.pid"
}

_stop_file_sync() {
    local f
    for f in "${RUNTIME_DIR}/status_sync.pid" "${RUNTIME_DIR}/latency_sync.pid" "${RUNTIME_DIR}/input_sync.pid"; do
        [[ -f "${f}" ]] || continue
        kill "$(cat "${f}")" 2>/dev/null || true
        rm -f "${f}"
    done
}

# ── Web dashboard ─────────────────────────────────────────────────────────────

_start_dashboard() {
    if [[ "${OPT_LOCAL}" -eq 1 ]]; then
        test_info "--local: skipping dashboard. Launch manually:"
        echo "  SAFE_EDGE_STATUS_FILE=${STATUS_FILE_HOST} SAFE_EDGE_INPUT_FILE=${INPUT_FILE} SAFE_EDGE_LOG_DIR=${RUNTIME_DIR} SAFE_EDGE_WEB_PORT=${WEB_PORT} python3 ${SCRIPT_DIR}/web/safe_edge_dashboard.py"
        return 0
    fi
    # In QNX mode, clear stale latency logs so dashboard starts at position 0
    # (logs will be synced fresh from VM; in Linux mode nodes write directly here)
    if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
        rm -f "${RUNTIME_DIR}/safe_edge_vehicle_mock.log" \
              "${RUNTIME_DIR}/safe_edge_policy_engine.log" 2>/dev/null || true
    fi
    test_info "Starting web dashboard on port ${WEB_PORT}"
    SAFE_EDGE_STATUS_FILE="${STATUS_FILE_HOST}" \
    SAFE_EDGE_INPUT_FILE="${INPUT_FILE}" \
    SAFE_EDGE_LOG_DIR="${RUNTIME_DIR}" \
    SAFE_EDGE_WEB_PORT="${WEB_PORT}" \
    SAFE_EDGE_HOST_IP="${BRIDGE_IP}" \
    SAFE_EDGE_GUEST_IP="${GUEST_IP}" \
    SAFE_EDGE_PLATFORM="${TEST_PLATFORM}" \
        python3 "${SCRIPT_DIR}/web/safe_edge_dashboard.py" \
        > "${RUNTIME_DIR}/dashboard.log" 2>&1 &
    echo $! > "${RUNTIME_DIR}/dashboard.pid"
}

_stop_dashboard() {
    local f="${RUNTIME_DIR}/dashboard.pid"
    [[ -f "${f}" ]] || return 0
    kill "$(cat "${f}")" 2>/dev/null || true
    rm -f "${f}"
}

# ── Status file verification ──────────────────────────────────────────────────

_wait_for_status_file() {
    local max_tries=$(( STATUS_TIMEOUT / 2 )) i
    test_info "Waiting for status file (up to ${STATUS_TIMEOUT}s)"
    for ((i = 1; i <= max_tries; i++)); do
        if [[ "${TEST_PLATFORM}" == "qnx" && -n "${VM_IP}" ]]; then
            _scp_get "${VM_IP}" "${STATUS_FILE_VM}" "${STATUS_FILE_HOST}" \
                >> "${RUNTIME_DIR}/status_sync.log" 2>&1 || true
        fi
        if [[ -f "${STATUS_FILE_HOST}" && -s "${STATUS_FILE_HOST}" ]]; then
            return 0
        fi
        sleep 2
    done
    test_error "Status file not found in VM after ${STATUS_TIMEOUT}s. Fetching infotainment log..."
    if [[ "${TEST_PLATFORM}" == "qnx" && -n "${VM_IP}" ]]; then
        _scp_get "${VM_IP}" \
            "/tmp/safe_edge_vehicle_nodes/safe_edge_infotainment.log" \
            "${RUNTIME_DIR}/infotainment_qnx.log" 2>/dev/null || true
        if [[ -s "${RUNTIME_DIR}/infotainment_qnx.log" ]]; then
            test_error "Infotainment log (first 30 lines):"
            head -30 "${RUNTIME_DIR}/infotainment_qnx.log" >&2
            test_error "Infotainment log (last 30 lines):"
            tail -30 "${RUNTIME_DIR}/infotainment_qnx.log" >&2
        else
            test_error "Infotainment log empty or not found — binary may have crashed at launch"
        fi
        test_error "QNX VM filesystem state:"
        _ssh_run "${VM_IP}" "ls -la /tmp/safe_edge_vehicle_nodes/ 2>/dev/null || echo 'DIR MISSING'" >&2 2>/dev/null || true
        _ssh_run "${VM_IP}" "ls -la ${STATUS_FILE_VM} 2>/dev/null || echo 'STATUS FILE MISSING: ${STATUS_FILE_VM}'" >&2 2>/dev/null || true
        _ssh_run "${VM_IP}" "ls -la /data/safe-edge-stage2/ 2>/dev/null || echo '/data/safe-edge-stage2/ NOT ACCESSIBLE'" >&2 2>/dev/null || true
        test_error "Searching for any .json files in /tmp and /data on QNX:"
        _ssh_run "${VM_IP}" "find /tmp /data -name '*.json' 2>/dev/null || true" >&2 2>/dev/null || true
    fi
    return 1
}

# ── Stop all ──────────────────────────────────────────────────────────────────

_ORIG_GOVERNOR=""
_set_cpu_governor() {
    local gov="$1"
    local path="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
    [[ -f "${path}" ]] || return 0
    if [[ "${gov}" == "performance" ]]; then
        _ORIG_GOVERNOR="$(cat "${path}" 2>/dev/null || true)"
    fi
    if echo "${gov}" | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1; then
        echo "[cpu] governor → ${gov}"
    else
        echo "[cpu] WARNING: could not set governor to ${gov} (try: sudo visudo)" >&2
    fi
}

_stop_all() {
    rm -f "${STATUS_FILE_HOST}" 2>/dev/null || true
    # Close ControlMaster socket if open
    if [[ -n "${VM_IP:-}" ]]; then
        ssh -o ControlPath="${_SSH_CTL}" -O exit "${_SSH_USER}@${VM_IP}" 2>/dev/null || true
    fi
    _stop_dashboard
    _stop_file_sync
    _stop_vehicle_nodes "${VM_IP:-}"
    _stop_fast_stack
    if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
        mkqnximage --stop 2>/dev/null || true
        pkill -TERM -f qemu-system-x86_64 2>/dev/null || true
    fi
}

# ── Preflight checks ──────────────────────────────────────────────────────────

mkdir -p "${LOG_DIR}" "${RUNTIME_DIR}"
test_banner_open "TPI 3.4 - Graphical Interface and Infotainment Data Bridge"
test_banner_context "${TEST_PLATFORM}" "${RUNTIME_DIR}"

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    CONFLICTING_QEMU_TARGETS="$(_find_conflicting_qemu_targets)"
    if [[ -n "${CONFLICTING_QEMU_TARGETS}" ]]; then
        echo "A QNX VM from another repo target is still running." >&2
        while IFS=$'\t' read -r pid cwd; do
            [[ -n "${pid}" ]] || continue
            echo "  PID ${pid}: ${cwd}" >&2
        done <<< "${CONFLICTING_QEMU_TARGETS}"
        exit 1
    fi
fi

if [[ "${TEST_PLATFORM}" == "qnx" && ! -f "${QNX_SDP_ROOT}/qnxsdp-env.sh" ]]; then
    echo "QNX SDK not found at QNX_SDP_ROOT='${QNX_SDP_ROOT}'" >&2; exit 1
fi
if [[ "${TEST_PLATFORM}" == "qnx" && ! -d "${TARGET_DIR}" ]]; then
    echo "QNX target directory not found: ${TARGET_DIR}" >&2; exit 1
fi
if [[ "${TEST_PLATFORM}" == "qnx" ]] && ! command -v sshpass >/dev/null 2>&1; then
    echo "sshpass not found: sudo apt install sshpass" >&2; exit 1
fi
if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found — required for server+edge containers" >&2; exit 1
fi
if ! docker image inspect safe-edge-server:fast >/dev/null 2>&1; then
    echo "Image safe-edge-server:fast not found. Build: bash scripts/build_ubuntu.sh" >&2; exit 1
fi
if ! docker image inspect safe-edge-edge:fast >/dev/null 2>&1; then
    echo "Image safe-edge-edge:fast not found. Build: bash scripts/build_ubuntu.sh" >&2; exit 1
fi

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    _SAVED_SCRIPT_DIR="${SCRIPT_DIR}"
    source "${QNX_SDP_ROOT}/qnxsdp-env.sh" >/dev/null 2>&1
    SCRIPT_DIR="${_SAVED_SCRIPT_DIR}"
fi

# ── Main ──────────────────────────────────────────────────────────────────────

VM_IP=""
PEER_IP=""
BRIDGE_IP=""
trap '_stop_all' EXIT

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    if [[ "${OPT_STOP}" -eq 1 ]]; then
        echo "Stopping QNX VM..."
        mkqnximage --stop 2>/dev/null || true
        kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true
        echo "VM stopped."
        exit 0
    fi

    kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true
    _validate_vehicle_binaries
    cd "${TARGET_DIR}"

    if [[ "${OPT_NO_REBUILD}" -eq 0 ]]; then
        _refresh_vehicle_ifs_start_snippet
        _refresh_vehicle_post_start_snippet
        _refresh_vehicle_system_files_snippet
        rm -rf "${TARGET_DIR}/output"
        test_info "Building QNX image and starting QEMU..."
        mkqnximage --noprompt --run=-h --clean >/dev/null 2>&1
    else
        test_info "Starting QNX QEMU (skipping rebuild)..."
        mkqnximage --noprompt --run=-h >/dev/null 2>&1
    fi

    test_info "Waiting for VM IP..."
    VM_IP="$(_get_ip_address)"
    test_info "VM is up: ${VM_IP}"
    _wait_for_ssh "${VM_IP}"
    test_info "VM is reachable."
else
    if [[ "${OPT_STOP}" -eq 1 ]]; then
        _stop_vehicle_nodes ""
        echo "Processes stopped."
        exit 0
    fi
    _validate_vehicle_binaries
    test_info "Running native Linux mode."
fi

if [[ "${TEST_PLATFORM}" == "linux" ]]; then
    PEER_IP="$(_get_linux_host_ip)"
    : "${PEER_IP:=127.0.0.1}"
    BRIDGE_IP="${PEER_IP}"
else
    BRIDGE_IP="$(ip -o -f inet addr show virbr0 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)"
    : "${BRIDGE_IP:=192.168.122.1}"
    PEER_IP="127.0.0.1"
fi
GUEST_IP="${VM_IP:-${BRIDGE_IP}}"
test_info "DDS IPs: host/bridge=${BRIDGE_IP} guest=${GUEST_IP}"

_start_fast_stack "${BRIDGE_IP}" "${GUEST_IP}"
_wait_fast_stack

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    test_info "QNX filesystem write test..."
    _status_dir="$(dirname "${STATUS_FILE_VM}")"
    if _ssh_run "${VM_IP}" \
        "mkdir -p ${_status_dir} && touch ${STATUS_FILE_VM} && echo WRITABLE || echo NOT_WRITABLE" 2>/dev/null | grep -q WRITABLE; then
        test_info "QNX can write to ${STATUS_FILE_VM} — removing test file"
        _ssh_run "${VM_IP}" "rm -f ${STATUS_FILE_VM}" 2>/dev/null || true
    else
        test_error "QNX CANNOT write to ${STATUS_FILE_VM} — check filesystem"
        exit 1
    fi
fi

test_info "Starting vehicle nodes"
_start_vehicle_nodes "${VM_IP}"

_start_file_sync "${VM_IP}"
_start_dashboard

if _wait_for_status_file; then
    test_info "PASS: status file present"
    test_info "Contents:"
    cat "${STATUS_FILE_HOST}"
else
    test_error "FAIL: status file not written within ${STATUS_TIMEOUT}s"
    test_footer "TPI 3.4 - Graphical Interface and Infotainment Data Bridge" 1 ""
    exit 1
fi

TEST_RC=0

echo ""
echo "================================================"
echo "Dashboard: http://${PEER_IP}:${WEB_PORT}"
echo "Status file: ${STATUS_FILE_HOST}"
echo "Input file:  ${INPUT_FILE}"
echo "Logs: ${RUNTIME_DIR}/"
echo "Stop: bash scripts/launch_tpi_3_4_test.sh --stop"
echo "================================================"
echo ""

if [[ -t 0 ]]; then
    # Interactive: keep running until Ctrl+C
    echo "Press Ctrl+C to stop all nodes and exit."
    _STOPPING=0
    _on_int() { _STOPPING=1; _stop_all; exit 0; }
    trap '_on_int' INT TERM
    while [[ "${_STOPPING}" -eq 0 ]]; do
        wait || true
    done
else
    # CI mode: report result and clean up
    test_footer "TPI 3.4 - Graphical Interface and Infotainment Data Bridge" "${TEST_RC}" ""
    exit "${TEST_RC}"
fi
