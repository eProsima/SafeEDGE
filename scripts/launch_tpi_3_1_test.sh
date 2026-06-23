#!/usr/bin/env bash
# TPI 3.1 — Cloud Outage Resilience
# Starts safety + non-safety (QNX VM or native Linux) + edge + server (Docker),
# induces a controlled cloud outage, and verifies safety remains stable.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/test_output_common.sh"

QNX_USER="${USER:-$(id -un)}"
: "${QNX_SDP_ROOT:=/home/${QNX_USER}/qnx800}"
: "${QNX_ARCH:=x86_64}"
: "${CMAKE_BUILD_TYPE:=Release}"
: "${TARGET_DIR:=${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-${QNX_ARCH}}"
: "${SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${NON_SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/non-safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${SAFETY_NATIVE_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/safety-native-${CMAKE_BUILD_TYPE}/bin}"
: "${NON_SAFETY_NATIVE_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/non-safety-native-${CMAKE_BUILD_TYPE}/bin}"
: "${DISCOVERY_WAIT_SECS:=15}"
: "${OUTAGE_HOLD_SECS:=30}"
STABILITY_WINDOW_SECS=30

LOG_DIR="${SCRIPT_DIR}/logs"
RUNTIME_DIR="${LOG_DIR}/tpi_3_1_runtime"

_SSH_PASS="root"
_SSH_USER="root"
_SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=30 -o LogLevel=ERROR"

TEST_PLATFORM="qnx"
OPT_NO_REBUILD=0
OPT_STOP=0

SAFETY_BINS=(safe_edge_safety_io_adapters safe_edge_policy_engine safe_edge_vehicle_mock)
NON_SAFETY_BINS=(safe_edge_infotainment safe_edge_cloud_gateway)
VEHICLE_BINS=("${NON_SAFETY_BINS[@]}" "${SAFETY_BINS[@]}")

VM_IP=""
CHILD_PIDS=()

usage() {
    cat <<EOF
Usage: bash scripts/launch_tpi_3_1_test.sh [--linux] [--no-rebuild] [--stop] [-h]

  --linux        Run native Linux binaries (no QNX VM); CI-friendly
  --no-rebuild   Skip QNX image rebuild
  --stop         Stop running VM/processes and exit

Environment variables:
  DISCOVERY_WAIT_SECS  seconds to wait for DDS convergence (default: 15)
  OUTAGE_HOLD_SECS     seconds to hold the outage (default: 20)
  QNX_SDP_ROOT / QNX_ARCH / CMAKE_BUILD_TYPE / TARGET_DIR
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --linux|--ubuntu) TEST_PLATFORM="linux"; shift ;;
        --no-rebuild)     OPT_NO_REBUILD=1; shift ;;
        --stop)           OPT_STOP=1; shift ;;
        -h|--help)        usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ "${TEST_PLATFORM}" == "linux" ]]; then
    SAFETY_BIN_DIR="${SAFETY_NATIVE_BIN_DIR}"
    NON_SAFETY_BIN_DIR="${NON_SAFETY_NATIVE_BIN_DIR}"
fi

mkdir -p "${RUNTIME_DIR}"
LAUNCHER_LOG="${RUNTIME_DIR}/launch_tpi_3_1.log"
exec > >(tee "${LAUNCHER_LOG}") 2>&1

test_banner_open "TPI 3.1 - Cloud Outage Resilience"
test_banner_context "${TEST_PLATFORM}" "${RUNTIME_DIR}"

# ── Helpers ───────────────────────────────────────────────────────────────────

_ssh_run() {
    local ip="$1" cmd="$2"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" ssh ${_SSH_OPTS} "${_SSH_USER}@${ip}" "${cmd}"
}

_wait_for_ssh() {
    local ip="$1" i
    for ((i = 1; i <= 45; i++)); do
        if _ssh_run "${ip}" "true" >/dev/null 2>&1; then return 0; fi
        (( i == 1 || i % 5 == 0 )) && test_info "waiting for SSH (${i}/45)..."
        sleep 2
    done
    echo "Timed out waiting for SSH on ${ip}." >&2; return 1
}

_get_ip_address() {
    local ip i
    for ((i = 0; i < 20; i++)); do
        set +e; ip="$(mkqnximage --getip 2>/dev/null)"; set -e
        [[ -n "${ip}" ]] && { echo "${ip}"; return 0; }
        sleep 2
    done
    echo "Timed out waiting for VM IP." >&2; return 1
}

_get_linux_host_ip() {
    local ip=""
    set +e
    ip="$(ip route get 1.1.1.1 2>/dev/null | awk '{for (i = 1; i <= NF; ++i) if ($i == "src") { print $(i + 1); exit }}')"
    set -e
    if [[ -z "${ip}" ]]; then
        set +e
        ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
        set -e
    fi
    [[ -n "${ip}" ]] && echo "${ip}"
}

_detect_docker_network_mode() {
    [[ "${TEST_PLATFORM}" == "linux" ]] || return 0
    [[ -f "/.dockerenv" ]] || return 0
    local runner_hostname
    runner_hostname="$(hostname 2>/dev/null)" || return 0
    [[ -n "${runner_hostname}" ]] || return 0
    echo "Containerized runner detected (/.dockerenv); using --network container:${runner_hostname} for Docker containers"
    export SAFE_EDGE_DOCKER_NETWORK="container:${runner_hostname}"
}

_capture_host_network_diag() {
    local out_file="$1"
    {
        echo "[host] uname:"
        uname -a 2>/dev/null || true
        echo "[host] hostname:"
        hostname 2>/dev/null || true
        echo "[host] hostname -I:"
        hostname -I 2>/dev/null || true
        echo "[host] ip -brief addr:"
        ip -brief addr 2>/dev/null || true
        echo "[host] ip route:"
        ip route 2>/dev/null || true
        echo "[host] docker network ls:"
        docker network ls 2>/dev/null || true
        echo "[host] ss listeners 8001|8002|8003|8011|8012|8013|8020|8030:"
        ss -lunpt 2>/dev/null | grep -E ':(8001|8002|8003|8011|8012|8013|8020|8030)\b' || true
    } > "${out_file}" 2>&1
}

_capture_container_network_diag() {
    local name="$1" out_file="$2"
    {
        echo "[container] ${name}"
        docker inspect -f 'name={{.Name}} status={{.State.Status}} pid={{.State.Pid}} net={{.HostConfig.NetworkMode}}' "${name}" 2>/dev/null || true
        echo "[container] ip -brief addr:"
        docker exec "${name}" sh -lc "ip -brief addr 2>/dev/null || ifconfig 2>/dev/null || true" 2>/dev/null || true
        echo "[container] ip route:"
        docker exec "${name}" sh -lc "ip route 2>/dev/null || route -n 2>/dev/null || true" 2>/dev/null || true
        echo "[container] ss listeners:"
        docker exec "${name}" sh -lc "ss -lunpt 2>/dev/null | grep -E ':(8001|8002|8003|8011|8012|8013|8020|8030)\\b' || true" 2>/dev/null || true
    } > "${out_file}" 2>&1
}

_wait_for_container_running() {
    local name="$1" timeout="$2"
    local deadline=$(( $(date +%s) + timeout ))
    until docker ps -q --filter "name=^${name}$" | grep -q .; do
        if [[ $(date +%s) -gt ${deadline} ]]; then
            test_error "${name} did not start within ${timeout}s"
            exit 1
        fi
        sleep 2
    done
}

_host_bin_path() {
    case "$1" in
        safe_edge_safety_io_adapters|safe_edge_policy_engine|safe_edge_vehicle_mock)
            echo "${SAFETY_BIN_DIR}/$1" ;;
        safe_edge_infotainment|safe_edge_cloud_gateway)
            echo "${NON_SAFETY_BIN_DIR}/$1" ;;
    esac
}

_validate_binaries() {
    local name bin
    for name in "${VEHICLE_BINS[@]}"; do
        bin="$(_host_bin_path "${name}")"
        if [[ ! -f "${bin}" ]]; then
            echo "Binary not found: ${bin}" >&2
            if [[ "${TEST_PLATFORM}" == "linux" ]]; then
                echo "Build with: bash scripts/build_native.sh" >&2
            else
                echo "Build with: bash scripts/build_qnx.sh" >&2
            fi
            exit 1
        fi
    done
}

_remote_log() {
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        echo "${RUNTIME_DIR}/${1}.log"
    else
        echo "/tmp/tpi_3_1/${1}.log"
    fi
}

_log_line_count() {
    local log="$1"
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        wc -l < "${log}" 2>/dev/null || echo 0
    else
        _ssh_run "${VM_IP}" "wc -l < '${log}' 2>/dev/null || echo 0" 2>/dev/null || echo 0
    fi
}

# ── QNX snippet helpers ───────────────────────────────────────────────────────

_refresh_snippets() {
    local snip_sys="${TARGET_DIR}/local/snippets/system_files.custom"
    local name
    mkdir -p "$(dirname "${snip_sys}")"
    {
        echo "# Generated by launch_tpi_3_1_test.sh"
        for name in "${VEHICLE_BINS[@]}"; do
            echo "[perms=555] bin/${name}=$(_host_bin_path "${name}")"
        done
    } > "${snip_sys}"

    cat > "${TARGET_DIR}/local/snippets/ifs_start.custom" <<'EOF'
# Generated by launch_tpi_3_1_test.sh
EOF
    cat > "${TARGET_DIR}/local/snippets/post_start.custom" <<'EOF'
# Generated by launch_tpi_3_1_test.sh
route add -net 224.0.0.0/4 vtnet0
EOF
}

# ── Vehicle node lifecycle ────────────────────────────────────────────────────

_start_vehicle_nodes() {
    local name cmd
    local safety_env
    local non_safety_env

    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        safety_env=(
            "SAFE_EDGE_OWN_IP=${PEER_SAFETY_IP}"
            "SAFE_EDGE_CROSS_DOMAIN_IP=${PEER_NON_SAFETY_IP}"
            "SAFE_EDGE_HOST_IP=${DOCKER_OWN_IP}"
            "SAFE_EDGE_INITIAL_PEERS=${PEER_SAFETY_IP}:8001,${PEER_SAFETY_IP}:8002,${PEER_NON_SAFETY_IP}:8011,${DOCKER_OWN_IP}:8020,${DOCKER_OWN_IP}:8030"
        )
        non_safety_env=(
            "SAFE_EDGE_OWN_IP=${PEER_NON_SAFETY_IP}"
            "SAFE_EDGE_CROSS_DOMAIN_IP=${PEER_SAFETY_IP}"
            "SAFE_EDGE_HOST_IP=${DOCKER_OWN_IP}"
            "SAFE_EDGE_INITIAL_PEERS=${PEER_SAFETY_IP}:8001,${PEER_SAFETY_IP}:8002,${PEER_NON_SAFETY_IP}:8011,${DOCKER_OWN_IP}:8020,${DOCKER_OWN_IP}:8030"
        )
        rm -f "${RUNTIME_DIR}"/*.pid
        env "${non_safety_env[@]}" "${NON_SAFETY_BIN_DIR}/safe_edge_infotainment" > "${RUNTIME_DIR}/safe_edge_infotainment.log" 2>&1 &
        CHILD_PIDS+=($!); echo $! > "${RUNTIME_DIR}/safe_edge_infotainment.pid"
        sleep 1
        env "${non_safety_env[@]}" "${NON_SAFETY_BIN_DIR}/safe_edge_cloud_gateway" > "${RUNTIME_DIR}/safe_edge_cloud_gateway.log" 2>&1 &
        CHILD_PIDS+=($!); echo $! > "${RUNTIME_DIR}/safe_edge_cloud_gateway.pid"
        for name in "${SAFETY_BINS[@]}"; do
            env "${safety_env[@]}" "$(_host_bin_path "${name}")" > "${RUNTIME_DIR}/${name}.log" 2>&1 &
            CHILD_PIDS+=($!); echo $! > "${RUNTIME_DIR}/${name}.pid"
        done
        return 0
    fi

    local _peers="${VM_IP}:8001,${VM_IP}:8002,${VM_IP}:8011,${BRIDGE_IP}:8020,${BRIDGE_IP}:8030"
    local _env="SAFE_EDGE_OWN_IP=${VM_IP} SAFE_EDGE_HOST_IP=${BRIDGE_IP} SAFE_EDGE_INITIAL_PEERS=${_peers}"

    cmd="mkdir -p /tmp/tpi_3_1; rm -f /tmp/tpi_3_1/*.pid /tmp/tpi_3_1/*.log;"
    cmd+=" ${_env} /system/bin/safe_edge_infotainment > /tmp/tpi_3_1/safe_edge_infotainment.log 2>&1 & echo \$! > /tmp/tpi_3_1/safe_edge_infotainment.pid;"
    cmd+=" sleep 1;"
    for name in safe_edge_cloud_gateway "${SAFETY_BINS[@]}"; do
        cmd+=" ${_env} /system/bin/${name} > /tmp/tpi_3_1/${name}.log 2>&1 & echo \$! > /tmp/tpi_3_1/${name}.pid;"
    done
    _ssh_run "${VM_IP}" "${cmd}"
}

_stop_vehicle_nodes() {
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        local f
        for f in "${RUNTIME_DIR}"/*.pid; do
            [[ -f "${f}" ]] || continue
            kill "$(cat "${f}")" 2>/dev/null || true
        done
        return 0
    fi
    [[ -z "${VM_IP}" ]] && return 0
    _ssh_run "${VM_IP}" \
        "for f in /tmp/tpi_3_1/*.pid; do [ -f \"\$f\" ] || continue; kill \"\$(cat \"\$f\")\" 2>/dev/null || true; done" \
        >/dev/null 2>&1 || true
}

_collect_vehicle_logs() {
    local name tmp
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        return 0
    fi
    echo "Collecting vehicle node logs from VM..."
    for name in "${VEHICLE_BINS[@]}"; do
        tmp="$(mktemp)"
        if _ssh_run "${VM_IP}" "cat /tmp/tpi_3_1/${name}.log 2>/dev/null" > "${tmp}" 2>/dev/null; then
            cp "${tmp}" "${RUNTIME_DIR}/${name}.log"
        fi
        rm -f "${tmp}"
    done
}

# ── Cleanup ───────────────────────────────────────────────────────────────────

_stop_all() {
    _stop_vehicle_nodes 2>/dev/null || true
    local pid
    for pid in "${CHILD_PIDS[@]+"${CHILD_PIDS[@]}"}"; do
        kill "${pid}" 2>/dev/null || true
    done
    echo "Stopping safe-edge-edge..."
    docker stop safe-edge-edge   2>/dev/null || true
    echo "Stopping safe-edge-server..."
    docker stop safe-edge-server 2>/dev/null || true
    if [[ "${TEST_PLATFORM}" == "qnx" && -n "${VM_IP}" ]]; then
        echo "Stopping QNX VM..."
        (cd "${TARGET_DIR}" && mkqnximage --stop 2>/dev/null || true)
    fi
}
trap _stop_all EXIT

# ── Stop mode ─────────────────────────────────────────────────────────────────

if [[ "${OPT_STOP}" -eq 1 ]]; then
    _stop_vehicle_nodes 2>/dev/null || true
    docker stop safe-edge-edge   2>/dev/null || true
    docker stop safe-edge-server 2>/dev/null || true
    if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
        (cd "${TARGET_DIR}" && mkqnximage --stop 2>/dev/null || true)
        kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true
    fi
    echo "Stopped."
    exit 0
fi

# ── Preflight ─────────────────────────────────────────────────────────────────

test_info "Building Docker images for TPI 3.1"
if ! bash "${SCRIPT_DIR}/build_ubuntu.sh" > "${RUNTIME_DIR}/build_ubuntu.log" 2>&1; then
    echo "Docker image build failed. See ${RUNTIME_DIR}/build_ubuntu.log" >&2
    tail -50 "${RUNTIME_DIR}/build_ubuntu.log" >&2 || true
    exit 1
fi

_validate_binaries

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    [[ -f "${QNX_SDP_ROOT}/qnxsdp-env.sh" ]] || { echo "QNX SDK not found: ${QNX_SDP_ROOT}" >&2; exit 1; }
    # shellcheck source=/dev/null
    # qnxsdp-env.sh overrides SCRIPT_DIR — save and restore it
    _SAVED_SCRIPT_DIR="${SCRIPT_DIR}"
    source "${QNX_SDP_ROOT}/qnxsdp-env.sh" >/dev/null 2>&1
    SCRIPT_DIR="${_SAVED_SCRIPT_DIR}"
    [[ -d "${TARGET_DIR}" ]]                  || { echo "QNX target dir not found: ${TARGET_DIR}" >&2; exit 1; }
    command -v sshpass    >/dev/null 2>&1 || { echo "sshpass not found" >&2; exit 1; }
    command -v mkqnximage >/dev/null 2>&1 || { echo "mkqnximage not found" >&2; exit 1; }
    mkdir -p "${TARGET_DIR}/local/snippets" "${TARGET_DIR}/local/misc_files"
fi

# ── QNX VM startup ────────────────────────────────────────────────────────────

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    cd "${TARGET_DIR}"
    kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true

    if [[ "${OPT_NO_REBUILD}" -eq 0 ]]; then
        _refresh_snippets
        rm -rf "${TARGET_DIR}/output"
        test_info "Building QNX image"
        mkqnximage --noprompt --run=-h --clean >/dev/null 2>&1
    else
        test_info "Starting QNX QEMU (skipping rebuild)"
        mkqnximage --noprompt --run=-h >/dev/null 2>&1
    fi

    test_info "Waiting for VM IP"
    VM_IP="$(_get_ip_address)"
    test_info "VM is up: ${VM_IP}"
    test_info "Waiting for VM SSH"
    _wait_for_ssh "${VM_IP}"
fi

# ── DDS IP topology ───────────────────────────────────────────────────────────

echo "Resolving bridge IP from virbr0..."
set +e
BRIDGE_IP="$(ip -o -f inet addr show virbr0 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)"
BRIDGE_IP_RC=$?
set -e
echo "virbr0 lookup exit code: ${BRIDGE_IP_RC}"
if [[ -z "${BRIDGE_IP}" ]]; then
    echo "virbr0 lookup returned no IP; falling back to 127.0.0.1"
    BRIDGE_IP="127.0.0.1"
else
    echo "virbr0 IP detected: ${BRIDGE_IP}"
fi

if [[ "${TEST_PLATFORM}" == "linux" ]]; then
    LINUX_HOST_IP="$(_get_linux_host_ip)"
    if [[ -z "${LINUX_HOST_IP}" ]]; then
        echo "Could not resolve primary Linux host IP; falling back to 127.0.0.1"
        LINUX_HOST_IP="127.0.0.1"
    fi
    PEER_SAFETY_IP="${LINUX_HOST_IP}"
    PEER_NON_SAFETY_IP="${LINUX_HOST_IP}"
    DOCKER_OWN_IP="${LINUX_HOST_IP}"
else
    PEER_SAFETY_IP="${VM_IP}"
    PEER_NON_SAFETY_IP="${VM_IP}"
    DOCKER_OWN_IP="${BRIDGE_IP}"
fi
echo "Topology: safety=${PEER_SAFETY_IP} non_safety=${PEER_NON_SAFETY_IP} docker=${DOCKER_OWN_IP}"

# ── Start Docker containers ───────────────────────────────────────────────────

_detect_docker_network_mode

test_info "Starting safe-edge-server"
SAFE_EDGE_OWN_IP="${DOCKER_OWN_IP}" \
SAFE_EDGE_NON_SAFETY_IP="${PEER_NON_SAFETY_IP}" \
SAFE_EDGE_INITIAL_PEERS="${PEER_NON_SAFETY_IP}:8011,${DOCKER_OWN_IP}:8030" \
    bash "${SCRIPT_DIR}/launch_fast_server.sh" > "${RUNTIME_DIR}/safe_edge_server_launcher.log" 2>&1 &
CHILD_PIDS+=($!)

deadline=$(( $(date +%s) + 30 ))
until docker ps -q --filter "name=^safe-edge-server$" | grep -q .; do
    if [[ $(date +%s) -gt ${deadline} ]]; then
        test_error "safe-edge-server did not start within 30s"; exit 1
    fi
    sleep 2
done

test_info "Starting safe-edge-edge (5s delay)"
sleep 5
SAFE_EDGE_OWN_IP="${DOCKER_OWN_IP}" \
SAFE_EDGE_SAFETY_IP="${PEER_SAFETY_IP}" \
SAFE_EDGE_NON_SAFETY_IP="${PEER_NON_SAFETY_IP}" \
SAFE_EDGE_INITIAL_PEERS="${PEER_SAFETY_IP}:8001,${PEER_SAFETY_IP}:8002,${PEER_NON_SAFETY_IP}:8011,${DOCKER_OWN_IP}:8020" \
    bash "${SCRIPT_DIR}/launch_fast_edge.sh" > "${RUNTIME_DIR}/safe_edge_edge_launcher.log" 2>&1 &
CHILD_PIDS+=($!)

deadline=$(( $(date +%s) + 30 ))
until docker ps -q --filter "name=^safe-edge-edge$" | grep -q .; do
    if [[ $(date +%s) -gt ${deadline} ]]; then
        test_error "safe-edge-edge did not start within 30s"; exit 1
    fi
    sleep 2
done

# ── Start vehicle nodes ───────────────────────────────────────────────────────

test_info "Starting vehicle nodes"
_start_vehicle_nodes

# ── DDS convergence wait ──────────────────────────────────────────────────────

test_info "Waiting ${DISCOVERY_WAIT_SECS}s for DDS discovery"
sleep "${DISCOVERY_WAIT_SECS}"

PASS_COUNT=0
FAIL_COUNT=0
TOTAL_COUNT=0


_extract_since_line() {
    local file="$1" start_line="$2" out_file="$3"
    if [[ ! -f "${file}" ]]; then
        : > "${out_file}"
        return 0
    fi
    sed -n "$(( start_line + 1 )),\$p" "${file}" > "${out_file}" 2>/dev/null || : > "${out_file}"
}

_snapshot_container_runtime() {
    local name="$1" out_file="$2"
    {
        echo "[snapshot] container=${name}"
        docker inspect -f 'name={{.Name}} status={{.State.Status}} pid={{.State.Pid}} net={{.HostConfig.NetworkMode}}' "${name}" 2>/dev/null || true
        docker inspect -f '{{range .Config.Env}}{{println .}}{{end}}' "${name}" 2>/dev/null | grep '^SAFE_EDGE_' || true
    } > "${out_file}" 2>&1
}

docker logs safe-edge-server > "${RUNTIME_DIR}/docker_safe_edge_server_preflight.log" 2>&1 || true
docker logs safe-edge-edge > "${RUNTIME_DIR}/docker_safe_edge_edge_preflight.log" 2>&1 || true
_snapshot_container_runtime "safe-edge-server" "${RUNTIME_DIR}/docker_safe_edge_server_snapshot.log"
_snapshot_container_runtime "safe-edge-edge" "${RUNTIME_DIR}/docker_safe_edge_edge_snapshot.log"
_capture_host_network_diag "${RUNTIME_DIR}/host_network_snapshot.log"
_capture_container_network_diag "safe-edge-server" "${RUNTIME_DIR}/docker_safe_edge_server_network.log"
_capture_container_network_diag "safe-edge-edge" "${RUNTIME_DIR}/docker_safe_edge_edge_network.log"

# ── Edge baseline (setup precondition) ───────────────────────────────────────

: "${EDGE_OK_TIMEOUT_SECS:=60}"
test_info "Waiting for edge baseline (HEALTH_OK, timeout ${EDGE_OK_TIMEOUT_SECS}s)"
deadline=$(( $(date +%s) + EDGE_OK_TIMEOUT_SECS ))
until docker logs safe-edge-edge 2>&1 | grep -q "EdgeGatewayStatus status=OK"; do
    if [[ $(date +%s) -gt ${deadline} ]]; then
        test_warn "edge did not reach HEALTH_OK within ${EDGE_OK_TIMEOUT_SECS}s — proceeding anyway"
        break
    fi
    sleep 2
done

test_section "TPI 3.1 — Cloud Outage Resilience (${TEST_PLATFORM})"

_check() {
    local label="$1" file="$2" pattern="$3"
    TOTAL_COUNT=$(( TOTAL_COUNT + 1 ))
    echo "[ RUN      ] ${label}"
    if grep -q "${pattern}" "${file}" 2>/dev/null; then
        echo "[       OK ] ${label}"
        PASS_COUNT=$(( PASS_COUNT + 1 ))
    else
        echo "[  FAILED  ] ${label}"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
    fi
}

_check_alive() {
    local label="$1" name="$2"
    TOTAL_COUNT=$(( TOTAL_COUNT + 1 ))
    echo "[ RUN      ] ${label}"
    local alive=0
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        local pid_file="${RUNTIME_DIR}/${name}.pid"
        [[ -f "${pid_file}" ]] && kill -0 "$(cat "${pid_file}")" 2>/dev/null && alive=1
    else
        _ssh_run "${VM_IP}" \
            "pid=\$(cat /tmp/tpi_3_1/${name}.pid 2>/dev/null); [ -n \"\$pid\" ] && kill -0 \"\$pid\" 2>/dev/null" \
            >/dev/null 2>&1 && alive=1
    fi
    if [[ "${alive}" -eq 1 ]]; then
        echo "[       OK ] ${label}"
        PASS_COUNT=$(( PASS_COUNT + 1 ))
    else
        echo "[  FAILED  ] ${label}"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
    fi
}

_check_growing() {
    local label="$1" before="$2" after="$3"
    TOTAL_COUNT=$(( TOTAL_COUNT + 1 ))
    echo "[ RUN      ] ${label}"
    if [[ "${after}" -gt "${before}" ]]; then
        echo "[       OK ] ${label}"
        PASS_COUNT=$(( PASS_COUNT + 1 ))
    else
        echo "[  FAILED  ] ${label}"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
    fi
}

_print_diag_section() {
    local title="$1" file="$2" pattern="$3" lines="${4:-10}"
    test_info "${title}:"
    grep -E "${pattern}" "${file}" 2>/dev/null | tail -n "${lines}" || true
}

_print_failure_diagnostics() {
    local pe_diag_log="${RUNTIME_DIR}/safe_edge_policy_engine_post_stop.log"
    local cg_diag_log="${RUNTIME_DIR}/safe_edge_cloud_gateway_post_stop.log"
    [[ -f "${pe_diag_log}" ]] || pe_diag_log="${RUNTIME_DIR}/safe_edge_policy_engine.log"
    [[ -f "${cg_diag_log}" ]] || cg_diag_log="${RUNTIME_DIR}/safe_edge_cloud_gateway.log"

    echo ""
    echo "[----------] TPI 3.1 diagnostics"
    _print_diag_section "host network snapshot" \
        "${RUNTIME_DIR}/host_network_snapshot.log" \
        ".*" 80
    _print_diag_section "server runtime snapshot" \
        "${RUNTIME_DIR}/docker_safe_edge_server_snapshot.log" \
        ".*" 20
    _print_diag_section "edge runtime snapshot" \
        "${RUNTIME_DIR}/docker_safe_edge_edge_snapshot.log" \
        ".*" 20
    _print_diag_section "server network snapshot" \
        "${RUNTIME_DIR}/docker_safe_edge_server_network.log" \
        ".*" 40
    _print_diag_section "edge network snapshot" \
        "${RUNTIME_DIR}/docker_safe_edge_edge_network.log" \
        ".*" 40
    _print_diag_section "policy_engine PolicyDecision" \
        "${pe_diag_log}" \
        "Published PolicyDecision" 12
    _print_diag_section "policy_engine ServerAvailabilityStatus" \
        "${pe_diag_log}" \
        "Received ServerAvailabilityStatus" 12
    _print_diag_section "policy_engine EdgeGatewayStatus" \
        "${pe_diag_log}" \
        "Received EdgeGatewayStatus" 12
    _print_diag_section "policy_engine EnergyAdvisory" \
        "${pe_diag_log}" \
        "Received EnergyAdvisory" 12
    _print_diag_section "policy_engine match lines" \
        "${RUNTIME_DIR}/safe_edge_policy_engine.log" \
        "Subscription matched|Publication matched|Failed .*|START" 20
    _print_diag_section "cloud_gateway relevant lines" \
        "${cg_diag_log}" \
        "Published ServerAvailabilityStatus|Published ServiceHeartbeat|Received .*|Forwarding query to server" 20
    _print_diag_section "cloud_gateway match lines" \
        "${RUNTIME_DIR}/safe_edge_cloud_gateway.log" \
        "Subscription matched|Publication matched|Failed .*|START" 20
    _print_diag_section "server post-stop lines" \
        "${RUNTIME_DIR}/docker_safe_edge_server_post_stop.log" \
        ".*" 20
    _print_diag_section "server relevant lines" \
        "${RUNTIME_DIR}/docker_safe_edge_server.log" \
        "Published ServiceHeartbeat|Subscription matched|Publication matched|Failed .*" 20
    _print_diag_section "edge relevant lines" \
        "${RUNTIME_DIR}/docker_safe_edge_edge.log" \
        "Published EdgeGatewayStatus|Received .*|Subscription matched|Publication matched" 20
}

_print_outage_diagnosis() {
    local pe_log="${RUNTIME_DIR}/safe_edge_policy_engine_post_stop.log"
    local cg_log="${RUNTIME_DIR}/safe_edge_cloud_gateway_post_stop.log"
    local edge_log="${RUNTIME_DIR}/docker_safe_edge_edge.log"
    [[ -f "${pe_log}" ]] || pe_log="${RUNTIME_DIR}/safe_edge_policy_engine.log"
    [[ -f "${cg_log}" ]] || cg_log="${RUNTIME_DIR}/safe_edge_cloud_gateway.log"

    local saw_server_down=0
    local saw_edge_status=0
    local saw_edge_degraded=0
    local saw_policy_edge_available=0
    local saw_policy_edge_unavailable=0

    grep -q "Received ServerAvailabilityStatus server_available=false" "${pe_log}" 2>/dev/null && saw_server_down=1
    grep -q "Received EdgeGatewayStatus" "${pe_log}" 2>/dev/null && saw_edge_status=1
    grep -q "Published EdgeGatewayStatus status=DEGRADED" "${edge_log}" 2>/dev/null && saw_edge_degraded=1
    grep -q "reason=server_down_edge_available" "${pe_log}" 2>/dev/null && saw_policy_edge_available=1
    grep -q "reason=server_down_edge_unavailable" "${pe_log}" 2>/dev/null && saw_policy_edge_unavailable=1

    echo ""
    echo "[----------] TPI 3.1 diagnosis matrix"
    [[ "${saw_server_down}" -eq 1 ]]          && test_info "policy saw server_down         : 1" || test_warn "policy saw server_down         : 0"
    [[ "${saw_edge_status}" -eq 1 ]]          && test_info "policy saw edge status         : 1" || test_info "policy saw edge status         : 0"
    [[ "${saw_edge_degraded}" -eq 1 ]]        && test_info "edge published DEGRADED        : 1" || test_warn "edge published DEGRADED        : 0"
    [[ "${saw_policy_edge_available}" -eq 1 ]]   && test_info "policy chose edge_available    : 1" || test_info "policy chose edge_available    : 0"
    [[ "${saw_policy_edge_unavailable}" -eq 1 ]] && test_info "policy chose edge_unavailable  : 1" || test_info "policy chose edge_unavailable  : 0"

    if [[ "${saw_server_down}" -eq 1 && "${saw_edge_degraded}" -eq 1 && "${saw_edge_status}" -eq 0 ]]; then
        echo "[ DIAG     ] edge is reacting, but policy_engine is not receiving EdgeGatewayStatus during outage"
    elif [[ "${saw_server_down}" -eq 1 && "${saw_edge_status}" -eq 1 && "${saw_policy_edge_available}" -eq 0 && "${saw_policy_edge_unavailable}" -eq 1 ]]; then
        echo "[ DIAG     ] policy_engine receives some EdgeGatewayStatus, but still evaluates edge as unavailable"
    elif [[ "${saw_server_down}" -eq 1 && "${saw_policy_edge_available}" -eq 1 && "${saw_policy_edge_unavailable}" -eq 1 ]]; then
        echo "[ DIAG     ] policy transitions through both safe branches; this points to timing/ordering during outage"
    elif [[ "${saw_server_down}" -eq 0 ]]; then
        echo "[ DIAG     ] policy_engine never logged server_down; inspect cloud_gateway -> policy path"
    else
        echo "[ DIAG     ] no single dominant failure pattern detected; inspect the detailed sections above"
    fi

    grep -q "Published ServerAvailabilityStatus server_available=false" "${cg_log}" 2>/dev/null || \
        echo "[ DIAG     ] cloud_gateway did not log server_available=false in this run"
}

# ── STEP: induce outage ───────────────────────────────────────────────────────

POLICY_LINES_BEFORE_STOP="$(_log_line_count "${RUNTIME_DIR}/safe_edge_policy_engine.log")"
CLOUD_LINES_BEFORE_STOP="$(_log_line_count "${RUNTIME_DIR}/safe_edge_cloud_gateway.log")"
SIO_LINES_BEFORE_STOP="$(_log_line_count "${RUNTIME_DIR}/safe_edge_safety_io_adapters.log")"

OUTAGE_TS="$(date --utc +%Y-%m-%dT%H:%M:%SZ)"
test_info "Stopping safe-edge-server"
docker stop safe-edge-server >/dev/null
docker logs safe-edge-server > "${RUNTIME_DIR}/docker_safe_edge_server.log" 2>&1 || true
test_info "Stopping safe-edge-server"

test_info "Waiting for propagation (${OUTAGE_HOLD_SECS}s)"
sleep "${OUTAGE_HOLD_SECS}"
docker logs --since "${OUTAGE_TS}" safe-edge-edge > "${RUNTIME_DIR}/docker_safe_edge_edge.log" 2>&1 || true
docker logs --since "${OUTAGE_TS}" safe-edge-server > "${RUNTIME_DIR}/docker_safe_edge_server_post_stop.log" 2>&1 || true
_extract_since_line "${RUNTIME_DIR}/safe_edge_policy_engine.log" "${POLICY_LINES_BEFORE_STOP}" \
    "${RUNTIME_DIR}/safe_edge_policy_engine_post_stop.log"
_extract_since_line "${RUNTIME_DIR}/safe_edge_cloud_gateway.log" "${CLOUD_LINES_BEFORE_STOP}" \
    "${RUNTIME_DIR}/safe_edge_cloud_gateway_post_stop.log"
_extract_since_line "${RUNTIME_DIR}/safe_edge_safety_io_adapters.log" "${SIO_LINES_BEFORE_STOP}" \
    "${RUNTIME_DIR}/safe_edge_safety_io_adapters_post_stop.log"
test_info "Waiting for propagation (${OUTAGE_HOLD_SECS}s)"

# ── TEST: outage detection ────────────────────────────────────────────────────

_check "EdgeGatewayStatus DEGRADED" \
    "${RUNTIME_DIR}/docker_safe_edge_edge.log"    "DEGRADED"

_check "cloud_gateway published server_lost" \
    "${RUNTIME_DIR}/safe_edge_cloud_gateway_post_stop.log"  "Published ServerAvailabilityStatus server_available=false"

_check "policy_engine received server_down" \
    "${RUNTIME_DIR}/safe_edge_policy_engine_post_stop.log"  "Received ServerAvailabilityStatus server_available=false"

_check "policy_engine received EdgeGatewayStatus" \
    "${RUNTIME_DIR}/safe_edge_policy_engine_post_stop.log"  "Received EdgeGatewayStatus"

_check "PolicyDecision reason: server_down_edge_available" \
    "${RUNTIME_DIR}/safe_edge_policy_engine_post_stop.log"  "reason=server_down_edge_available"

_check "Published PolicyDecision during outage" \
    "${RUNTIME_DIR}/safe_edge_policy_engine_post_stop.log"  "Published PolicyDecision"

# ── STEP: safety stability window ────────────────────────────────────────────

PE_LOG="$(_remote_log safe_edge_policy_engine)"
SIO_LOG="$(_remote_log safe_edge_safety_io_adapters)"
LINES_PE_BEFORE="$(_log_line_count "${PE_LOG}")"
LINES_SIO_BEFORE="$(_log_line_count "${SIO_LOG}")"

test_info "Safety stability window (${STABILITY_WINDOW_SECS}s)"
sleep "${STABILITY_WINDOW_SECS}"
docker logs safe-edge-edge > "${RUNTIME_DIR}/docker_safe_edge_edge_final.log" 2>&1 || true
_collect_vehicle_logs
test_info "Safety stability window (${STABILITY_WINDOW_SECS}s)"

LINES_PE_AFTER="$(_log_line_count "${PE_LOG}")"
LINES_SIO_AFTER="$(_log_line_count "${SIO_LOG}")"

# ── TEST: safety liveness ─────────────────────────────────────────────────────

_check_alive "policy_engine alive"      "safe_edge_policy_engine"
_check_alive "safety_io_adapters alive" "safe_edge_safety_io_adapters"

_check_growing "policy_engine log active during stability window" \
    "${LINES_PE_BEFORE}"  "${LINES_PE_AFTER}"

_check_growing "safety_io_adapters log active during stability window" \
    "${LINES_SIO_BEFORE}" "${LINES_SIO_AFTER}"

echo ""
echo "[----------] ${TOTAL_COUNT} tests from TPI 3.1"
if [[ "${FAIL_COUNT}" -eq 0 ]]; then
    echo "[  PASSED  ] ${PASS_COUNT} tests."
else
    echo "[  PASSED  ] ${PASS_COUNT} tests."
    echo "[  FAILED  ] ${FAIL_COUNT} tests."
    _print_failure_diagnostics
    _print_outage_diagnosis
    test_footer "TPI 3.1 - Cloud Outage Resilience" "${FAIL_COUNT}" "${RUNTIME_DIR}"
    exit ${FAIL_COUNT}
fi
echo ""
test_artifact "Evidence directory" "${RUNTIME_DIR}"
test_footer "TPI 3.1 - Cloud Outage Resilience" 0 "${RUNTIME_DIR}"
exit 0
