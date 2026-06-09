#!/usr/bin/env bash
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
BASE_TARGET_DIR="${WORKSPACE_ROOT}/qnx/targets/qvm-safe-edge-qnx800-x86_64"
SAFETY_VARIANT_DIR="${BASE_TARGET_DIR}/local/variants/safety"
NON_SAFETY_VARIANT_DIR="${BASE_TARGET_DIR}/local/variants/non-safety"

STAGING_ROOT="/tmp/safe-edge-hyp-split"
SAFETY_STAGING="${STAGING_ROOT}/guest-safety"
NON_SAFETY_STAGING="${STAGING_ROOT}/guest-non-safety"

SAFETY_GUEST_IFS="${SAFETY_STAGING}/output/ifs.bin"
SAFETY_GUEST_DISK="${SAFETY_STAGING}/output/disk-qvm"
NON_SAFETY_GUEST_IFS="${NON_SAFETY_STAGING}/output/ifs.bin"
NON_SAFETY_GUEST_DISK="${NON_SAFETY_STAGING}/output/disk-qvm"

: "${SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"
: "${NON_SAFETY_BIN_DIR:=${WORKSPACE_ROOT}/safe_dds/install/non-safety-qnx8-${QNX_ARCH}-${CMAKE_BUILD_TYPE}/bin}"

_SSH_PASS="root"
_SSH_USER="root"
_SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=30 -o LogLevel=ERROR"

SAFETY_GUEST_IP="192.168.10.2"
SAFETY_HOST_LINK_IP="192.168.10.1/24"
SAFETY_HOST_LOG="/data/var/log/hypervisor/start_guest_safety.log"
SAFETY_GUEST_CTL="/tmp/ssh-guest-safety-ctl"

NON_SAFETY_GUEST_IP="192.168.20.2"
NON_SAFETY_HOST_LINK_IP="192.168.20.1/24"
NON_SAFETY_HOST_LOG="/data/var/log/hypervisor/start_guest_non_safety.log"
NON_SAFETY_GUEST_CTL="/tmp/ssh-guest-non-safety-ctl"

OPT_NO_REBUILD=0
OPT_NO_RUN=0
OPT_STOP=0

SAFETY_NODES=(
    safe_edge_vehicle_mock
    safe_edge_safety_io_adapters
    safe_edge_policy_engine
)

NON_SAFETY_NODES=(
    safe_edge_cloud_gateway
    safe_edge_ota_service
    safe_edge_infotainment
)

usage() {
    cat <<EOF
Usage: bash scripts/launch_hypervisor_split.sh [options]

Options:
  --no-rebuild    skip image rebuild and reuse existing artifacts
  --no-run        boot both guests, but do not auto-start nodes
  --stop          stop the hypervisor host VM and both guest SSH control sockets
  -h, --help
EOF
}

while [[ $# -gt 0 ]]; do
    case "${1}" in
        --no-rebuild) OPT_NO_REBUILD=1; shift ;;
        --no-run) OPT_NO_RUN=1; shift ;;
        --stop) OPT_STOP=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: ${1}" >&2; usage >&2; exit 1 ;;
    esac
done

_ssh_host_run() {
    local ip="${1}" cmd="${2}"
    # shellcheck disable=SC2086
    sshpass -p "${_SSH_PASS}" ssh ${_SSH_OPTS} "${_SSH_USER}@${ip}" "${cmd}"
}

_ssh_guest_run() {
    local ctl="${1}" guest_ip="${2}" cmd="${3}"
    ssh -o ControlPath="${ctl}" "${_SSH_USER}@${guest_ip}" "${cmd}"
}

_ssh_guest_close() {
    local ctl="${1}" guest_ip="${2}"
    if [[ -S "${ctl}" ]]; then
        ssh -o ControlPath="${ctl}" -O exit "${_SSH_USER}@${guest_ip}" 2>/dev/null || true
        rm -f "${ctl}"
    fi
}

_get_host_ip_address() {
    local max_tries=20 ip i
    for ((i = 0; i < max_tries; i++)); do
        set +e; ip="$(mkqnximage --getip 2>/dev/null)"; set -e
        if [[ -n "${ip}" ]]; then
            echo "${ip}"
            return 0
        fi
        sleep 2
    done
    echo "Timed out waiting for hypervisor host IP address." >&2
    return 1
}

_dump_guest_log() {
    local host_ip="${1}" log_path="${2}"
    echo "=== ${log_path} ==="
    _ssh_host_run "${host_ip}" "cat ${log_path}" || echo "(guest log not available)"
}

_wait_for_guest_boot_log() {
    local host_ip="${1}" log_path="${2}"
    local max_tries=75 i
    for ((i = 1; i <= max_tries; i++)); do
        if _ssh_host_run "${host_ip}" "test -s ${log_path}" >/dev/null 2>&1; then
            if _ssh_host_run "${host_ip}" \
                "grep -q -e 'Starting SSH daemon' -e 'Starting sshd' ${log_path}" \
                >/dev/null 2>&1; then
                return 0
            fi
        fi
        if (( i == 1 || i % 5 == 0 )); then
            echo "  waiting for guest boot log ${log_path} to reach sshd..."
        fi
        sleep 2
    done
    echo "Timed out waiting for guest boot output in ${log_path}." >&2
    _dump_guest_log "${host_ip}" "${log_path}"
    return 1
}

_wait_for_guest_ping() {
    local host_ip="${1}" host_link_ip="${2}" guest_ip="${3}"
    local max_tries=30 i
    for ((i = 1; i <= max_tries; i++)); do
        if _ssh_host_run "${host_ip}" \
            "ping -c 1 ${guest_ip}" >/dev/null 2>&1; then
            return 0
        fi
        if (( i == 1 || i % 5 == 0 )); then
            echo "  guest ${guest_ip} not reachable from host yet..."
        fi
        sleep 1
    done
    echo "Timed out waiting for guest ${guest_ip} reachability from host." >&2
    return 1
}

_wait_for_guest_ssh() {
    local host_ip="${1}" guest_ip="${2}" ctl="${3}" log_path="${4}" host_link_ip="${5}"
    local max_tries=75 i
    _wait_for_guest_boot_log "${host_ip}" "${log_path}"
    _wait_for_guest_ping "${host_ip}" "${host_link_ip}" "${guest_ip}"
    rm -f "${ctl}"
    sleep 5
    for ((i = 1; i <= max_tries; i++)); do
        rm -f "${ctl}"
        # shellcheck disable=SC2086
        if ssh ${_SSH_OPTS} \
                -o ProxyCommand="sshpass -p ${_SSH_PASS} ssh ${_SSH_OPTS} ${_SSH_USER}@${host_ip} -W %h:%p" \
                -i "${HYP_GUEST_PRIVATE_KEY_FILE}" \
                -o ControlMaster=yes -o ControlPath="${ctl}" -o ControlPersist=yes \
                -fN "${_SSH_USER}@${guest_ip}" 2>/dev/null; then
            return 0
        fi
        if (( i == 1 || i % 5 == 0 )); then
            echo "  guest ${guest_ip} booted and pingable; waiting for nested SSH..."
        fi
        sleep 2
    done
    echo "Timed out waiting for guest SSH at ${guest_ip}." >&2
    _dump_guest_log "${host_ip}" "${log_path}"
    return 1
}

_validate_qnx_binary() {
    local bin="${1}" description
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
    local name="${1}"
    case "${name}" in
        safe_edge_safety_io_adapters|safe_edge_policy_engine|safe_edge_vehicle_mock)
            echo "${SAFETY_BIN_DIR}/${name}" ;;
        safe_edge_cloud_gateway|safe_edge_ota_service|safe_edge_infotainment)
            echo "${NON_SAFETY_BIN_DIR}/${name}" ;;
        *) echo "Unknown vehicle node binary: ${name}" >&2; return 1 ;;
    esac
}

_validate_node_binaries() {
    local name bin
    for name in "$@"; do
        bin="$(_host_bin_path "${name}")"
        if [[ ! -f "${bin}" ]]; then
            echo "Binary not found: ${bin}" >&2
            echo "Build with: bash scripts/build_qnx.sh" >&2
            exit 1
        fi
        _validate_qnx_binary "${bin}"
    done
}

_patch_guest_startup_flags() {
    local guest_target_dir="${1}"
    local ifs_build="${guest_target_dir}/output/build/ifs.build"

    if [[ ! -f "${ifs_build}" ]]; then
        echo "Guest ifs.build not found: ${ifs_build}" >&2
        exit 1
    fi

    case "${HYP_GUEST_STARTUP_PATCH_MODE}" in
        forced)
            sed -i "s/startup-apic \\(.*\\) -zz[[:space:]]*/startup-apic \\1 -vv -f ,,${HYP_GUEST_STARTUP_FREQ} /" \
                "${ifs_build}" 2>/dev/null || true
            ;;
        unforced)
            sed -i 's/startup-apic \(.*\) -zz[[:space:]]*/startup-apic \1 -vv /' \
                "${ifs_build}" 2>/dev/null || true
            ;;
        *)
            echo "Unsupported HYP_GUEST_STARTUP_PATCH_MODE='${HYP_GUEST_STARTUP_PATCH_MODE}'" >&2
            exit 1
            ;;
    esac
}

_refresh_guest_files() {
    local guest_target_dir="${1}" guest_ip="${2}" run_script_name="${3}" log_dir="${4}" data_dir="${5}" env_prefix="${6}"
    shift 6
    local nodes=("$@")
    local snippet="${guest_target_dir}/local/snippets/system_files.custom"
    local post_start="${guest_target_dir}/local/snippets/post_start.custom"
    local ifs_start="${guest_target_dir}/local/snippets/ifs_start.custom"
    local run_script="${guest_target_dir}/local/misc_files/${run_script_name}"
    local authorized_keys="${guest_target_dir}/local/misc_files/authorized_keys"
    local name bin

    mkdir -p "$(dirname "${snippet}")" "$(dirname "${run_script}")"

    {
        echo "#!/bin/sh"
        echo "LOG_DIR=${log_dir}"
        echo "DATA_DIR=${data_dir}"
        echo 'mkdir -p "${LOG_DIR}" "${DATA_DIR}"'
        echo 'rm -f "${LOG_DIR}"/*.pid "${LOG_DIR}"/*.log'
        if [[ "${env_prefix}" == "safety" ]]; then
            echo 'printf "soc=50.0\nemergency_stop=0\nadas_fault=0\navailable_charge_kw=50.0\navailable_discharge_kw=50.0\nv2g_ready=1\nspeed_mps=0.0\nbraking_available=1\nsteering_available=1\n" > "${DATA_DIR}/input.txt"'
        fi
        local first=1
        for name in "${nodes[@]}"; do
            if [[ "${first}" -eq 0 ]]; then
                echo "sleep 1"
            fi
            first=0
            if [[ "${env_prefix}" == "safety" ]]; then
                echo "SAFE_EDGE_OWN_IP=${guest_ip} /system/bin/${name} > \"\${LOG_DIR}/${name}.log\" 2>&1 &"
            else
                echo "/system/bin/${name} > \"\${LOG_DIR}/${name}.log\" 2>&1 &"
            fi
            echo "echo \$! > \"\${LOG_DIR}/${name}.pid\""
        done
    } > "${run_script}"
    chmod +x "${run_script}"
    cp "${HYP_GUEST_AUTHORIZED_KEY_FILE}" "${authorized_keys}"
    chmod 600 "${authorized_keys}"

    {
        echo "# local/snippets/system_files.custom"
        echo "# Generated by scripts/launch_hypervisor_split.sh"
        echo "[dperms=755 type=dir] root/.ssh"
        for name in "${nodes[@]}"; do
            bin="$(_host_bin_path "${name}")"
            echo "[perms=555] bin/${name}=${bin}"
        done
        echo "[perms=555] bin/${run_script_name}=${run_script}"
        echo "[perms=644] root/.ssh/authorized_keys=${authorized_keys}"
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

    cat > "${ifs_start}" <<'EOF'
# local/snippets/ifs_start.custom
# Generated by scripts/launch_hypervisor_split.sh
EOF

    local pubkey seed_b64 seed_line
    pubkey="$(cat "${HYP_GUEST_AUTHORIZED_KEY_FILE}")"
    seed_b64="IedEPk92NsxQW/slRiEnDIw5/AFr6kd1MGponQbBjP1JGf1i8c62f9pJeM11xjaG9co3QEppfVIlHc45a7Nlivu+HfCOs6ywetbZ/rv4jgv7FhfFwO7FfsNy9foYm6tNR0shcF8iwO5ElEpk5W59WJ8Per3zrgs1fWqqOOgHEsA="
    seed_line="waitfor /dev/random 10"
    seed_line+=" && echo '${seed_b64}' | base64 -d > /dev/random 2>/dev/null"
    seed_line+="; mkdir -p /data/var/random"
    seed_line+=" && echo '${seed_b64}' | base64 -d > /data/var/random/rnd-seed 2>/dev/null"
    seed_line+="; true"
    cat > "${post_start}" <<EOF
# local/snippets/post_start.custom
# Generated by scripts/launch_hypervisor_split.sh
route add -net 224.0.0.0/4 vtnet0
${seed_line}
mkdir -p /data/ssh
echo "${pubkey}" > /data/ssh/authorized_keys
chmod 600 /data/ssh/authorized_keys
EOF
}

_setup_staging() {
    rm -rf "${STAGING_ROOT}"
    mkdir -p "${STAGING_ROOT}"
    cp -a "${BASE_TARGET_DIR}" "${SAFETY_STAGING}"
    cp -a "${BASE_TARGET_DIR}" "${NON_SAFETY_STAGING}"
    rm -rf "${SAFETY_STAGING}/output" "${SAFETY_STAGING}/local/variants"
    rm -rf "${NON_SAFETY_STAGING}/output" "${NON_SAFETY_STAGING}/local/variants"
    cp "${SAFETY_VARIANT_DIR}/options" "${SAFETY_STAGING}/local/options"
    cp "${NON_SAFETY_VARIANT_DIR}/options" "${NON_SAFETY_STAGING}/local/options"
}

_refresh_host_common_snippets() {
    mkdir -p "${TARGET_DIR}/local/snippets"
    cat > "${TARGET_DIR}/local/snippets/system_files.custom" <<'EOF'
# local/snippets/system_files.custom
# Generated by scripts/launch_hypervisor_split.sh
EOF
    cat > "${TARGET_DIR}/local/snippets/ifs_start.custom" <<'EOF'
# local/snippets/ifs_start.custom
# Generated by scripts/launch_hypervisor_split.sh
EOF
    cat > "${TARGET_DIR}/local/snippets/post_start.custom" <<'EOF'
# local/snippets/post_start.custom
# Generated by scripts/launch_hypervisor_split.sh
route add -net 224.0.0.0/4 vtnet0
mkdir -p /data/var/log/hypervisor
/data/hypervisor/start_safety_guest >/data/var/log/hypervisor/start_guest_safety.log 2>&1 &
EOF
}

_refresh_host_guest_bundle_snippet() {
    local snippet="${TARGET_DIR}/local/snippets/data_files.custom"
    mkdir -p "$(dirname "${snippet}")"
    cat > "${snippet}" <<EOF
# local/snippets/data_files.custom
# Generated by scripts/launch_hypervisor_split.sh
[dperms=555 type=dir] hypervisor
[dperms=555 type=dir] hypervisor/guest-safety
[dperms=555 type=dir] hypervisor/guest-non-safety
hypervisor/guest-safety/ifs.bin=${SAFETY_GUEST_IFS}
hypervisor/guest-safety/disk-qvm=${SAFETY_GUEST_DISK}
hypervisor/guest-non-safety/ifs.bin=${NON_SAFETY_GUEST_IFS}
hypervisor/guest-non-safety/disk-qvm=${NON_SAFETY_GUEST_DISK}
[perms=555] /hypervisor/start_safety_guest = {
#!/bin/sh
if [ ! -e /dev/qvmdisks0 ]; then
   devb-loopback loopback blksz=512,prefix=qvmdisks,fd=/data/hypervisor/guest-safety/disk-qvm
   waitfor /dev/qvmdisks0
fi
if ! ifconfig vp0 >/dev/null 2>&1; then
   ifconfig vp0 create ${SAFETY_HOST_LINK_IP} up
fi
vpctl vp0 peer=/dev/qvm/mkqnximage-guest-safety/network bind=/dev/vdevpeers/vp0
qvm @/data/hypervisor/guest-safety/guest.conf
}
[perms=555] /hypervisor/start_non_safety_guest = {
#!/bin/sh
if [ ! -e /dev/qvmdiskn0 ]; then
   devb-loopback loopback blksz=512,prefix=qvmdiskn,fd=/data/hypervisor/guest-non-safety/disk-qvm
   waitfor /dev/qvmdiskn0
fi
if ! ifconfig vp1 >/dev/null 2>&1; then
   ifconfig vp1 create ${NON_SAFETY_HOST_LINK_IP} up
fi
vpctl vp1 peer=/dev/qvm/mkqnximage-guest-non-safety/network bind=/dev/vdevpeers/vp1
qvm @/data/hypervisor/guest-non-safety/guest.conf
}
hypervisor/guest-safety/guest.conf = {
system mkqnximage-guest-safety

ram 0xa0000
rom 0xc0000,0x40000
ram 512M
cpu
load /data/hypervisor/guest-safety/ifs.bin

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
        hostdev /dev/qvmdisks0
        name virtio-blk_qvmdisk0

vdev 8259
        loc 0x20
vdev 8259
        loc 0xa0

vdev hpet
        intr myioapic:2
        name hpet_0
}
hypervisor/guest-non-safety/guest.conf = {
system mkqnximage-guest-non-safety

ram 0xa0000
rom 0xc0000,0x40000
ram 512M
cpu
load /data/hypervisor/guest-non-safety/ifs.bin

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
        peer /dev/vdevpeers/vp1

vdev virtio-blk
        hostdev /dev/qvmdiskn0
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

_print_manual_commands() {
    local host_ip="${1}"
    cat <<EOF

Both guests are up. Nodes were not auto-started.

Connect:
  Host      : sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@${host_ip}
  Safety    : ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${SAFETY_GUEST_CTL} root@${SAFETY_GUEST_IP}
  Non-safety: ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${NON_SAFETY_GUEST_CTL} root@${NON_SAFETY_GUEST_IP}

Start nodes:
  Safety    : ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${SAFETY_GUEST_CTL} root@${SAFETY_GUEST_IP} /system/bin/run_safety_nodes.sh
  Non-safety: ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${NON_SAFETY_GUEST_CTL} root@${NON_SAFETY_GUEST_IP} /system/bin/run_non_safety_nodes.sh

When finished: bash scripts/launch_hypervisor_split.sh --stop
EOF
}

for dir in "${TARGET_DIR}" "${BASE_TARGET_DIR}" "${SAFETY_VARIANT_DIR}" "${NON_SAFETY_VARIANT_DIR}"; do
    if [[ ! -d "${dir}" ]]; then
        echo "QNX target directory not found: ${dir}" >&2
        exit 1
    fi
done
if [[ ! -f "${QNX_SDP_ROOT}/qnxsdp-env.sh" ]]; then
    echo "QNX SDK not found at QNX_SDP_ROOT='${QNX_SDP_ROOT}'" >&2
    exit 1
fi
if ! command -v sshpass >/dev/null 2>&1; then
    echo "sshpass not found. Install with: sudo apt install sshpass" >&2
    exit 1
fi
if [[ ! -f "${HYP_GUEST_PRIVATE_KEY_FILE}" || ! -f "${HYP_GUEST_AUTHORIZED_KEY_FILE}" ]]; then
    echo "Guest SSH keypair not found." >&2
    exit 1
fi

# shellcheck source=/dev/null
source "${QNX_SDP_ROOT}/qnxsdp-env.sh" >/dev/null 2>&1

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "qemu-system-x86_64 not found. Install with: sudo apt install qemu-system-x86" >&2
    exit 1
fi

cd "${TARGET_DIR}"

if [[ "${OPT_STOP}" -eq 1 ]]; then
    _ssh_guest_close "${SAFETY_GUEST_CTL}" "${SAFETY_GUEST_IP}"
    _ssh_guest_close "${NON_SAFETY_GUEST_CTL}" "${NON_SAFETY_GUEST_IP}"
    echo "Stopping hypervisor host VM..."
    mkqnximage --stop 2>/dev/null || true
    kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true
    echo "VM stopped."
    exit 0
fi

kill "$(pgrep -f qemu-system-x86_64)" 2>/dev/null || true

_QEMU_WRAPPER="/tmp/qemu-tsc-wrapper-split-$$"
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
    _validate_node_binaries "${SAFETY_NODES[@]}"
    _validate_node_binaries "${NON_SAFETY_NODES[@]}"

    _setup_staging

    _refresh_guest_files \
        "${SAFETY_STAGING}" "${SAFETY_GUEST_IP}" "run_safety_nodes.sh" \
        "/data/safe-edge-safety/logs" "/data/safe-edge-safety" "safety" \
        "${SAFETY_NODES[@]}"
    echo "Building safety QNX guest image..."
    (cd "${SAFETY_STAGING}" && mkqnximage --noprompt --clean >/dev/null 2>&1) || true
    _patch_guest_startup_flags "${SAFETY_STAGING}"
    (cd "${SAFETY_STAGING}" && mkqnximage --noprompt --build >/dev/null 2>&1)

    _refresh_guest_files \
        "${NON_SAFETY_STAGING}" "${NON_SAFETY_GUEST_IP}" "run_non_safety_nodes.sh" \
        "/data/safe-edge-non-safety/logs" "/data/safe-edge-non-safety" "non-safety" \
        "${NON_SAFETY_NODES[@]}"
    echo "Building non-safety QNX guest image..."
    (cd "${NON_SAFETY_STAGING}" && mkqnximage --noprompt --clean >/dev/null 2>&1) || true
    _patch_guest_startup_flags "${NON_SAFETY_STAGING}"
    (cd "${NON_SAFETY_STAGING}" && mkqnximage --noprompt --build >/dev/null 2>&1)

    for artifact in \
        "${SAFETY_GUEST_IFS}" "${SAFETY_GUEST_DISK}" \
        "${NON_SAFETY_GUEST_IFS}" "${NON_SAFETY_GUEST_DISK}"; do
        if [[ ! -f "${artifact}" ]]; then
            echo "Expected guest artifact missing after build: ${artifact}" >&2
            exit 1
        fi
    done

    _refresh_host_common_snippets
    _refresh_host_guest_bundle_snippet
    rm -rf "${TARGET_DIR}/output"

    echo "Building QNX hypervisor host image and starting QEMU..."
    mkqnximage --noprompt --guest=none --run=-h --clean >/dev/null 2>&1
else
    for artifact in \
        "${SAFETY_GUEST_IFS}" "${SAFETY_GUEST_DISK}" \
        "${NON_SAFETY_GUEST_IFS}" "${NON_SAFETY_GUEST_DISK}"; do
        if [[ ! -f "${artifact}" ]]; then
            echo "Guest artifact not found: ${artifact}" >&2
            echo "Rebuild first with: bash scripts/launch_hypervisor_split.sh" >&2

            exit 1
        fi
    done
    echo "Starting QNX hypervisor host QEMU (skipping rebuild)..."
    mkqnximage --noprompt --guest=none --run=-h >/dev/null 2>&1
fi

echo "Waiting for hypervisor host IP address..."
HOST_IP="$(_get_host_ip_address)"
echo "Hypervisor host is up: ${HOST_IP}"

echo "Waiting for safety guest SSH reachability at ${SAFETY_GUEST_IP}..."
_wait_for_guest_ssh "${HOST_IP}" "${SAFETY_GUEST_IP}" "${SAFETY_GUEST_CTL}" "${SAFETY_HOST_LOG}" "${SAFETY_HOST_LINK_IP}"
echo "Safety guest is reachable over SSH."

echo "Starting non-safety guest on host..."
_ssh_host_run "${HOST_IP}" \
    "nohup /data/hypervisor/start_non_safety_guest >/data/var/log/hypervisor/start_guest_non_safety.log 2>&1 </dev/null &"

echo "Waiting for non-safety guest SSH reachability at ${NON_SAFETY_GUEST_IP}..."
_wait_for_guest_ssh "${HOST_IP}" "${NON_SAFETY_GUEST_IP}" "${NON_SAFETY_GUEST_CTL}" "${NON_SAFETY_HOST_LOG}" "${NON_SAFETY_HOST_LINK_IP}"
echo "Non-safety guest is reachable over SSH."

if [[ "${OPT_NO_RUN}" -eq 1 ]]; then
    _print_manual_commands "${HOST_IP}"
    exit 0
fi

echo "Launching safety nodes..."
_ssh_guest_run "${SAFETY_GUEST_CTL}" "${SAFETY_GUEST_IP}" "/system/bin/run_safety_nodes.sh"
echo "Launching non-safety nodes..."
_ssh_guest_run "${NON_SAFETY_GUEST_CTL}" "${NON_SAFETY_GUEST_IP}" "/system/bin/run_non_safety_nodes.sh"

cat <<EOF

Both guests are running.

Connect:
  Host      : sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@${HOST_IP}
  Safety    : ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${SAFETY_GUEST_CTL} root@${SAFETY_GUEST_IP}
  Non-safety: ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ControlPath=${NON_SAFETY_GUEST_CTL} root@${NON_SAFETY_GUEST_IP}

Logs:
  Safety    : /data/safe-edge-safety/logs/
  Non-safety: /data/safe-edge-non-safety/logs/

Press Ctrl+C or run 'bash scripts/launch_hypervisor_split.sh --stop' to stop.
EOF

trap '_ssh_guest_close "${SAFETY_GUEST_CTL}" "${SAFETY_GUEST_IP}"; _ssh_guest_close "${NON_SAFETY_GUEST_CTL}" "${NON_SAFETY_GUEST_IP}"; echo "Stopped."; exit 0' INT TERM
while \
    ssh -o ControlPath="${SAFETY_GUEST_CTL}" "${_SSH_USER}@${SAFETY_GUEST_IP}" "true" >/dev/null 2>&1 && \
    ssh -o ControlPath="${NON_SAFETY_GUEST_CTL}" "${_SSH_USER}@${NON_SAFETY_GUEST_IP}" "true" >/dev/null 2>&1; do
    sleep 30
done
echo "At least one guest SSH connection was lost. VM may have stopped." >&2
