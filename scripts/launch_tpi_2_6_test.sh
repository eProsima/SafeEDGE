#!/usr/bin/env bash
# Run TPI 2.6 safety communication path latency benchmark for SafeDDS nodes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${SCRIPT_DIR}/logs"
RAW_LOG="${LOG_DIR}/tpi_2_6_raw.log"
REPORT="${LOG_DIR}/tpi_2_6_report.txt"

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
OPT_LOAD=0
OPT_PRIO=0
OPT_LINUX=0

TEST_PLATFORM="qnx"
RUNTIME_DIR="${LOG_DIR}/tpi_2_6_runtime"
INPUT_FILE="${RUNTIME_DIR}/input.txt"

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
Usage: bash scripts/launch_tpi_2_6_test.sh [options]

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

mkdir -p "${LOG_DIR}"

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
        echo "# Generated by scripts/launch_tpi_2_6_test.sh"
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
# Generated by scripts/launch_tpi_2_6_test.sh
EOF
}

_refresh_vehicle_post_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/post_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/post_start.custom
# Generated by scripts/launch_tpi_2_6_test.sh
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

_start_vehicle_nodes() {
    local ip="$1"
    local cmd name
    local input_escaped
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        mkdir -p "${RUNTIME_DIR}"
        printf '%s' "${INPUT_FILE_CONTENT}" > "${INPUT_FILE}"
        rm -f "${RUNTIME_DIR}"/*.pid "${RUNTIME_DIR}"/*.log
        "${NON_SAFETY_BIN_DIR}/safe_edge_infotainment" > "${RUNTIME_DIR}/safe_edge_infotainment.log" 2>&1 &
        echo $! > "${RUNTIME_DIR}/safe_edge_infotainment.pid"
        sleep 2
        for name in "${VEHICLE_NODE_BINS[@]}"; do
            [[ "${name}" == "safe_edge_infotainment" ]] && continue
            if [[ "${name}" == "safe_edge_vehicle_mock" ]]; then
                SAFE_EDGE_INPUT_FILE="${INPUT_FILE}" "${SAFETY_BIN_DIR}/${name}" > "${RUNTIME_DIR}/${name}.log" 2>&1 &
            else
                "$(_host_bin_path "${name}")" > "${RUNTIME_DIR}/${name}.log" 2>&1 &
            fi
            echo $! > "${RUNTIME_DIR}/${name}.pid"
        done
        return 0
    fi

    input_escaped="$(_escape_single_quotes "${INPUT_FILE_CONTENT}")"

    cmd="set -e; mkdir -p /tmp/safe_edge_vehicle_nodes /data/safe-edge-stage2;"
    cmd+=" printf '${input_escaped}' > /data/safe-edge-stage2/input.txt;"
    cmd+=" rm -f /tmp/safe_edge_vehicle_nodes/*.pid /tmp/safe_edge_vehicle_nodes/*.log;"
    cmd+=" /system/bin/safe_edge_infotainment > /tmp/safe_edge_vehicle_nodes/safe_edge_infotainment.log 2>&1 & echo \$! > /tmp/safe_edge_vehicle_nodes/safe_edge_infotainment.pid;"
    cmd+=" sleep 2;"
    for name in "${VEHICLE_NODE_BINS[@]}"; do
        [[ "${name}" == "safe_edge_infotainment" ]] && continue
        cmd+=" /system/bin/${name} > /tmp/safe_edge_vehicle_nodes/${name}.log 2>&1 & echo \$! > /tmp/safe_edge_vehicle_nodes/${name}.pid;"
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

    # vCPU warmer: always launched at priority 1:r (minimum user priority).
    # Preempted instantly by any SafeDDS thread (10:r) so it never competes.
    # Keeps the QEMU vCPU thread scheduled on a host core at all levels,
    # eliminating the idle-vCPU artifact from baseline measurements.
    # Without this, load 0 shows artificially high latency because the host
    # Linux scheduler deskedules the QEMU vCPU thread between DDS events.
    local _warmer="on -d -p 1:r awk 'BEGIN{x=1.0;while(1){x=sin(x)+cos(x)}}'"

    # Level 1 — 1 duty-cycled burner at 10:r, ~80% duty cycle:
    #   burns 2M FP iterations then sleeps 10ms. High enough duty cycle to
    #   compete with SafeDDS threads most of the time.
    #   Expected impact: latency clearly above baseline.
    # Level 1 — 1 duty-cycled burner at 10:r, ~90% duty cycle (sleep 5ms).
    local _duty1="on -d -p 10:r sh -c 'while true; do awk \"BEGIN{for(i=0;i<2000000;i++){x=sin(i)+cos(i)}}\"; sleep 0.005; done'"

    # Level 2 — level 1 + a gentle second process: few iterations, long sleep.
    #   Just enough to occasionally steal a CPU slice from SafeDDS on top of
    #   the level 1 burner. ~20% duty cycle, minimal additional pressure.
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
    # Kill tracked node pids
    _ssh_run "${ip}" \
        "for f in /tmp/safe_edge_vehicle_nodes/*.pid; do [ -f \"\$f\" ] || continue; pid=\$(cat \"\$f\"); kill \"\$pid\" 2>/dev/null || true; done" \
        >/dev/null 2>&1 || true
    # Kill any load stressor processes (busy loops, dd)
    _ssh_run "${ip}" "killall dd 2>/dev/null || true" >/dev/null 2>&1 || true
    # Kill orphaned shell busy-loops spawned for load (best effort)
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
    # Single SSH call: write trigger, hold for one vehicle_mock tick, reset — avoids 3×handshake overhead
    _ssh_run "${ip}" "printf '${esc1}' > /data/safe-edge-stage2/input.txt; sleep 0.25; printf '${esc0}' > /data/safe-edge-stage2/input.txt"
}

_run_measurement_loop() {
    local ip="$1"
    local pe_log
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        pe_log="${RUNTIME_DIR}/safe_edge_policy_engine.log"
    else
        pe_log="/tmp/safe_edge_vehicle_nodes/safe_edge_policy_engine.log"
    fi
    local i

    echo "Waiting for nodes to initialize..."
    _wait_for_remote_file_contains "${ip}" "${pe_log}" "Published ServiceHeartbeat" 60 1 || {
        echo "policy_engine did not publish ServiceHeartbeat within 60 s — nodes may have crashed." >&2
        _dump_node_diagnostics "${ip}"
        return 1
    }
    echo "Nodes ready. Starting ${OPT_SAMPLES} measurement samples..."

    for ((i = 1; i <= OPT_SAMPLES; i++)); do
        _trigger_sample_cycle "${ip}"
        if (( i % 10 == 0 )); then echo "  ${i}/${OPT_SAMPLES} samples collected"; fi
    done
    echo "Measurement loop complete."
}

_collect_logs() {
    local ip="$1"
    local name tmp

    echo "Collecting logs..."
    : > "${RAW_LOG}"
    for name in "${VEHICLE_NODE_BINS[@]}"; do
        if [[ "${TEST_PLATFORM}" == "linux" ]]; then
            if [[ -f "${RUNTIME_DIR}/${name}.log" ]]; then
                cat "${RUNTIME_DIR}/${name}.log" >> "${RAW_LOG}"
            else
                echo "[${name}] (log not found on host)" >> "${RAW_LOG}"
            fi
        else
            tmp="$(mktemp)"
            if _scp_get "${ip}" "/tmp/safe_edge_vehicle_nodes/${name}.log" "${tmp}" 2>/dev/null; then
                cat "${tmp}" >> "${RAW_LOG}"
            else
                echo "[${name}] (log not found on VM)" >> "${RAW_LOG}"
            fi
            rm -f "${tmp}"
        fi
    done
    echo "Raw log: ${RAW_LOG}"
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

_generate_report() {
    local environment_label
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        environment_label="Linux native"
    else
        environment_label="QNX 8.0 / QEMU x86_64"
    fi
    python3 - "${RAW_LOG}" "${REPORT}" "${OPT_SAMPLES}" "${E2E_THRESHOLD_MS}" "${REACTION_THRESHOLD_MS}" "${environment_label}" <<'PYEOF'
import sys
import re

raw_log     = sys.argv[1]
report_path = sys.argv[2]
n_samples   = int(sys.argv[3])
e2e_limit   = float(sys.argv[4])
rxn_limit   = float(sys.argv[5])
environment = sys.argv[6]

def parse_ts(s, ns):
    return int(s) * 1_000_000_000 + int(ns)

def ns_to_ms(ns):
    return ns / 1_000_000.0

# vehicle_mock: stimulus publish (t_pub, same clock as t_rx_dec)
vm_pub_re = re.compile(
    r'\[vehicle_mock\] Published SafetyInputFrame'
    r'.*t_pub=(\d+)\.(\d+).*emergency_stop=1')

# vehicle_mock: PolicyDecision received (t_rx_dec, same clock as t_pub)
vm_dec_re = re.compile(
    r'\[vehicle_mock\] Received PolicyDecision t_rx_dec=(\d+)\.(\d+)')

# policy_engine: SafetyInputFrame received (t_rx, policy_engine clock)
pe_rx_re = re.compile(
    r'\[policy_engine\] Received SafetyInputFrame'
    r'.*t_rx=(\d+)\.(\d+).*emergency_stop=1')

# policy_engine: PolicyDecision published (t_dec, policy_engine clock)
pe_dec_re = re.compile(
    r'\[policy_engine\] Published PolicyDecision.*t_dec=(\d+)\.(\d+)')

with open(raw_log) as f:
    lines = f.readlines()

e2e_samples = []
rxn_samples = []

i = 0
while i < len(lines):
    pub_m = vm_pub_re.search(lines[i])
    if pub_m:
        t_pub = parse_ts(pub_m.group(1), pub_m.group(2))
        # Find next vehicle_mock PolicyDecision receive → E2E
        j = i + 1
        dec_m = None
        while j < len(lines):
            dec_m = vm_dec_re.search(lines[j])
            if dec_m:
                break
            j += 1
        if dec_m is not None:
            t_rx_dec = parse_ts(dec_m.group(1), dec_m.group(2))
            e2e_samples.append(ns_to_ms(t_rx_dec - t_pub))
        i += 1
    else:
        # Reaction time: policy_engine receive → policy_engine publish
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
                t_dec = parse_ts(pdec_m.group(1), pdec_m.group(2))
                rxn_samples.append(ns_to_ms(t_dec - t_rx))
            i = k + 1
        else:
            i += 1

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
        'min': min(data),
        'p50': percentile(data, 50),
        'p90': percentile(data, 90),
        'p95': percentile(data, 95),
        'p99': percentile(data, 99),
        'max': max(data),
    }

e2e_st = stats(e2e_samples)
rxn_st = stats(rxn_samples)

e2e_pass = (not (e2e_st['p99'] != e2e_st['p99'])) and e2e_st['p99'] < e2e_limit
rxn_pass = (not (rxn_st['p99'] != rxn_st['p99'])) and rxn_st['p99'] < rxn_limit
overall  = e2e_pass and rxn_pass

report = []
report.append("SafeEDGE TPI 2.6 — Safety Path Latency Benchmark")
report.append(f"Environment: {environment}")
report.append(f"Raw log: {raw_log}")
report.append(f"Samples collected: {len(e2e_samples)} E2E, {len(rxn_samples)} reaction (requested: {n_samples})")
report.append("")
report.append("=== E2E Latency (vehicle_mock publish -> vehicle_mock receive PolicyDecision) ===")
report.append(f"Proposal limit: < {e2e_limit:.0f} ms   Derived KPI (P99): < {e2e_limit:.0f} ms")
report.append("")
report.append(f"  {'min':>8}  {'P50':>8}  {'P90':>8}  {'P95':>8}  {'P99':>8}  {'max':>8}")
report.append(f"  {e2e_st['min']:>7.2f}ms  {e2e_st['p50']:>7.2f}ms  {e2e_st['p90']:>7.2f}ms  {e2e_st['p95']:>7.2f}ms  {e2e_st['p99']:>7.2f}ms  {e2e_st['max']:>7.2f}ms")
report.append("")
report.append(f"Result: {'PASS' if e2e_pass else 'FAIL'}")
report.append("")
report.append("=== Policy-Reaction Time (policy_engine receive -> publish PolicyDecision) ===")
report.append(f"Proposal limit: < {rxn_limit:.0f} ms   Derived KPI (P99): < {rxn_limit:.0f} ms")
report.append("")
report.append(f"  {'min':>8}  {'P50':>8}  {'P90':>8}  {'P95':>8}  {'P99':>8}  {'max':>8}")
report.append(f"  {rxn_st['min']:>7.2f}ms  {rxn_st['p50']:>7.2f}ms  {rxn_st['p90']:>7.2f}ms  {rxn_st['p95']:>7.2f}ms  {rxn_st['p99']:>7.2f}ms  {rxn_st['max']:>7.2f}ms")
report.append("")
report.append(f"Result: {'PASS' if rxn_pass else 'FAIL'}")
report.append("")
report.append(f"Overall: {'PASS' if overall else 'FAIL'}")

text = "\n".join(report)
print(text)
with open(report_path, 'w') as f:
    f.write(text + "\n")

sys.exit(0 if overall else 1)
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
    VM_IP=""
    echo "Running native Linux mode. No QNX image will be built."
fi

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
    if [[ "${TEST_PLATFORM}" == "linux" ]]; then
        _stop_vehicle_nodes "${VM_IP}"
    fi
    exit 0
fi

if [[ "${OPT_LOAD}" -gt 0 ]]; then
    echo "Starting load level ${OPT_LOAD}..."
    _start_load "${VM_IP}" "${OPT_LOAD}"
    echo "Load stressors active."
fi

TEST_RC=0
_run_measurement_loop "${VM_IP}" || TEST_RC=1

_collect_logs "${VM_IP}"

_stop_vehicle_nodes "${VM_IP}"
if [[ "${TEST_PLATFORM}" == "qnx" ]]; then
    echo "Stopping QNX VM..."
    mkqnximage --stop 2>/dev/null || true
fi

echo "Generating latency report..."
_generate_report || TEST_RC=1
echo "Report: ${REPORT}"

exit "${TEST_RC}"
