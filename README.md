# SafeEDGE

## 1. Purpose

SafeEDGE contains the source code, QNX target definitions, Docker packaging,
build scripts, and test launchers required to build and run the current system
from this repository alone.

This repository does **not** vendor:

- QNX SDP 8
- Safe-DDS source tree

Those must exist outside the repository and be provided through environment
variables.

## 2. Repository Structure

The most relevant repository areas are:

- `common_server/`
  Shared server-side code and tests used by the server path.
- `fast_dds/`
  Fast DDS based implementations, including:
  - `fast_dds/server/`
  - `fast_dds/edge/`
  - `fast_dds/docker/`
- `safe_dds/`
  SafeDDS based implementations, including:
  - `safe_dds/server/`
  - `safe_dds/edge/`
  - `safe_dds/safety/`
  - `safe_dds/non_safety/`
- `idl/`
  Shared IDL definitions.
- `qnx/`
  QNX toolchains, targets, hypervisor artifacts, and guest definitions.
- `scripts/`
  Build scripts, launchers, test launchers, setup helpers, and utilities.

Use `scripts/` for operational entry points, `qnx/` for QNX/hypervisor layout,
`fast_dds/` for Docker/Fast DDS code, and `safe_dds/` for SafeDDS/QNX code.

## 3. System Architecture and Endpoints

### Runtime groups

| Group | Runtime | Main process(es) |
|---|---|---|
| `server` | Docker container `safe-edge-server` | `safe_edge_server` |
| `edge` | Docker container `safe-edge-edge` | `safe_edge_edge_gateway` |
| `safety` | QNX guest VM or native Linux processes | `safe_edge_vehicle_mock`, `safe_edge_safety_io_adapters`, `safe_edge_policy_engine` |
| `non-safety` | QNX guest VM or native Linux processes | `safe_edge_cloud_gateway`, `safe_edge_ota_service`, `safe_edge_infotainment` |

### Execution modes used in this repository

The repository currently supports two practical execution modes for the
integrated launchers:

- `qnx`
  Docker `server` and `edge` run on the Linux host. Safety and non-safety
  binaries run inside a QNX VM and are controlled through SSH.
- `--linux`
  Docker `server` and `edge` still run on the Linux host. Safety and
  non-safety binaries run as native Linux host processes.

Some launchers and `qnx/` assets refer to a hypervisor or split-guest layout.

### High-level topology

```text
Linux host
├── Docker container: safe-edge-server   DDS participant port 8020
├── Docker container: safe-edge-edge     DDS participant port 8030
└── Vehicle-side binaries
    ├── qnx mode:
    │   ├── safety guest      participant ports 8001, 8002, 8003
    │   └── non-safety guest  participant ports 8011, 8012, 8013
    └── --linux mode:
        ├── safety processes on host      participant ports 8001, 8002, 8003
        └── non-safety processes on host  participant ports 8011, 8012, 8013
```

In `qnx` mode, the guest-side IPs and the Linux bridge IP come from the active
QNX target and launcher path. In `--linux` mode, the launchers resolve a host
IP dynamically and use explicit DDS peers on that host network stack.

### Interfaces and endpoints

- Docker services run with `--network host`
- `server` participates on the host network stack at DDS participant port `8020`
- `edge` participates on the host network stack at DDS participant port `8030`
- in `qnx` mode, SSH access to the guest VM is part of the normal operational flow
- in `--linux` mode, safety and non-safety binaries are started directly as host
  processes by the test launchers

### Current discovery configuration

The launchers configure DDS discovery explicitly through environment variables
such as:

- `SAFE_EDGE_OWN_IP`
- `SAFE_EDGE_SAFETY_IP`
- `SAFE_EDGE_NON_SAFETY_IP`
- `SAFE_EDGE_HOST_IP`
- `SAFE_EDGE_CROSS_DOMAIN_IP`
- `SAFE_EDGE_INITIAL_PEERS`

The resulting peer layout follows this pattern:

| Group | Current initial peers |
|---|---|
| `server` | non-safety `8011`, edge `8030` |
| `edge` | safety `8001`, safety `8002`, non-safety `8011`, server `8020` |
| `safety` | safety `8001,8002`, non-safety `8011`, server `8020`, edge `8030` |
| `non-safety` | safety `8001,8002`, non-safety `8011`, server `8020`, edge `8030` |

Exact IP values depend on the launcher mode and target environment. The port
roles above are the stable part.

## 4. Prerequisites

### QNX / Safe-DDS

Required to build QNX artifacts:

- QNX SDP 8 installed on the host
- Safe-DDS source tree available on the host
- `QNX_SDP_ROOT` exported or available at `/home/$USER/qnx800`
- `SAFE_DDS_PATH` exported and pointing to the Safe-DDS source tree

### Host tools

- `cmake`
- host C/C++ toolchain
- `docker`
- `qemu-system-x86_64`
- `sshpass`
- `bridge-utils` (`brctl`)
- `file`

### Linux development packages

- `libcurl` development package for Linux-side builds/tests

Ubuntu/Debian:

```bash
sudo apt install libcurl4-openssl-dev
```

### Helper script

Install what the repository can install automatically:

```bash
bash scripts/install_host_deps.sh
```

This helper does **not** install QNX SDP or Safe-DDS.

## 5. Configuration

### Required build environment

```bash
export QNX_SDP_ROOT="/path/to/qnx800"
export QNX_HOST="$QNX_SDP_ROOT/host/linux/x86_64"
export QNX_TARGET="$QNX_SDP_ROOT/target/qnx"
export SAFE_DDS_PATH="/path/to/Safe-DDS-source-release"
```

An example template is available at:

```bash
scripts/env.example
```

Validate the environment:

```bash
bash scripts/check_setup.sh
```

### Runtime topology variables

The integrated stack relies mainly on:

```bash
# Announced IP for the current process/container
SAFE_EDGE_OWN_IP=<resolved_at_runtime>

# Peer IPs used by integrated launchers
SAFE_EDGE_SAFETY_IP=<safety-side IP>
SAFE_EDGE_NON_SAFETY_IP=<non-safety-side IP>
SAFE_EDGE_HOST_IP=<host-side IP>
SAFE_EDGE_CROSS_DOMAIN_IP=<peer-domain IP>

# Explicit DDS discovery peers
SAFE_EDGE_INITIAL_PEERS=<ip:port,...>
```

Depending on binary family and launcher path, backward-compatible fallback
variables may still exist, but the current integrated stack is driven primarily
by:

- `SAFE_EDGE_OWN_IP`
- `SAFE_EDGE_INITIAL_PEERS`

### Pilot server API key

The server-side Pilot client does **not** take the API key from an environment
variable and it is not hardcoded in the source tree.

Both the Fast DDS server and the SafeDDS server read it from:

```bash
/etc/safe-edge/server.ini
```

Expected format:

```ini
[pilot_server]
api_key = <your_api_key>
```

Notes:

- the key must live under the `[pilot_server]` section
- if the file is missing, server code that talks to the Pilot backend will log:
  `Cannot open config file: /etc/safe-edge/server.ini`
- some real Pilot backend tests are skipped automatically when that file is not
  present

## 6. Build

### Docker / Fast DDS side

```bash
bash scripts/build_ubuntu.sh
```

### QNX side

```bash
bash scripts/build_qnx.sh --idl -- -j2
```

### SafeDDS QNX package only

```bash
bash scripts/build_safedds_qnx.sh
```

### Recommended full build sequence

```bash
bash scripts/build_ubuntu.sh
bash scripts/build_qnx.sh --idl -- -j2
```

## 7. Launchers

Run all scripts from the repository root:

```bash
bash scripts/<script_name>.sh [options]
```

### Launcher overview

| Goal | Script | Notes |
|---|---|---|
| Launch full integrated stack | `scripts/launch_all.sh` | Main current end-to-end launcher |
| Stop full integrated stack | `scripts/launch_all.sh --stop` | Stops Docker services and split hypervisor stack |
| Launch Docker server service | `scripts/launch_fast_server.sh` | Service mode by default |
| Launch Docker edge service | `scripts/launch_fast_edge.sh` | Service mode by default |
| Run Fast DDS server integration test | `scripts/launch_fast_server.sh --test` | Docker test mode |
| Run Fast DDS edge integration test | `scripts/launch_fast_edge.sh --test` | Docker test mode |
| Launch split QNX hypervisor stack | `scripts/launch_hypervisor_split.sh` | Safety guest + non-safety guest |
| Stop split QNX hypervisor stack | `scripts/launch_hypervisor_split.sh --stop` | Stops hypervisor/QNX split flow |
| Launch single-guest QNX baseline | `scripts/launch_hypervisor_nodes.sh` | Alternative/baseline launcher |
| Run Stage 3 outage test | `scripts/launch_tpi_3_1_test.sh` | Integrated stack, supports `qnx` and `--linux` |
| Run Stage 3 low-SoC test | `scripts/launch_tpi_3_2_test.sh` | Integrated stack, supports `qnx` and `--linux` |
| Run Stage 3 mixed-traffic benchmark | `scripts/launch_tpi_3_3_test.sh` | Integrated stack, supports `qnx` and `--linux` |
| Run all wired tests | `scripts/launch_tests.sh` | Aggregates Docker tests and TPI launchers |

### Full stack launcher

```bash
bash scripts/launch_all.sh [--no-rebuild]
```

`launch_all.sh`:

1. stops previous SafeEDGE instances
2. optionally rebuilds Docker and QNX artifacts
3. starts the split hypervisor guests
4. starts Docker `server` and `edge`
5. verifies DDS connectivity across all groups

### Hypervisor launchers

Split deployment:

```bash
bash scripts/launch_hypervisor_split.sh
bash scripts/launch_hypervisor_split.sh --no-rebuild
bash scripts/launch_hypervisor_split.sh --stop
```

Single-guest baseline:

```bash
bash scripts/launch_hypervisor_nodes.sh
```

### Stage 3 integrated launchers

```bash
bash scripts/launch_tpi_3_1_test.sh [--linux|--ubuntu] [--no-rebuild]
bash scripts/launch_tpi_3_2_test.sh [--linux|--ubuntu] [--no-rebuild]
bash scripts/launch_tpi_3_3_test.sh [--linux|--ubuntu] [--no-rebuild]
```

These launchers are repository-scoped validation entry points. They orchestrate
the stack that exists in this repository; they are not a replacement for the
formal project documentation.

## 8. Tests

### Run all tests

```bash
bash scripts/launch_tests.sh
bash scripts/launch_tests.sh --no-rebuild
```

`launch_tests.sh` currently runs:

- `launch_fast_server.sh --test`
- `launch_fast_edge.sh --test`
- `launch_tpi_2_3_test.sh`
- `launch_tpi_2_1_test.sh`
- `launch_tpi_2_2_test.sh`
- `launch_tpi_2_5_test.sh`
- `launch_tpi_2_6_test.sh`

### Run individual test launchers

```bash
bash scripts/launch_fast_server.sh --test
bash scripts/launch_fast_edge.sh --test
bash scripts/launch_tpi_2_1_test.sh
bash scripts/launch_tpi_2_2_test.sh
bash scripts/launch_tpi_2_3_test.sh
bash scripts/launch_tpi_2_5_test.sh
bash scripts/launch_tpi_2_6_test.sh
bash scripts/launch_tpi_3_1_test.sh
bash scripts/launch_tpi_3_2_test.sh
bash scripts/launch_tpi_3_3_test.sh
```

### Test intent

- Fast DDS launchers with `--test` run Docker integration tests
- `launch_tpi_2_3_test.sh` exercises the Linux/common server path
- the QNX TPI launchers exercise QNX VM and SafeDDS flows
- the Stage 3 launchers exercise the integrated stack from this repository in
  either `qnx` or `--linux` mode

## 9. Logs and Observability

### Docker service logs

```bash
docker logs -f safe-edge-server
docker logs -f safe-edge-edge
```

### launch_all.sh logs

```bash
tail -f /tmp/safe-edge-hypervisor.log
tail -f /tmp/safe-edge-edge.log
tail -f /tmp/safe-edge-server.log
```

### QNX guest access

After `launch_all.sh` or `launch_hypervisor_split.sh` succeeds, the active guest
IPs come from the chosen QNX target. The exact addresses are launcher-defined
and should be read from runtime output rather than assumed from this document.

### QNX guest logs

Safety:

```bash
ssh root@<safety-guest-ip> \
  'tail -f /data/safe-edge-safety/logs/*.log | grep -v "^==> .* <==$"'
```

Non-safety:

```bash
ssh root@<non-safety-guest-ip> \
  'tail -f /data/safe-edge-non-safety/logs/*.log | grep -v "^==> .* <==$"'
```

### Test logs

All test launchers write logs under:

```bash
scripts/logs/
```

## 10. Operational Flows

### Build and launch everything

```bash
bash scripts/build_ubuntu.sh
bash scripts/build_qnx.sh --idl -- -j2
bash scripts/launch_all.sh --no-rebuild
```

### Run tests only

```bash
bash scripts/launch_tests.sh
```

### Stop the stack

```bash
bash scripts/launch_all.sh --stop
```

### Run one component only

```bash
bash scripts/launch_fast_server.sh
bash scripts/launch_fast_edge.sh
bash scripts/launch_hypervisor_split.sh
```

## 11. Troubleshooting

### `Cannot open config file: /etc/safe-edge/server.ini`

Likely cause:

- missing Pilot server config file for server-side backend access

First thing to check:

- create or verify `/etc/safe-edge/server.ini`
- ensure it contains:

```ini
[pilot_server]
api_key = <your_api_key>
```

### QNX build cannot start

Likely cause:

- missing or misconfigured `QNX_SDP_ROOT`
- missing `SAFE_DDS_PATH`

First thing to check:

```bash
bash scripts/check_setup.sh
```

### Guests do not come up

Likely cause:

- QNX image build issue
- QEMU/hypervisor startup failure
- missing host dependencies such as `qemu-system-x86_64` or `sshpass`

First places to inspect:

- `/tmp/safe-edge-hypervisor.log`
- `bash scripts/check_setup.sh`

### Docker services do not start

Likely cause:

- missing Docker image
- Docker not running
- bad runtime environment

First places to inspect:

```bash
docker logs safe-edge-server
docker logs safe-edge-edge
```

### DDS connectivity does not converge

Likely cause:

- incorrect runtime topology variables
- guest routing issue
- missing peer in current discovery configuration

First places to inspect:

- `bash scripts/launch_all.sh`
- `/tmp/safe-edge-hypervisor.log`
- `/tmp/safe-edge-server.log`
- `/tmp/safe-edge-edge.log`
- guest logs under `/data/safe-edge-safety/logs/` and
  `/data/safe-edge-non-safety/logs/`
