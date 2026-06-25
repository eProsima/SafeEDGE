#!/usr/bin/env bash
# Launch vehicle-side SafeEDGE nodes inside a QNX Hypervisor guest.
#
# Topology:
#   Linux host -> QNX hypervisor host VM -> QNX guest VM
#   Guest IP: 192.168.10.2 (host guest-link: 192.168.10.1)
#
# Usage:
#   bash scripts/launch_hypervisor_nodes.sh [options]
#
# Options:
#   --no-rebuild    skip image rebuild and reuse existing artifacts
#   --no-run        do not auto-start guest nodes; print manual launch commands
#   --stop          stop a running hypervisor host VM and exit
#   -h, --help
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

QNX_USER="${USER:-$(id -un)}"
: "${QNX_SDP_ROOT:=/home/${QNX_USER}/qnx800}"
: "${QNX_ARCH:=x86_64}"
: "${CMAKE_BUILD_TYPE:=Release}"
: "${HYP_GUEST_PRIVATE_KEY_FILE:=${HOME}/.ssh/id_ed25519}"
: "${HYP_GUEST_AUTHORIZED_KEY_FILE:=${HYP_GUEST_PRIVATE_KEY_FILE}.pub}"
: "${HYP_GUEST_STARTUP_PATCH_MODE:=forced}"
: "${HYP_GUEST_STARTUP_FREQ:=1000000000}"

TARGET_DIR="${WORKSPACE_ROOT}/qnx/targets/qemu-qnx800-x86_64-hypervisor"
GUEST_TARGET_DIR="${WORKSPACE_ROOT}/qnx/targets/qvm-safe-edge-qnx800-x86_64"
GUEST_IFS="${GUEST_TARGET_DIR}/output/ifs.bin"
GUEST_DISK="${GUEST_TARGET_DIR}/output/disk-qvm"
: "${SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${NON_SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/non-safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"

_SSH_PASS="root"
_SSH_USER="root"
_SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=30 -o LogLevel=ERROR"
_GUEST_IP="192.168.10.2"
_HOST_GUEST_LOG="/data/var/log/hypervisor/start_guest.log"
_GUEST_CTL="/tmp/ssh-guest-ctl"  # ControlPath socket for persistent guest SSH connection

OPT_NO_REBUILD=0
OPT_NO_RUN=0
OPT_STOP=0
OPT_SMOKE_TEST=0
OPT_STABILITY_TEST=0

# All known vehicle node binaries (used for binary validation).
VEHICLE_NODE_BINS=(
    safe_edge_safety_io_adapters
    safe_edge_policy_engine
    safe_edge_vehicle_mock
    safe_edge_cloud_gateway
    safe_edge_ota_service
    safe_edge_infotainment
)

# Nodes to actually launch in the guest. Edit this list to add/remove nodes.
# Order matters: nodes are started in sequence with a 2s delay after vehicle_mock.
ACTIVE_NODES=(
    safe_edge_vehicle_mock
    safe_edge_safety_io_adapters
    safe_edge_policy_engine
    safe_edge_cloud_gateway
    safe_edge_ota_service
    safe_edge_infotainment
)

usage() {
    cat <<EOF
Usage: bash scripts/launch_hypervisor_nodes.sh [options]

Options:
  --smoke-test       launch nodes, run 1min stability check, then stop the VM
  --stability-test   launch nodes, monitor indefinitely (stats every 10s, df every 60s); Ctrl+C to stop
  --no-rebuild       skip image rebuild and reuse existing artifacts
  --no-run           do not auto-start guest nodes; print manual launch commands
  --stop             stop a running hypervisor host VM and exit
  -h, --help

Default (no options): launch VM and nodes, print SSH hints, keep VM running.

Environment variables:
  QNX_SDP_ROOT        path to the QNX SDP root
  QNX_ARCH            x86_64 or aarch64le
  CMAKE_BUILD_TYPE    Release or Debug
  HYP_GUEST_PRIVATE_KEY_FILE    SSH private key used to connect to the guest
  HYP_GUEST_AUTHORIZED_KEY_FILE SSH public key copied into guest root authorized_keys
  HYP_GUEST_STARTUP_PATCH_MODE  guest startup patch mode: forced or unforced
  HYP_GUEST_STARTUP_FREQ        forced startup-apic timer frequency when mode=forced
  SAFETY_BIN_DIR      directory containing safety domain binaries
  NON_SAFETY_BIN_DIR  directory containing non-safety domain binaries
EOF
}

while [[ $# -gt 0 ]]; do
    case "${1}" in
        --smoke-test)     OPT_SMOKE_TEST=1;     shift ;;
        --stability-test) OPT_STABILITY_TEST=1; shift ;;
        --no-rebuild)     OPT_NO_REBUILD=1;     shift ;;
        --no-run)         OPT_NO_RUN=1;         shift ;;
        --stop)           OPT_STOP=1;           shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "Unknown option: ${1}" >&2; usage >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_ssh_host_run() {
    local ip="${1}" cmd="${2}"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" ssh ${_SSH_OPTS} "${_SSH_USER}@${ip}" "${cmd}"
}

_ssh_guest_open() {
    local host_ip="${1}"
    rm -f "${_GUEST_CTL}"
    # No sshpass on outer ssh — guest uses pubkey; host password via ProxyCommand only.
    # shellcheck disable=SC2086
    ssh ${_SSH_OPTS} \
        -o ProxyCommand="sshpass -p ${_SSH_PASS} ssh ${_SSH_OPTS} ${_SSH_USER}@${host_ip} -W %h:%p" \
        -i "${HYP_GUEST_PRIVATE_KEY_FILE}" \
        -o ControlMaster=yes -o ControlPath="${_GUEST_CTL}" -o ControlPersist=yes \
        -fN "${_SSH_USER}@${_GUEST_IP}"
}

_ssh_guest_run() {
    local host_ip="${1}" cmd="${2}"
    # Reuse the persistent ControlMaster opened by _wait_for_guest_ssh (-fN).
    # No new handshake — no entropy cost.
    ssh -o ControlPath="${_GUEST_CTL}" "${_SSH_USER}@${_GUEST_IP}" "${cmd}"
}

_ssh_guest_close() {
    if [[ -S "${_GUEST_CTL}" ]]; then
        ssh -o ControlPath="${_GUEST_CTL}" -O exit "${_SSH_USER}@${_GUEST_IP}" 2>/dev/null || true
        rm -f "${_GUEST_CTL}"
    fi
}

_get_host_ip_address() {
    local max_tries=20 ip i
    for ((i = 0; i < max_tries; i++)); do
        set +e; ip="$(mkqnximage --getip 2>/dev/null)"; set -e
        if [[ -n "${ip}" ]]; then echo "${ip}"; return 0; fi
        sleep 2
    done
    echo "Timed out waiting for hypervisor host IP address." >&2
    return 1
}

_dump_guest_diagnostics() {
    local host_ip="${1}"
    echo ""
    echo "=== host qvm processes ==="
    _ssh_host_run "${host_ip}" "pidin ar | grep qvm" || echo "(no qvm process visible)"
    echo ""
    echo "=== host vp0 status ==="
    _ssh_host_run "${host_ip}" "ifconfig vp0" || echo "(vp0 not available)"
    echo ""
    echo "=== host ping to guest (${_GUEST_IP}) ==="
    _ssh_host_run "${host_ip}" "ping -c 1 ${_GUEST_IP}" || echo "(host could not ping guest)"
    echo ""
    echo "=== host start_guest log ==="
    _ssh_host_run "${host_ip}" "cat ${_HOST_GUEST_LOG}" || echo "(guest log not available yet)"
}

_guest_boot_log_has_fatal_error() {
    local host_ip="${1}"
    _ssh_host_run "${host_ip}" \
        "grep -q \
            -e 'init_qtime.c:.*ASSERT((r == 0)) failed!' \
            -e '\\*\\* ERROR: No system clock \\*\\*' \
            -e 'Failed to arm a resource manager:' \
            ${_HOST_GUEST_LOG}" >/dev/null 2>&1
}

_wait_for_guest_boot_log() {
    local host_ip="${1}"
    local max_tries=75 i
    for ((i = 1; i <= max_tries; i++)); do
        if _ssh_host_run "${host_ip}" "test -s ${_HOST_GUEST_LOG}" >/dev/null 2>&1; then
            if _guest_boot_log_has_fatal_error "${host_ip}"; then
                echo "Fatal error detected in guest boot log." >&2
                _dump_guest_diagnostics "${host_ip}"
                return 1
            fi
            if _ssh_host_run "${host_ip}" \
                "grep -q -e 'Starting SSH daemon' -e 'Starting sshd' ${_HOST_GUEST_LOG}" \
                >/dev/null 2>&1; then
                echo "Guest boot log shows SSH startup."
                return 0
            fi
            if (( i == 1 || i % 5 == 0 )); then
                echo "  guest boot log exists; waiting for SSH daemon startup..."
            fi
        else
            if (( i == 1 || i % 5 == 0 )); then
                echo "  waiting for host-side guest log at ${_HOST_GUEST_LOG}..."
            fi
        fi
        sleep 2
    done
    echo "Timed out waiting for guest boot output on the host." >&2
    _dump_guest_diagnostics "${host_ip}"
    return 1
}

_wait_for_guest_ping() {
    local host_ip="${1}"
    local max_tries=30 i
    for ((i = 1; i <= max_tries; i++)); do
        if _ssh_host_run "${host_ip}" "ping -c 1 ${_GUEST_IP}" >/dev/null 2>&1; then
            echo "Host can ping guest ${_GUEST_IP}."
            return 0
        fi
        if (( i == 1 || i % 5 == 0 )); then
            echo "  guest ${_GUEST_IP} not reachable from host yet..."
        fi
        sleep 1
    done
    echo "Timed out waiting for host-side reachability to guest ${_GUEST_IP}." >&2
    _dump_guest_diagnostics "${host_ip}"
    return 1
}

_wait_for_guest_ssh() {
    local host_ip="${1}"
    local max_tries=75 i
    _wait_for_guest_boot_log "${host_ip}"
    _wait_for_guest_ping "${host_ip}"
    rm -f "${_GUEST_CTL}"
    sleep 5  # Give post_start time to seed /dev/random before first SSH attempt
    for ((i = 1; i <= max_tries; i++)); do
        rm -f "${_GUEST_CTL}"
        # Open a persistent background ControlMaster (-fN: fork after auth, no command).
        # The guest uses pubkey — no sshpass needed on the outer ssh.
        # sshpass is only used inside ProxyCommand (to authenticate against the host).
        # Without sshpass wrapping the outer ssh, the daemon is not killed by PTY closure
        # when sshpass exits (sshpass PTY teardown sends SIGHUP to its SSH child).
        # shellcheck disable=SC2086
        if ssh ${_SSH_OPTS} \
                -o ProxyCommand="sshpass -p ${_SSH_PASS} ssh ${_SSH_OPTS} ${_SSH_USER}@${host_ip} -W %h:%p" \
                -i "${HYP_GUEST_PRIVATE_KEY_FILE}" \
                -o ControlMaster=yes -o ControlPath="${_GUEST_CTL}" -o ControlPersist=yes \
                -fN "${_SSH_USER}@${_GUEST_IP}" 2>/dev/null; then
            return 0
        fi
        if (( i == 1 || i % 5 == 0 )); then
            echo "  guest booted and pingable; waiting for nested SSH..."
        fi
        sleep 2
    done
    echo "Timed out waiting for guest SSH reachability via host ${host_ip}." >&2
    echo "=== SSH debug (last attempt) ===" >&2
    sshpass -p "${_SSH_PASS}" ssh ${_SSH_OPTS} -v \
        -o ProxyCommand="sshpass -p ${_SSH_PASS} ssh ${_SSH_OPTS} ${_SSH_USER}@${host_ip} -W %h:%p" \
        -i "${HYP_GUEST_PRIVATE_KEY_FILE}" \
        "${_SSH_USER}@${_GUEST_IP}" "echo guest-ready" 2>&1 | tail -30 >&2 || true
    _dump_guest_diagnostics "${host_ip}"
    return 1
}

_validate_qnx_binary() {
    local bin="${1}" description
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
    local name="${1}"
    case "${name}" in
        safe_edge_safety_io_adapters|safe_edge_policy_engine|safe_edge_vehicle_mock)
            echo "${SAFETY_BIN_DIR}/${name}" ;;
        safe_edge_cloud_gateway|safe_edge_ota_service|safe_edge_infotainment)
            echo "${NON_SAFETY_BIN_DIR}/${name}" ;;
        *) echo "Unknown vehicle node binary: ${name}" >&2; return 1 ;;
    esac
}

_validate_vehicle_binaries() {
    local name bin
    for name in "${VEHICLE_NODE_BINS[@]}"; do
        bin="$(_host_bin_path "${name}")"
        if [[ ! -f "${bin}" ]]; then
            echo "Binary not found: ${bin}" >&2
            echo "Build with: bash scripts/build_qnx.sh" >&2; exit 1
        fi
        _validate_qnx_binary "${bin}"
    done
}

_patch_guest_startup_flags() {
    local ifs_build="${GUEST_TARGET_DIR}/output/build/ifs.build"

    if [[ ! -f "${ifs_build}" ]]; then
        echo "Guest ifs.build not found: ${ifs_build}" >&2
        exit 1
    fi

    case "${HYP_GUEST_STARTUP_PATCH_MODE}" in
        forced)
            # Replace mkqnximage-generated startup flags (-zz breaks clock init in QVM)
            # with the flags that currently allow the guest to boot.
            sed -i "s/startup-apic \\(.*\\) -zz[[:space:]]*/startup-apic \\1 -vv -f ,,${HYP_GUEST_STARTUP_FREQ} /" \
                "${ifs_build}" 2>/dev/null || true
            ;;
        unforced)
            # Experimental mode for IDEA 4:
            # keep verbose output but do not force a timer frequency.
            sed -i 's/startup-apic \(.*\) -zz[[:space:]]*/startup-apic \1 -vv /' \
                "${ifs_build}" 2>/dev/null || true
            ;;
        *)
            echo "Unsupported HYP_GUEST_STARTUP_PATCH_MODE='${HYP_GUEST_STARTUP_PATCH_MODE}'" >&2
            echo "Use 'forced' or 'unforced'." >&2
            exit 1
            ;;
    esac
}

# ---------------------------------------------------------------------------
# Snippet generators
# ---------------------------------------------------------------------------

_refresh_guest_system_files_snippet() {
    local snippet="${GUEST_TARGET_DIR}/local/snippets/system_files.custom"
    local run_nodes="${GUEST_TARGET_DIR}/local/misc_files/run_nodes.sh"
    local authorized_keys="${GUEST_TARGET_DIR}/local/misc_files/authorized_keys"
    local name bin
    mkdir -p "$(dirname "${snippet}")" "$(dirname "${run_nodes}")"

    # Generate run_nodes.sh dynamically from ACTIVE_NODES.
    # Written as a real host file so the mkqnximage buildfile parser never sees
    # the absolute guest paths in its own syntax.
    {
        cat << 'HEADER'
#!/bin/sh
LOG_DIR=/data/safe-edge-stage2/logs
DATA_DIR=/data/safe-edge-stage2
mkdir -p "${LOG_DIR}" "${DATA_DIR}"
printf "soc=50.0\nemergency_stop=0\nadas_fault=0\navailable_charge_kw=50.0\navailable_discharge_kw=50.0\nv2g_ready=1\nspeed_mps=0.0\nbraking_available=1\nsteering_available=1\n" > "${DATA_DIR}/input.txt"
rm -f "${LOG_DIR}"/*.pid "${LOG_DIR}"/*.log
HEADER
        local first=1
        for name in "${ACTIVE_NODES[@]}"; do
            if [[ "${first}" -eq 1 ]]; then
                first=0
            else
                echo "sleep 1"
            fi
            echo "/system/bin/${name} > \"\${LOG_DIR}/${name}.log\" 2>&1 &"
            echo "echo \$! > \"\${LOG_DIR}/${name}.pid\""
        done
    } > "${run_nodes}"
    chmod +x "${run_nodes}"
    cp "${HYP_GUEST_AUTHORIZED_KEY_FILE}" "${authorized_keys}"
    chmod 600 "${authorized_keys}"

    {
        echo "# local/snippets/system_files.custom"
        echo "# Generated by scripts/launch_hypervisor_nodes.sh"
        echo "[dperms=755 type=dir] root/.ssh"
        for name in "${VEHICLE_NODE_BINS[@]}"; do
            bin="$(_host_bin_path "${name}")"
            echo "[perms=555] bin/${name}=${bin}"
        done
        echo "[perms=555] bin/run_nodes.sh=${run_nodes}"
        echo "[perms=644] root/.ssh/authorized_keys=${authorized_keys}"
        # Override sshd_config to keep sessions alive and allow enough auth attempts.
        # [-dupignore] replaces the default inline sshd_config from the base system.build.
        cat <<'SSHD'
[-dupignore]
[perms=444] etc/ssh/sshd_config = {
Protocol 2
HostKey /data/var/ssh/ssh_host_rsa_key
HostKey /data/var/ssh/ssh_host_ed25519_key
Ciphers aes128-ctr,aes192-ctr,aes256-ctr
MACs hmac-sha2-512-etm@openssh.com,hmac-sha2-256-etm@openssh.com,umac-128-etm@openssh.com,hmac-sha2-512,hmac-sha2-256,umac-128@openssh.com
KexAlgorithms curve25519-sha256@libssh.org,ecdh-sha2-nistp256,ecdh-sha2-nistp384,ecdh-sha2-nistp521,diffie-hellman-group-exchange-sha256
PubkeyAuthentication yes
AuthorizedKeysFile /data/ssh/authorized_keys
StrictModes no
KbdInteractiveAuthentication no
ChallengeResponseAuthentication no
UsePAM no
PasswordAuthentication no
PermitUserEnvironment yes
PermitRootLogin yes
PidFile none
Subsystem sftp /system/bin/sftp-server
SshdSessionPath /system/bin/sshd-session
ClientAliveInterval 30
ClientAliveCountMax 10
MaxAuthTries 20
}
[+dupignore]
SSHD
    } > "${snippet}"
}

_refresh_guest_post_start_snippet() {
    local snippet="${GUEST_TARGET_DIR}/local/snippets/post_start.custom"
    local pubkey
    pubkey="$(cat "${HYP_GUEST_AUTHORIZED_KEY_FILE}")"
    mkdir -p "$(dirname "${snippet}")"
    # Write authorized_keys to /data (writable virtio-blk) at runtime.
    # Avoids IFS path-prefix issues with system_files.custom (/system/ prefix).
    # sshd reads authorized_keys per-connection so writing it here (before any
    # SSH attempt) is sufficient.
    # Seed the guest PRNG by restarting random with a seed file on /data.
    # The guest's default random startup fails (no virtio-entropy/rdrand), leaving
    # sshd with no entropy. Seeding here (post_start = after /data is mounted) fixes it.
    # Seed is base64-encoded (no null bytes) and decoded with base64 on the guest.
    # This is a fixed test seed — not suitable for production.
    local seed_b64="IedEPk92NsxQW/slRiEnDIw5/AFr6kd1MGponQbBjP1JGf1i8c62f9pJeM11xjaG9co3QEppfVIlHc45a7Nlivu+HfCOs6ywetbZ/rv4jgv7FhfFwO7FfsNy9foYm6tNR0shcF8iwO5ElEpk5W59WJ8Per3zrgs1fWqqOOgHEsA="
    # Write seed to /dev/random directly — random manager creates the device even when
    # unseeded. Writing adds entropy without needing to kill/restart the daemon.
    # Also persist seed to /data so random -s picks it up on subsequent boots.
    local seed_line="waitfor /dev/random 10"
    seed_line+=" && echo '${seed_b64}' | base64 -d > /dev/random 2>/dev/null"
    seed_line+="; mkdir -p /data/var/random"
    seed_line+=" && echo '${seed_b64}' | base64 -d > /data/var/random/rnd-seed 2>/dev/null"
    seed_line+="; true"
    # Nodes are NOT started from post_startup.sh — they are launched via SSH after
    # the ControlMaster is established. This prevents DDS nodes from consuming
    # /dev/random entropy before our SSH handshake completes.
    cat > "${snippet}" <<EOF
# local/snippets/post_start.custom
# Generated by scripts/launch_hypervisor_nodes.sh
route add -net 224.0.0.0/4 vtnet0
${seed_line}
mkdir -p /data/ssh
echo "${pubkey}" > /data/ssh/authorized_keys
chmod 600 /data/ssh/authorized_keys
EOF
}

_print_manual_guest_run_commands() {
    local host_ip="${1}"
    cat <<EOF
Guest nodes were not auto-started.

Manual launch commands:
  Full startup script:
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${_GUEST_CTL} root@${_GUEST_IP} /system/bin/run_nodes.sh

  Interactive guest shell:
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${_GUEST_CTL} root@${_GUEST_IP}

  Fallback if the control socket is gone:
    sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ProxyCommand='sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@${host_ip} -W %h:%p' -i '${HYP_GUEST_PRIVATE_KEY_FILE}' root@${_GUEST_IP}

  Individual guest binaries:
    /system/bin/safe_edge_infotainment
    /system/bin/safe_edge_safety_io_adapters
    /system/bin/safe_edge_policy_engine
    /system/bin/safe_edge_vehicle_mock
    /system/bin/safe_edge_cloud_gateway
    /system/bin/safe_edge_ota_service
EOF
}

_refresh_guest_ifs_start_snippet() {
    local snippet="${GUEST_TARGET_DIR}/local/snippets/ifs_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/ifs_start.custom
# Generated by scripts/launch_hypervisor_nodes.sh
EOF
}

_refresh_host_system_files_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/system_files.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/system_files.custom
# Generated by scripts/launch_hypervisor_nodes.sh
EOF
}

_refresh_host_post_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/post_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/post_start.custom
# Generated by scripts/launch_hypervisor_nodes.sh
route add -net 224.0.0.0/4 vtnet0
mkdir -p /data/var/log/hypervisor
/data/hypervisor/start_guest >/data/var/log/hypervisor/start_guest.log 2>&1 &
EOF
}

_refresh_host_ifs_start_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/ifs_start.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<'EOF'
# local/snippets/ifs_start.custom
# Generated by scripts/launch_hypervisor_nodes.sh
EOF
}

_refresh_host_guest_bundle_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/data_files.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<EOF
# local/snippets/data_files.custom
# Generated by scripts/launch_hypervisor_nodes.sh
[dperms=555 type=dir] hypervisor
[dperms=555 type=dir] hypervisor/guest
hypervisor/guest/ifs.bin=${GUEST_IFS}
hypervisor/guest/disk-qvm=${GUEST_DISK}
[perms=555] /hypervisor/start_guest = {
#!/bin/sh
if [ ! -e /dev/qvmdisk0 ]; then
   echo "Setting up block devices for guest"
   devb-loopback loopback blksz=512,prefix=qvmdisk,fd=/data/hypervisor/guest/disk-qvm
   waitfor /dev/qvmdisk0
   echo "Setting up networking interface for guest"
   ifconfig vp0 create 192.168.10.1/24 up
fi
echo "Binding guest network peer"
vpctl vp0 peer=/dev/qvm/mkqnximage-guest/network bind=/dev/vdevpeers/vp0
echo "Starting guest VM"
qvm @/data/hypervisor/guest/guest.conf
}
hypervisor/guest/guest.conf = {
system mkqnximage-guest

ram 0xa0000
rom 0xc0000,0x40000
ram 512M
cpu
load /data/hypervisor/guest/ifs.bin

vdev ioapic
        loc 0xf8000000
        intr apic
        name myioapic

vdev ser8250
        hostdev >-
        intr myioapic:4
        name ser8250_0

vdev timer8254
        intr myioapic:0
        name timer8254_0

vdev mc146818
        name mc146818_0

vdev shmem
        name shmem_0

vdev pckeyboard
        name pckeyboard_0

vdev virtio-net
        name network
        peerfeats 0x0382
        peer /dev/vdevpeers/vp0

vdev virtio-blk
        hostdev /dev/qvmdisk0
        name virtio-blk_qvmdisk0

vdev 8259
        loc 0x20
vdev 8259
        loc 0xa0

vdev hpet
        intr myioapic:2
        name hpet_0

}
EOF
}

_reset_generated_target_output() {
    rm -rf "${1}/output"
}

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------

if [[ ! -f "${QNX_SDP_ROOT}/qnxsdp-env.sh" ]]; then
    echo "QNX SDK not found at QNX_SDP_ROOT='${QNX_SDP_ROOT}'" >&2; exit 1
fi
for dir in "${TARGET_DIR}" "${GUEST_TARGET_DIR}"; do
    if [[ ! -d "${dir}" ]]; then
        echo "QNX target directory not found: ${dir}" >&2; exit 1
    fi
done
if ! command -v sshpass >/dev/null 2>&1; then
    echo "sshpass not found. Install with: sudo apt install sshpass" >&2; exit 1
fi
if [[ ! -f "${HYP_GUEST_PRIVATE_KEY_FILE}" ]]; then
    echo "Guest SSH private key not found: ${HYP_GUEST_PRIVATE_KEY_FILE}" >&2; exit 1
fi
if [[ ! -f "${HYP_GUEST_AUTHORIZED_KEY_FILE}" ]]; then
    echo "Guest SSH public key not found: ${HYP_GUEST_AUTHORIZED_KEY_FILE}" >&2; exit 1
fi

# shellcheck source=/dev/null
source "${QNX_SDP_ROOT}/qnxsdp-env.sh" >/dev/null 2>&1

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "qemu-system-x86_64 not found. Install with: sudo apt install qemu-system-x86" >&2; exit 1
fi
if ! command -v brctl >/dev/null 2>&1; then
    echo "brctl not found. Install with: sudo apt install bridge-utils" >&2; exit 1
fi

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

cd "${TARGET_DIR}"

if [[ "${OPT_STOP}" -eq 1 ]]; then
    _ssh_guest_close
    echo "Stopping hypervisor host VM..."
    mkqnximage --stop 2>/dev/null || true
    kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true
    echo "VM stopped."
    exit 0
fi

kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true

# Inject tsc_freq into QEMU so the QNX hypervisor host sees a fixed TSC.
# qvm inherits this base for its virtual LAPIC → guest clock ratio ≈ 1x.
# Without this, QEMU uses the host TSC (~2.6 GHz turbo) while the QNX hint
# is 1 GHz → guest clock runs ~2.6x faster than real time.
_QEMU_WRAPPER="/tmp/qemu-tsc-wrapper-$$"
cat > "${_QEMU_WRAPPER}" <<'WRAPPER_EOF'
#!/bin/bash
args=()
for arg in "$@"; do
    if [[ "${arg}" == "host" ]]; then
        args+=("host,tsc_freq=_TSC_FREQ_,invtsc=on")
    else
        args+=("${arg}")
    fi
done
exec /usr/bin/qemu-system-x86_64 "${args[@]}"
WRAPPER_EOF
sed -i "s/_TSC_FREQ_/${HYP_GUEST_STARTUP_FREQ}/" "${_QEMU_WRAPPER}"
chmod +x "${_QEMU_WRAPPER}"
ln -sf "${_QEMU_WRAPPER}" /tmp/qemu-system-x86_64
export PATH="/tmp:${PATH}"

if [[ "${OPT_NO_REBUILD}" -eq 0 ]]; then
    _validate_vehicle_binaries

    # Generate guest snippets
    _refresh_guest_ifs_start_snippet
    _refresh_guest_post_start_snippet
    _refresh_guest_system_files_snippet
    _reset_generated_target_output "${GUEST_TARGET_DIR}"

    echo "Building QNX guest image..."
    echo "  guest target: ${GUEST_TARGET_DIR}"
    echo "  startup mode: ${HYP_GUEST_STARTUP_PATCH_MODE}"
    # Generate build files first, then patch startup flags before building.
    (cd "${GUEST_TARGET_DIR}" && mkqnximage --noprompt --clean >/dev/null 2>&1) || true
    _patch_guest_startup_flags
    (cd "${GUEST_TARGET_DIR}" && mkqnximage --noprompt --build >/dev/null 2>&1)

    for artifact in "${GUEST_IFS}" "${GUEST_DISK}"; do
        if [[ ! -f "${artifact}" ]]; then
            echo "Expected guest artifact missing after build: ${artifact}" >&2; exit 1
        fi
    done

    # Generate host snippets (after guest build — data_files.custom references output artifacts)
    _refresh_host_ifs_start_snippet
    _refresh_host_post_start_snippet
    _refresh_host_system_files_snippet
    _refresh_host_guest_bundle_snippet
    _reset_generated_target_output "${TARGET_DIR}"

    echo "Building QNX hypervisor host image and starting QEMU..."
    echo "  host target : ${TARGET_DIR}"
    mkqnximage --noprompt --guest=none --run=-h --clean >/dev/null 2>&1
else
    for artifact in "${GUEST_IFS}" "${GUEST_DISK}"; do
        if [[ ! -f "${artifact}" ]]; then
            echo "Guest artifact not found: ${artifact}" >&2
            echo "Rebuild first with: bash scripts/launch_hypervisor_nodes.sh" >&2; exit 1
        fi
    done
    echo "Starting QNX hypervisor host QEMU (skipping rebuild)..."
    mkqnximage --noprompt --guest=none --run=-h >/dev/null 2>&1
fi

echo "Waiting for hypervisor host IP address..."
HOST_IP="$(_get_host_ip_address)"
echo "Hypervisor host is up: ${HOST_IP}"

echo "Waiting for guest SSH reachability at ${_GUEST_IP} via host..."
_wait_for_guest_ssh "${HOST_IP}"
echo "Guest is reachable over SSH."

echo ""
echo "Host SSH hint : sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@${HOST_IP}"
echo "Guest SSH hint: ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${_GUEST_CTL} root@${_GUEST_IP}"
echo ""
if [[ "${OPT_NO_RUN}" -eq 1 ]]; then
    _print_manual_guest_run_commands "${HOST_IP}"
    echo "When finished: bash scripts/launch_hypervisor_nodes.sh --stop"
elif [[ "${OPT_SMOKE_TEST}" -eq 1 || "${OPT_STABILITY_TEST}" -eq 1 ]]; then
    # Shared guest-side monitoring function used by both modes.
    # smoke-test : runs for 60s then stops the VM.
    # stability-test: runs indefinitely (infinite loop) until the user kills the script.
    echo "LAPIC hint: ${HYP_GUEST_STARTUP_FREQ} Hz"
    if [[ "${OPT_SMOKE_TEST}" -eq 1 ]]; then
        _MONITOR_CMD='
         /system/bin/run_nodes.sh
         LOG_DIR=/data/safe-edge-stage2/logs
         NODES="safe_edge_vehicle_mock safe_edge_safety_io_adapters safe_edge_policy_engine safe_edge_cloud_gateway safe_edge_ota_service safe_edge_infotainment"
         _print_stats() {
             _elapsed="$1" _total="$2" _show_df="$3"
             _m=$((_elapsed/60)); _s=$((_elapsed%60))
             _tm=$((_total/60)); _ts=$((_total%60))
             printf "\n  (%dm%02ds / %dm%02ds)\n" "${_m}" "${_s}" "${_tm}" "${_ts}"
             printf "  %-32s %5s %8s %8s %8s\n" "node" "alive" "start" "now" "delta"
             printf "  %-32s %5s %8s %8s %8s\n" "----" "-----" "-----" "---" "-----"
             for _node in ${NODES}; do
                 _log="${LOG_DIR}/${_node}.log"
                 _alive=N; pidin 2>/dev/null | grep -q "${_node}" && _alive=Y
                 _now=0; [ -f "${_log}" ] && _now=$(wc -l < "${_log}" 2>/dev/null || echo 0)
                 _start=$(eval echo \${_prev_${_node}:-0})
                 _delta=$((_now - _start))
                 eval "_prev_${_node}=${_now}"
                 printf "  %-32s %5s %8d %8d %8d\n" "${_node}" "${_alive}" "${_start}" "${_now}" "${_delta}"
             done
             [ "${_show_df}" = "1" ] && { echo ""; df 2>/dev/null; }
         }
         for _node in ${NODES}; do eval "_prev_${_node}=0"; done
         echo "=== Smoke test (1 min) ==="
         _e=0; while [ "${_e}" -lt 60 ]; do
             sleep 10; _e=$((_e+10))
             _show_df=0; [ "$((_e%60))" -eq 0 ] && _show_df=1
             _print_stats "${_e}" 60 "${_show_df}"
         done
         echo ""
         echo "=== pidin ==="
         pidin 2>/dev/null | grep safe_edge || echo "(no safe_edge processes)"'
        _ssh_guest_run "${HOST_IP}" "${_MONITOR_CMD}"
        echo ""
        echo "Smoke test complete. Stopping VM..."
        _ssh_guest_close
        mkqnximage --stop 2>/dev/null || true
        kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true
        echo "VM stopped."
    else
        _MONITOR_CMD='
         /system/bin/run_nodes.sh
         LOG_DIR=/data/safe-edge-stage2/logs
         NODES="safe_edge_vehicle_mock safe_edge_safety_io_adapters safe_edge_policy_engine safe_edge_cloud_gateway safe_edge_ota_service safe_edge_infotainment"
         _print_stats() {
             _elapsed="$1" _show_df="$2"
             _h=$((_elapsed/3600)); _m=$(((_elapsed%3600)/60)); _s=$((_elapsed%60))
             printf "\n  (%dh%02dm%02ds elapsed)\n" "${_h}" "${_m}" "${_s}"
             printf "  %-32s %5s %8s %8s %8s\n" "node" "alive" "start" "now" "delta"
             printf "  %-32s %5s %8s %8s %8s\n" "----" "-----" "-----" "---" "-----"
             for _node in ${NODES}; do
                 _log="${LOG_DIR}/${_node}.log"
                 _alive=N; pidin 2>/dev/null | grep -q "${_node}" && _alive=Y
                 _now=0; [ -f "${_log}" ] && _now=$(wc -l < "${_log}" 2>/dev/null || echo 0)
                 _start=$(eval echo \${_prev_${_node}:-0})
                 _delta=$((_now - _start))
                 eval "_prev_${_node}=${_now}"
                 printf "  %-32s %5s %8d %8d %8d\n" "${_node}" "${_alive}" "${_start}" "${_now}" "${_delta}"
             done
             [ "${_show_df}" = "1" ] && { echo ""; df 2>/dev/null; }
         }
         for _node in ${NODES}; do eval "_prev_${_node}=0"; done
         echo "=== Stability test (Ctrl+C to stop) ==="
         _e=0
         while true; do
             sleep 10; _e=$((_e+10))
             _show_df=0; [ "$((_e%60))" -eq 0 ] && _show_df=1
             _print_stats "${_e}" "${_show_df}"
         done'
        echo "Stability test running. Press Ctrl+C or run '--stop' from another terminal to stop."
        trap '_ssh_guest_close; echo "Stopped."; exit 0' INT TERM
        _ssh_guest_run "${HOST_IP}" "${_MONITOR_CMD}"
    fi
else
    # Default: launch nodes and block until stopped.
    # The guest has no entropy source — each new SSH handshake drains the pool.
    # To keep the ControlMaster alive without a new handshake, we run a long
    # sleep on the guest through the existing socket. While that sleep is active,
    # the ControlMaster stays open and the user can connect reusing it.
    echo "Launching nodes via SSH..."
    _ssh_guest_run "${HOST_IP}" '/system/bin/run_nodes.sh'

    echo ""
    echo "VM is running. Connect from another terminal with:"
    echo "  Host : sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@${HOST_IP}"
    echo "  Guest: ssh -o StrictHostKeyChecking=no -o ControlPath=${_GUEST_CTL} root@${_GUEST_IP}"
    echo ""
    echo "Logs: /data/safe-edge-stage2/logs/"
    echo "Press Ctrl+C or run 'bash scripts/launch_hypervisor_nodes.sh --stop' to stop."

    # Block until the ControlMaster (-fN background process) dies or user interrupts.
    trap '_ssh_guest_close; echo "Stopped."; exit 0' INT TERM
    while ssh -o ControlPath="${_GUEST_CTL}" "${_SSH_USER}@${_GUEST_IP}" "true" >/dev/null 2>&1; do
        sleep 30
    done
    echo "Guest SSH connection lost. VM may have stopped." >&2
fi
