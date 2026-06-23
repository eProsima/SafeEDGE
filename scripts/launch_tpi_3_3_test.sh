#!/usr/bin/env bash
# Run TPI 3.3 Mixed Traffic and Concurrent Workload benchmark for SafeDDS nodes.
# Based on launch_tpi_2_6_test.sh — adds server+edge Docker containers for mixed DDS traffic.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${SCRIPT_DIR}/logs"
RUNTIME_DIR="${LOG_DIR}/tpi_3_3_runtime"
BASELINE_RAW_LOG="${RUNTIME_DIR}/tpi_3_3_baseline_raw.log"
LOADED_RAW_LOG="${RUNTIME_DIR}/tpi_3_3_loaded_raw.log"
REPORT="${RUNTIME_DIR}/tpi_3_3_report.txt"

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
_SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=30 -o LogLevel=ERROR"

OPT_NO_REBUILD=0
OPT_STOP=0
OPT_SAMPLES=100
OPT_LOAD=2
OPT_PRIO=0
OPT_LINUX=0

TEST_PLATFORM="qnx"
INPUT_FILE="${RUNTIME_DIR}/input.txt"

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
v2g_ready=1
speed_mps=0.0
braking_available=1
steering_available=1'

E2E_THRESHOLD_MS=20
REACTION_THRESHOLD_MS=100

usage() {
    cat <<EOF
Usage: bash scripts/launch_tpi_3_3_test.sh [options]

Options:
  --no-rebuild        skip QNX image rebuild
  --linux             run native Linux binaries instead of a QNX VM
  --ubuntu            alias for --linux
  --samples N         number of measurement samples (default: ${OPT_SAMPLES})
  --load LEVEL        load level: 0=baseline, 1=medium (CPU), 2=high (CPU+I/O) (default: ${OPT_LOAD})
  --prio              start VM + nodes, print thread priorities via pidin, then exit
  --stop              stop a running VM and exit
  -h, --help

Environment variables:
  QNX_SDP_ROOT        path to the QNX SDP root
  QNX_ARCH            x86_64 or aarch64le
  CMAKE_BUILD_TYPE    Release or Debug
  TARGET_DIR          mkqnximage target directory
  SAFETY_BIN_DIR      directory containing safety domain binaries
  NON_SAFETY_BIN_DIR  directory containing non-safety domain binaries
  SAFETY_NATIVE_BIN_DIR      directory containing native safety domain binaries
  NON_SAFETY_NATIVE_BIN_DIR  directory containing native non-safety domain binaries
EOF
}

while [[ $# -gt 0 ]]; do
    case "${1}" in
        --no-rebuild)
            OPT_NO_REBUILD=1
            shift
            ;;
        --linux|--ubuntu)
            OPT_LINUX=1
            shift
            ;;
        --samples)
            if [[ $# -lt 2 ]]; then
                echo "--samples requires a value" >&2; exit 1
            fi
            OPT_SAMPLES="$2"
            shift 2
            ;;
        --load)
            if [[ $# -lt 2 ]]; then
                echo "--load requires a value" >&2; exit 1
            fi
            OPT_LOAD="$2"
            shift 2
            ;;
        --stop)
            OPT_STOP=1
            shift
            ;;
        --prio)
            OPT_PRIO=1
            shift
            ;;
        -h|--help)
            usage; exit 0
            ;;
        *)
            echo "Unknown option: ${1}" >&2; usage >&2; exit 1
            ;;
    esac
done

if [[ "${OPT_LINUX}" -eq 1 ]]; then
    TEST_PLATFORM="linux"
    SAFETY_BIN_DIR="${SAFETY_NATIVE_BIN_DIR}"
    NON_SAFETY_BIN_DIR="${NON_SAFETY_NATIVE_BIN_DIR}"
fi

if [[ ! "${OPT_SAMPLES}" =~ ^[0-9]+$ || "${OPT_SAMPLES}" -lt 1 ]]; then
    echo "Invalid --samples value: ${OPT_SAMPLES}" >&2; exit 1
fi
if [[ ! "${OPT_LOAD}" =~ ^[012]$ ]]; then
    echo "Invalid --load value: ${OPT_LOAD} (must be 0, 1, or 2)" >&2; exit 1
fi

mkdir -p "${LOG_DIR}" "${RUNTIME_DIR}"

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

_ssh_run() {
    local ip="$1"
    local cmd="$2"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" ssh ${_SSH_OPTS} "${_SSH_USER}@${ip}" "${cmd}"
}

_scp_get() {
    local ip="$1"
    local remote="$2"
    local local_dst="$3"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" scp ${_SSH_OPTS} "${_SSH_USER}@${ip}:${remote}" "${local_dst}"
}

_wait_for_ssh() {
    local ip="$1"
    local max_tries=45
    local i
    for ((i = 1; i <= max_tries; i++)); do
        if _ssh_run "${ip}" "true" >/dev/null 2>&1; then return 0; fi
        if (( i == 1 || i % 5 == 0 )); then echo "  waiting for SSH (${i}/${max_tries})..."; fi
        sleep 2
    done
    echo "Timed out waiting for SSH on ${ip}." >&2; return 1
}

_get_ip_address() {
    local max_tries=20
    local ip i
    for ((i = 0; i < max_tries; i++)); do
        set +e; ip="$(mkqnximage --getip 2>/dev/null)"; set -e
        if [[ -n "${ip}" ]]; then echo "${ip}"; return 0; fi
        sleep 2
    done
    echo "Timed out waiting for VM IP address." >&2; return 1
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
                if [[ "${cwd}" != "${TARGET_DIR}" ]]; then
                    printf '%s\t%s\n' "${pid}" "${cwd}"
                fi ;;
        esac
    done
}

_validate_qnx_binary() {
    local bin="$1"
    local description
    if ! description="$(file "${bin}")"; then
        echo "Failed to inspect binary: ${bin}" >&2; return 1
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
            echo "${SAFETY_BIN_DIR}/${name}" ;;
        safe_edge_cloud_gateway|safe_edge_ota_service|safe_edge_infotainment)
            echo "${NON_SAFETY_BIN_DIR}/${name}" ;;
        *)
            echo "Unknown vehicle node binary: ${name}" >&2; return 1 ;;
    esac
}

_validate_vehicle_binaries() {
    local name bin
    for name in "${VEHICLE_NODE_BINS[@]}"; do
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
        if [[ "${TEST_PLATFORM}" == "linux" ]]; then
            _validate_linux_binary "${bin}"
        else
            _validate_qnx_binary "${bin}"
        fi
    done
}

_refresh_vehicle_system_files_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/system_files.custom"
    local name bin
    mkdir -p "$(dirname "${snippet}")"
    {
        echo "# local/snippets/system_files.custom"
        echo "# Generated by scripts/launch_tpi_3_3_test.sh"
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
# Generated by scripts/launch_tpi_3_3_test.sh
EOF
}

_refresh_vehicle_post_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/post_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/post_start.custom
# Generated by scripts/launch_tpi_3_3_test.sh
route add -net 224.0.0.0/4 vtnet0
EOF
}

_reset_generated_target_output() {
    rm -rf "${TARGET_DIR}/output"
}

_prepare_local_target_dirs() {
    if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
        mkdir -p "${TARGET_DIR}/local/misc_files" "${TARGET_DIR}/local/snippets"
    else
        mkdir -p "${RUNTIME_DIR}"
    fi
}

_escape_single_quotes() {
    local value="$1"
    printf "%s" "${value//\'/\'\\\'\'}"
}

_remote_file_contains() {
    local ip="$1" file="$2" pattern="$3"
    local ef ep
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        grep -Fq -- "${pattern}" "${file}" >/dev/null 2>&1
    else
        ef="$(_escape_single_quotes "${file}")"
        ep="$(_escape_single_quotes "${pattern}")"
        _ssh_run "${ip}" "grep -Fq -- '${ep}' '${ef}'" >/dev/null 2>&1
    fi
}

_wait_for_remote_file_contains() {
    local ip="$1" file="$2" pattern="$3"
    local max_tries="${4:-8}" sleep_sec="${5:-1}" i
    for ((i = 1; i <= max_tries; i++)); do
        if _remote_file_contains "${ip}" "${file}" "${pattern}"; then return 0; fi
        sleep "${sleep_sec}"
    done
    return 1
}

_start_fast_stack() {
    local host_ip="$1"
    local guest_ip="$2"
    echo "Starting mixed traffic stack (server + edge, host_ip=${host_ip} guest_ip=${guest_ip})..."
    rm -f /dev/shm/fastdds_* /dev/shm/sem.fastdds_* 2>/dev/null || true

    docker rm -f "${SERVER_CONTAINER}" 2>/dev/null || true
    docker rm -f "${EDGE_CONTAINER}" 2>/dev/null || true

    docker run -d \
        --name "${SERVER_CONTAINER}" \
        --network host \
        -e "SAFE_EDGE_OWN_IP=${host_ip}" \
        -e "SAFE_EDGE_NON_SAFETY_IP=${guest_ip}" \
        -e "SAFE_EDGE_INITIAL_PEERS=${guest_ip}:8011,${host_ip}:8030" \
        safe-edge-server:fast \
        >/dev/null
    echo "  [server] container started: ${SERVER_CONTAINER}"

    docker run -d \
        --name "${EDGE_CONTAINER}" \
        --network host \
        -e "SAFE_EDGE_OWN_IP=${host_ip}" \
        -e "SAFE_EDGE_SAFETY_IP=${guest_ip}" \
        -e "SAFE_EDGE_NON_SAFETY_IP=${guest_ip}" \
        -e "SAFE_EDGE_INITIAL_PEERS=${guest_ip}:8001,${guest_ip}:8002,${guest_ip}:8011,${host_ip}:8020" \
        safe-edge-edge:fast \
        >/dev/null
    echo "  [edge] container started: ${EDGE_CONTAINER}"
}

_wait_fast_stack() {
    local max_tries=20 i
    echo "Waiting for server container to be running..."
    for ((i = 1; i <= max_tries; i++)); do
        if docker inspect -f '{{.State.Running}}' "${SERVER_CONTAINER}" 2>/dev/null | grep -q true; then
            echo "  [server] running"
            break
        fi
        sleep 1
    done
    if (( i > max_tries )); then
        echo "WARNING: server container did not reach running state" >&2
    fi

    echo "Waiting for edge container to be running..."
    for ((i = 1; i <= max_tries; i++)); do
        if docker inspect -f '{{.State.Running}}' "${EDGE_CONTAINER}" 2>/dev/null | grep -q true; then
            echo "  [edge] running"
            break
        fi
        sleep 1
    done
    if (( i > max_tries )); then
        echo "WARNING: edge container did not reach running state" >&2
    fi
}

_stop_fast_stack() {
    docker stop "${SERVER_CONTAINER}" 2>/dev/null || true
    docker rm -f "${SERVER_CONTAINER}" 2>/dev/null || true
    docker stop "${EDGE_CONTAINER}" 2>/dev/null || true
    docker rm -f "${EDGE_CONTAINER}" 2>/dev/null || true
}

_check_node_liveness() {
    local ip="$1"
    local rc=0
    local name
    local safety_nodes=(safe_edge_policy_engine safe_edge_safety_io_adapters safe_edge_vehicle_mock)
    echo "--- Node liveness ---"
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        for name in "${safety_nodes[@]}"; do
            local pid_file="${RUNTIME_DIR}/${name}.pid"
            if [[ ! -f "${pid_file}" ]]; then
                echo "  [FAIL] ${name}: pid file missing" >&2; rc=1; continue
            fi
            local pid; pid="$(cat "${pid_file}")"
            if kill -0 "${pid}" 2>/dev/null; then
                echo "  [OK]   ${name}: pid=${pid} alive"
            else
                echo "  [FAIL] ${name}: pid=${pid} dead (crashed during test)" >&2; rc=1
            fi
        done
    else
        for name in "${safety_nodes[@]}"; do
            local remote_pid_file="/tmp/safe_edge_vehicle_nodes/${name}.pid"
            if _ssh_run "${ip}" "[ -f '${remote_pid_file}' ]" 2>/dev/null; then
                local pid; pid="$(_ssh_run "${ip}" "cat '${remote_pid_file}'" 2>/dev/null)"
                if _ssh_run "${ip}" "kill -0 ${pid} 2>/dev/null" 2>/dev/null; then
                    echo "  [OK]   ${name}: pid=${pid} alive"
                else
                    echo "  [FAIL] ${name}: pid=${pid} dead (crashed)" >&2; rc=1
                fi
            else
                echo "  [FAIL] ${name}: pid file missing on VM" >&2; rc=1
            fi
        done
    fi
    return "${rc}"
}

_stop_all() {
    _stop_vehicle_nodes "${VM_IP:-}"
    _stop_fast_stack
    if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
        mkqnximage --stop 2>/dev/null || true
    fi
}

_start_vehicle_nodes() {
    local ip="$1"
    local cmd name
    local input_escaped
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        local safety_env non_safety_env
        safety_env=(
            "SAFE_EDGE_OWN_IP=${PEER_IP}"
            "SAFE_EDGE_CROSS_DOMAIN_IP=${PEER_IP}"
            "SAFE_EDGE_HOST_IP=${PEER_IP}"
            "SAFE_EDGE_INITIAL_PEERS=${PEER_IP}:8001,${PEER_IP}:8002,${PEER_IP}:8011,${PEER_IP}:8020,${PEER_IP}:8030"
        )
        non_safety_env=(
            "SAFE_EDGE_OWN_IP=${PEER_IP}"
            "SAFE_EDGE_CROSS_DOMAIN_IP=${PEER_IP}"
            "SAFE_EDGE_HOST_IP=${PEER_IP}"
            "SAFE_EDGE_INITIAL_PEERS=${PEER_IP}:8001,${PEER_IP}:8002,${PEER_IP}:8011,${PEER_IP}:8020,${PEER_IP}:8030"
        )
        mkdir -p "${RUNTIME_DIR}"
        printf '%s' "${INPUT_FILE_CONTENT}" > "${INPUT_FILE}"
        rm -f "${RUNTIME_DIR}"/*.pid "${RUNTIME_DIR}"/*.log
        env "${non_safety_env[@]}" "${NON_SAFETY_BIN_DIR}/safe_edge_infotainment" > "${RUNTIME_DIR}/safe_edge_infotainment.log" 2>&1 &
        echo $! > "${RUNTIME_DIR}/safe_edge_infotainment.pid"
        sleep 2
        for name in "${VEHICLE_NODE_BINS[@]}"; do
            [[ "${name}" == "safe_edge_infotainment" ]] && continue
            case "${name}" in
                safe_edge_safety_io_adapters|safe_edge_policy_engine)
                    env "${safety_env[@]}" "$(_host_bin_path "${name}")" > "${RUNTIME_DIR}/${name}.log" 2>&1 & ;;
                safe_edge_vehicle_mock)
                    env "${safety_env[@]}" "SAFE_EDGE_INPUT_FILE=${INPUT_FILE}" "${SAFETY_BIN_DIR}/${name}" > "${RUNTIME_DIR}/${name}.log" 2>&1 & ;;
                *)
                    env "${non_safety_env[@]}" "$(_host_bin_path "${name}")" > "${RUNTIME_DIR}/${name}.log" 2>&1 & ;;
            esac
            echo $! > "${RUNTIME_DIR}/${name}.pid"
        done
        return 0
    fi

    input_escaped="$(_escape_single_quotes "${INPUT_FILE_CONTENT}")"
    local _peers="${ip}:8001,${ip}:8002,${ip}:8011,${BRIDGE_IP}:8020,${BRIDGE_IP}:8030"
    local _qnx_env="SAFE_EDGE_OWN_IP=${ip} SAFE_EDGE_HOST_IP=${BRIDGE_IP} SAFE_EDGE_CROSS_DOMAIN_IP=${BRIDGE_IP} SAFE_EDGE_INITIAL_PEERS=${_peers}"

    cmd="set -e; mkdir -p /tmp/safe_edge_vehicle_nodes /data/safe-edge-stage2;"
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

_start_load() {
    local ip="$1" level="$2"

    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        local count=0
        if [[ "${level}" -ge 1 ]]; then
            bash -lc 'while true; do awk "BEGIN{for(i=0;i<2000000;i++){x=sin(i)+cos(i)}}" >/dev/null; sleep 0.005; done' >/dev/null 2>&1 &
            echo $! > "${RUNTIME_DIR}/load_$((++count)).pid"
        fi
        if [[ "${level}" -ge 2 ]]; then
            bash -lc 'while true; do awk "BEGIN{for(i=0;i<50000;i++){x=sin(i)+cos(i)}}" >/dev/null; sleep 0.3; done' >/dev/null 2>&1 &
            echo $! > "${RUNTIME_DIR}/load_$((++count)).pid"
        fi
        return 0
    fi

    local _warmer="on -d -p 1:r awk 'BEGIN{x=1.0;while(1){x=sin(x)+cos(x)}}'"
    local _duty1="on -d -p 10:r sh -c 'while true; do awk \"BEGIN{for(i=0;i<2000000;i++){x=sin(i)+cos(i)}}\"; sleep 0.005; done'"
    local _duty2_light="on -d -p 10:r sh -c 'while true; do awk \"BEGIN{for(i=0;i<50000;i++){x=sin(i)+cos(i)}}\"; sleep 0.3; done'"

    local cmd="${_warmer} </dev/null >/dev/null 2>&1 &"
    if [[ "${level}" -ge 1 ]]; then
        cmd+=" ${_duty1} </dev/null >/dev/null 2>&1 &"
    fi
    if [[ "${level}" -ge 2 ]]; then
        cmd+=" ${_duty2_light} </dev/null >/dev/null 2>&1 &"
    fi

    _ssh_run "${ip}" "${cmd}"
}

_stop_vehicle_nodes() {
    local ip="$1"
    local pid_file
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        for pid_file in "${RUNTIME_DIR}"/*.pid; do
            [[ -f "${pid_file}" ]] || continue
            kill "$(cat "${pid_file}")" 2>/dev/null || true
        done
        return 0
    fi
    _ssh_run "${ip}" \
        "for f in /tmp/safe_edge_vehicle_nodes/*.pid; do [ -f \"\$f\" ] || continue; pid=\$(cat \"\$f\"); kill \"\$pid\" 2>/dev/null || true; done" \
        >/dev/null 2>&1 || true
    _ssh_run "${ip}" "killall dd 2>/dev/null || true" >/dev/null 2>&1 || true
    _ssh_run "${ip}" "kill \$(ps -e | grep 'sh' | awk '{print \$1}' | grep -v \$\$) 2>/dev/null || true" >/dev/null 2>&1 || true
}

_trigger_sample_cycle() {
    local ip="$1"
    local esc1 esc0
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        printf '%s' "${INPUT_FILE_CONTENT/emergency_stop=0/emergency_stop=1}" > "${INPUT_FILE}"
        sleep 0.25
        printf '%s' "${INPUT_FILE_CONTENT}" > "${INPUT_FILE}"
        return 0
    fi
    esc1="$(_escape_single_quotes "${INPUT_FILE_CONTENT/emergency_stop=0/emergency_stop=1}")"
    esc0="$(_escape_single_quotes "${INPUT_FILE_CONTENT}")"
    _ssh_run "${ip}" "printf '${esc1}' > /data/safe-edge-stage2/input.txt; sleep 0.25; printf '${esc0}' > /data/safe-edge-stage2/input.txt"
}

_wait_for_stable_nominal() {
    local ip="$1" pe_log="$2"
    local required=5 count=0 i bad t
    echo "Waiting for ${required}s of stable NOMINAL mode..."
    for ((i = 1; i <= 90; i++)); do
        bad=0
        # Extract individual mode values to handle concatenated log lines
        if [[ "${TEST_PLATFORM}" == "linux" ]]; then
            t="$(grep 'Published PolicyDecision' "${pe_log}" 2>/dev/null | grep -oE 'mode=[0-9]+' | tail -20)"
            if [[ -n "${t}" ]]; then
                echo "${t}" | grep -qv 'mode=1' && bad=1 || true
            else
                bad=1
            fi
        else
            if _ssh_run "${ip}" \
                "t=\$(grep 'Published PolicyDecision' '${pe_log}' 2>/dev/null | grep -oE 'mode=[0-9]+' | tail -20); [ -n \"\$t\" ] && ! echo \"\$t\" | grep -qv 'mode=1'" \
                2>/dev/null; then
                bad=0
            else
                bad=1
            fi
        fi
        if [[ "${bad}" -eq 0 ]]; then
            (( count++ )) || true
            echo "  NOMINAL stable ${count}/${required}s..."
        else
            [[ "${count}" -gt 0 ]] && echo "  Non-nominal detected — resetting counter"
            count=0
        fi
        if (( count >= required )); then
            echo "System stable in NOMINAL for ${required}s."
            return 0
        fi
        sleep 1
    done
    echo "WARNING: system did not reach ${required}s stable NOMINAL within 90s — proceeding anyway" >&2
    return 0
}

_wait_for_nodes_ready() {
    local ip="$1"
    local pe_log
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        pe_log="${RUNTIME_DIR}/safe_edge_policy_engine.log"
    else
        pe_log="/tmp/safe_edge_vehicle_nodes/safe_edge_policy_engine.log"
    fi
    echo "Waiting for nodes to initialize..."
    _wait_for_remote_file_contains "${ip}" "${pe_log}" "Published ServiceHeartbeat" 60 1 || {
        echo "policy_engine did not publish ServiceHeartbeat within 60 s — nodes may have crashed." >&2
        _dump_node_diagnostics "${ip}"
        return 1
    }
    _wait_for_stable_nominal "${ip}" "${pe_log}"
}

_run_measurement_loop() {
    local ip="$1" label="$2"
    local i
    echo "Starting ${OPT_SAMPLES} measurement samples [${label}] (mixed traffic active)..."
    for ((i = 1; i <= OPT_SAMPLES; i++)); do
        _trigger_sample_cycle "${ip}"
        if (( i % 10 == 0 )); then echo "  ${i}/${OPT_SAMPLES} samples collected"; fi
    done
    echo "Measurement loop complete [${label}]."
}

_collect_logs() {
    local ip="$1" dest_raw_log="$2"
    local name tmp

    echo "Collecting logs → ${dest_raw_log}..."
    : > "${dest_raw_log}"
    for name in "${VEHICLE_NODE_BINS[@]}"; do
        if [[ "${TEST_PLATFORM}" == "linux" ]]; then
            if [[ -f "${RUNTIME_DIR}/${name}.log" ]]; then
                cat "${RUNTIME_DIR}/${name}.log" >> "${dest_raw_log}"
            else
                echo "[${name}] (log not found on host)" >> "${dest_raw_log}"
            fi
        else
            tmp="$(mktemp)"
            if _scp_get "${ip}" "/tmp/safe_edge_vehicle_nodes/${name}.log" "${tmp}" 2>/dev/null; then
                cat "${tmp}" >> "${dest_raw_log}"
            else
                echo "[${name}] (log not found on VM)" >> "${dest_raw_log}"
            fi
            rm -f "${tmp}"
        fi
    done

    docker logs "${SERVER_CONTAINER}" > "${RUNTIME_DIR}/server.log" 2>&1 || true
    docker logs "${EDGE_CONTAINER}"   > "${RUNTIME_DIR}/edge.log"   2>&1 || true

    echo "Server log: ${RUNTIME_DIR}/server.log"
    echo "Edge log: ${RUNTIME_DIR}/edge.log"
}

_clear_node_logs() {
    local ip="$1" name
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        for name in "${VEHICLE_NODE_BINS[@]}"; do
            [[ -f "${RUNTIME_DIR}/${name}.log" ]] && : > "${RUNTIME_DIR}/${name}.log" || true
        done
    else
        _ssh_run "${ip}" \
            "for f in /tmp/safe_edge_vehicle_nodes/*.log; do [ -f \"\$f\" ] && : > \"\$f\" || true; done" \
            >/dev/null 2>&1 || true
    fi
}

_dump_node_diagnostics() {
    local ip="$1"
    local name
    echo "--- node diagnostics ---" >&2
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        for name in "${VEHICLE_NODE_BINS[@]}"; do
            local pid_file="${RUNTIME_DIR}/${name}.pid"
            [[ -f "${pid_file}" ]] || continue
            local pid
            pid="$(cat "${pid_file}")"
            if kill -0 "${pid}" 2>/dev/null; then
                echo "${name}: pid=${pid} [running]" >&2
            else
                echo "${name}: pid=${pid} [dead]" >&2
            fi
        done
    else
        { _ssh_run "${ip}" \
            "for f in /tmp/safe_edge_vehicle_nodes/*.pid; do [ -f \"\$f\" ] || continue; n=\${f##*/}; n=\${n%.pid}; pid=\$(cat \"\$f\"); alive=dead; kill -0 \"\$pid\" 2>/dev/null && alive=running; echo \"\$n: pid=\$pid [\$alive]\"; done" \
            2>/dev/null || true; } >&2
    fi
    for name in "${VEHICLE_NODE_BINS[@]}"; do
        echo "--- ${name}.log (tail 10) ---" >&2
        if [[ "${TEST_PLATFORM}" == "linux" ]]; then
            { tail -10 "${RUNTIME_DIR}/${name}.log" 2>/dev/null || echo '(empty)'; } >&2
        else
            { _ssh_run "${ip}" "tail -10 /tmp/safe_edge_vehicle_nodes/${name}.log 2>/dev/null || echo '(empty)'" || true; } >&2
        fi
    done
}

_check_mixed_traffic_informative() {
    local rc=0
    echo "--- Mixed traffic checks (informative) ---"
    if docker inspect -f '{{.State.Running}}' "${SERVER_CONTAINER}" 2>/dev/null | grep -q true; then
        echo "  [server] container active: OK"
    else
        echo "  WARNING: server container not running at end of measurement" >&2
        rc=1
    fi
    if docker inspect -f '{{.State.Running}}' "${EDGE_CONTAINER}" 2>/dev/null | grep -q true; then
        echo "  [edge] container active: OK"
    else
        echo "  WARNING: edge container not running at end of measurement" >&2
        rc=1
    fi
    if [[ -s "${RUNTIME_DIR}/server.log" ]]; then
        echo "  [server] log has content: OK"
    else
        echo "  WARNING: server log is empty" >&2
    fi
    if [[ -s "${RUNTIME_DIR}/edge.log" ]]; then
        echo "  [edge] log has content: OK"
    else
        echo "  WARNING: edge log is empty" >&2
    fi
    return 0
}

_generate_report() {
    local baseline_log="$1" loaded_log="$2"
    local environment_label
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        environment_label="Linux native (mixed traffic: server+edge active)"
    else
        environment_label="QNX 8.0 / QEMU x86_64 (mixed traffic: server+edge active)"
    fi
    python3 - "${baseline_log}" "${loaded_log}" "${REPORT}" "${OPT_SAMPLES}" \
              "${E2E_THRESHOLD_MS}" "${REACTION_THRESHOLD_MS}" "${environment_label}" "${OPT_LOAD}" <<'PYEOF'
import sys
import re
from collections import Counter

baseline_log = sys.argv[1]
loaded_log   = sys.argv[2]
report_path  = sys.argv[3]
n_samples    = int(sys.argv[4])
e2e_limit    = float(sys.argv[5])
rxn_limit    = float(sys.argv[6])
environment  = sys.argv[7]
load_level   = int(sys.argv[8])

vm_pub_re = re.compile(
    r'\[vehicle_mock\] Published SafetyInputFrame'
    r'.*t_pub=(\d+)\.(\d+).*emergency_stop=1')
vm_dec_re = re.compile(
    r'\[vehicle_mock\] Received PolicyDecision t_rx_dec=(\d+)\.(\d+)')
pe_rx_re = re.compile(
    r'\[policy_engine\] Received SafetyInputFrame'
    r'.*t_rx=(\d+)\.(\d+).*emergency_stop=1')
pe_dec_re = re.compile(
    r'\[policy_engine\] Published PolicyDecision.*t_dec=(\d+)\.(\d+)')
mode_re = re.compile(
    r'\[policy_engine\] Published PolicyDecision.*\bmode=(\d+)\b')

def parse_ts(s, ns):
    return int(s) * 1_000_000_000 + int(ns)

def ns_to_ms(ns):
    return ns / 1_000_000.0

LOG_ENTRY_RE = re.compile(r'(?=\[[a-z_]+ ?[a-z_]*\] )')

def split_entries(raw_lines):
    """Split concatenated log entries that share a single physical line."""
    out = []
    for raw in raw_lines:
        parts = LOG_ENTRY_RE.split(raw)
        out.extend(p for p in parts if p.strip())
    return out

def parse_log(path):
    if not path:
        return [], [], [], []
    try:
        with open(path) as f:
            lines = split_entries(f.readlines())
    except OSError:
        return [], [], [], []
    e2e, rxn = [], []
    i = 0
    last_dec_j = -1
    first_pub_idx = None
    while i < len(lines):
        pub_m = vm_pub_re.search(lines[i])
        if pub_m:
            if first_pub_idx is None:
                first_pub_idx = i
            t_pub = parse_ts(pub_m.group(1), pub_m.group(2))
            j = max(i + 1, last_dec_j + 1)
            dec_m = None
            while j < len(lines):
                dec_m = vm_dec_re.search(lines[j])
                if dec_m:
                    break
                j += 1
            if dec_m is not None:
                e2e.append(ns_to_ms(parse_ts(dec_m.group(1), dec_m.group(2)) - t_pub))
                last_dec_j = j
            i += 1
        else:
            rx_m = pe_rx_re.search(lines[i])
            if rx_m:
                t_rx = parse_ts(rx_m.group(1), rx_m.group(2))
                k = i + 1
                pdec_m = None
                while k < len(lines):
                    pdec_m = pe_dec_re.search(lines[k])
                    if pdec_m:
                        break
                    k += 1
                if pdec_m is not None:
                    rxn.append(ns_to_ms(parse_ts(pdec_m.group(1), pdec_m.group(2)) - t_rx))
                i = k + 1
            else:
                i += 1
    bad, bad_lines = [], []
    for line in (lines[first_pub_idx:] if first_pub_idx is not None else []):
        m = mode_re.search(line)
        if m and m.group(1) != '1':
            bad.append(int(m.group(1)))
            bad_lines.append(line.rstrip())
    return e2e, rxn, bad, bad_lines

def percentile(data, p):
    if not data:
        return float('nan')
    data_s = sorted(data)
    idx = (len(data_s) - 1) * p / 100.0
    lo = int(idx)
    hi = lo + 1
    if hi >= len(data_s):
        return data_s[lo]
    return data_s[lo] + (data_s[hi] - data_s[lo]) * (idx - lo)

def stats(data):
    if not data:
        return {k: float('nan') for k in ('min','p50','p90','p95','p99','max')}
    return {
        'min': min(data), 'p50': percentile(data, 50),
        'p90': percentile(data, 90), 'p95': percentile(data, 95),
        'p99': percentile(data, 99), 'max': max(data),
    }

def run_tests(e2e_s, rxn_s, bad_modes, bad_lines):
    e2e_st = stats(e2e_s)
    rxn_st = stats(rxn_s)
    results = []

    passed = len(e2e_s) >= n_samples
    detail = [f"    Requested: {n_samples}  Captured E2E: {len(e2e_s)}  Reaction: {len(rxn_s)}"]
    results.append(("SampleCompleteness", passed, detail))

    passed = len(bad_modes) == 0
    if passed:
        detail = []
    else:
        counts = Counter(bad_modes)
        detail = [f"    Unexpected modes: {', '.join(f'mode={k}({v}x)' for k,v in sorted(counts.items()))}"]
        for ln in bad_lines[:5]:
            detail.append(f"    {ln}")
    results.append(("ModeCoherence", passed, detail))

    passed = not (e2e_st['p99'] != e2e_st['p99']) and e2e_st['p99'] < e2e_limit
    detail = [
        f"    KPI: P99 < {e2e_limit:.0f} ms  →  P99 = {e2e_st['p99']:.2f} ms",
        f"    min={e2e_st['min']:.2f}  P50={e2e_st['p50']:.2f}  P90={e2e_st['p90']:.2f}"
        f"  P95={e2e_st['p95']:.2f}  P99={e2e_st['p99']:.2f}  [ms]",
    ]
    results.append(("E2ELatency_P99", passed, detail))

    passed = not (rxn_st['p99'] != rxn_st['p99']) and rxn_st['p99'] < rxn_limit
    detail = [
        f"    KPI: P99 < {rxn_limit:.0f} ms  →  P99 = {rxn_st['p99']:.2f} ms",
        f"    min={rxn_st['min']:.2f}  P50={rxn_st['p50']:.2f}  P90={rxn_st['p90']:.2f}"
        f"  P95={rxn_st['p95']:.2f}  P99={rxn_st['p99']:.2f}  [ms]",
    ]
    results.append(("ReactionTime_P99", passed, detail))

    return results

def fmt_suite(suite_name, tests):
    out = []
    out.append(f"[----------] {len(tests)} tests from {suite_name}")
    for name, passed, detail in tests:
        out.append(f"[ RUN      ] {suite_name}.{name}")
        for d in detail:
            out.append(d)
        out.append(f"{'[       OK ]' if passed else '[  FAILED  ]'} {suite_name}.{name}")
    out.append(f"[----------] {len(tests)} tests from {suite_name}")
    return out

b_e2e, b_rxn, b_bad, b_bad_lines = parse_log(baseline_log)
l_e2e, l_rxn, l_bad, l_bad_lines = parse_log(loaded_log if loaded_log else "")
has_loaded = load_level > 0 and bool(loaded_log)

b_tests = run_tests(b_e2e, b_rxn, b_bad, b_bad_lines)
l_tests = run_tests(l_e2e, l_rxn, l_bad, l_bad_lines) if has_loaded else []

all_suites = [("TPI_3_3_Nominal", b_tests)]
if has_loaded:
    all_suites.append((f"TPI_3_3_Load{load_level}", l_tests))

total        = sum(len(t) for _, t in all_suites)
passed_count = sum(1 for _, t in all_suites for _, p, _ in t if p)
failed_count = total - passed_count
failed_names = [f"  {s}.{n}" for s, t in all_suites for n, p, _ in t if not p]

out = []
out.append(f"SafeEDGE TPI 3.3 — Mixed Traffic and Concurrent Workload")
out.append(f"Environment: {environment}")
out.append("")
out.append(f"[==========] {total} tests from {len(all_suites)} test suite(s).")
out.append("[----------] Global test environment set-up.")
out.append("")

for suite_name, tests in all_suites:
    out.extend(fmt_suite(suite_name, tests))
    out.append("")

out.append(f"[==========] {total} tests from {len(all_suites)} test suite(s) ran.")
out.append(f"[  PASSED  ] {passed_count} test(s).")
if failed_count:
    out.append(f"[  FAILED  ] {failed_count} test(s).")
    out.extend(failed_names)

text = "\n".join(out)
print(text)
with open(report_path, 'w') as f:
    f.write(text + "\n")

sys.exit(1 if failed_count else 0)
PYEOF
}

# ─── Main ───────────────────────────────────────────────────────────────────

_prepare_local_target_dirs

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
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
fi

if [[ "${TEST_PLATFORM}" == "qnx" && ! -f "${QNX_SDP_ROOT}/qnxsdp-env.sh" ]]; then
    echo "QNX SDK not found at QNX_SDP_ROOT='${QNX_SDP_ROOT}'" >&2; exit 1
fi
if [[ "${TEST_PLATFORM}" == "qnx" && ! -d "${TARGET_DIR}" ]]; then
    echo "QNX target directory not found: ${TARGET_DIR}" >&2; exit 1
fi
if [[ "${TEST_PLATFORM}" == "qnx" ]] && ! command -v sshpass >/dev/null 2>&1; then
    echo "sshpass not found. Install with: sudo apt install sshpass" >&2; exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not found on host — required for post-processing" >&2; exit 1
fi
if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found — required for mixed traffic stack" >&2; exit 1
fi
if ! docker image inspect safe-edge-server:fast >/dev/null 2>&1; then
    echo "Image safe-edge-server:fast not found. Build with: bash scripts/build_ubuntu.sh" >&2; exit 1
fi
if ! docker image inspect safe-edge-edge:fast >/dev/null 2>&1; then
    echo "Image safe-edge-edge:fast not found. Build with: bash scripts/build_ubuntu.sh" >&2; exit 1
fi

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    # shellcheck source=/dev/null
    source "${QNX_SDP_ROOT}/qnxsdp-env.sh" >/dev/null 2>&1
fi

if [[ "${TEST_PLATFORM}" == "qnx" ]] && ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "qemu-system-x86_64 not found. Install with: sudo apt install qemu-system-x86" >&2; exit 1
fi
if [[ "${TEST_PLATFORM}" == "qnx" ]] && ! command -v brctl >/dev/null 2>&1; then
    echo "brctl not found. Install with: sudo apt install bridge-utils" >&2; exit 1
fi

VM_IP=""
PEER_IP=""
BRIDGE_IP=""
trap '_stop_all' EXIT

if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
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
else
    if [[ "${OPT_STOP}" -eq 1 ]]; then
        echo "Stopping local Linux node processes..."
        _stop_vehicle_nodes ""
        echo "Processes stopped."
        exit 0
    fi
    _validate_vehicle_binaries
    echo "Running native Linux mode. No QNX image will be built."
fi

if [[ "${TEST_PLATFORM}" == "linux" ]]; then
    PEER_IP="$(_get_linux_host_ip)"
    : "${PEER_IP:=127.0.0.1}"
    BRIDGE_IP="${PEER_IP}"
else
    BRIDGE_IP="$(ip -o -f inet addr show virbr0 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)"
    : "${BRIDGE_IP:=192.168.122.1}"
fi
GUEST_IP="${VM_IP:-${BRIDGE_IP}}"
echo "DDS IPs: host/bridge=${BRIDGE_IP} guest=${GUEST_IP}"

_start_fast_stack "${BRIDGE_IP}" "${GUEST_IP}"
_wait_fast_stack

echo "Starting vehicle nodes..."
_start_vehicle_nodes "${VM_IP}"
echo "Vehicle nodes launched."

if [[ "${OPT_PRIO}" -eq 1 ]]; then
    if [[ "${OPT_LOAD}" -gt 0 ]]; then
        echo "Starting load level ${OPT_LOAD}..."
        _start_load "${VM_IP}" "${OPT_LOAD}"
        echo "Load stressors launched."
    fi
    echo "Waiting for policy_engine heartbeat (up to 60s)..."
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        NODE_LOG_DIR="${RUNTIME_DIR}"
    else
        NODE_LOG_DIR="/tmp/safe_edge_vehicle_nodes"
    fi
    if ! _wait_for_remote_file_contains "${VM_IP}" \
            "${NODE_LOG_DIR}/safe_edge_policy_engine.log" \
            "Published ServiceHeartbeat" \
            60 1; then
        echo "WARNING: heartbeat not seen after 60s — querying anyway" >&2
    else
        echo "Nodes up. Querying priorities..."
    fi
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        echo ""
        echo "=== local processes ==="
        ps -eo pid,ppid,pri,ni,cls,rtprio,comm | grep -E 'safe_edge|awk' || true
    else
        echo ""
        echo "=== pidin header (raw) ==="
        _ssh_run "${VM_IP}" "pidin 2>&1 | head -3" 2>/dev/null || true
        echo ""
        echo "=== all processes (pidin, first 40 lines) ==="
        _ssh_run "${VM_IP}" "pidin 2>&1 | head -40" 2>/dev/null || true
        echo ""
        echo "=== safe_edge grep ==="
        _ssh_run "${VM_IP}" "pidin 2>&1 | grep -i safe" 2>/dev/null || true
        echo ""
        echo "=== stressor grep ==="
        _ssh_run "${VM_IP}" "pidin 2>&1 | grep -iE 'awk|nc '" 2>/dev/null || true
        echo ""
        echo "Stopping VM..."
        mkqnximage --stop 2>/dev/null || true
    fi
    exit 0
fi

TEST_RC=0

_wait_for_nodes_ready "${VM_IP}" || TEST_RC=1

if [[ "${TEST_RC}" -eq 0 ]]; then
    echo "=== Phase 1: Nominal (no load stressors) ==="
    _run_measurement_loop "${VM_IP}" "nominal"
    _collect_logs "${VM_IP}" "${BASELINE_RAW_LOG}"

    if [[ "${OPT_LOAD}" -gt 0 ]]; then
        echo "=== Phase 2: Under load level ${OPT_LOAD} ==="
        _clear_node_logs "${VM_IP}"
        echo "Starting load level ${OPT_LOAD}..."
        _start_load "${VM_IP}" "${OPT_LOAD}"
        echo "Load stressors active."
        _run_measurement_loop "${VM_IP}" "load=${OPT_LOAD}"
        _collect_logs "${VM_IP}" "${LOADED_RAW_LOG}"
    fi
fi

_check_mixed_traffic_informative
_check_node_liveness "${VM_IP}" || TEST_RC=1

_stop_vehicle_nodes "${VM_IP}"
_stop_fast_stack
if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    echo "Stopping QNX VM..."
    mkqnximage --stop 2>/dev/null || true
fi

echo "Generating latency report..."
if [[ "${OPT_LOAD}" -gt 0 ]]; then
    _generate_report "${BASELINE_RAW_LOG}" "${LOADED_RAW_LOG}" || TEST_RC=1
else
    _generate_report "${BASELINE_RAW_LOG}" "" || TEST_RC=1
fi
echo "Report: ${REPORT}"

exit "${TEST_RC}"
