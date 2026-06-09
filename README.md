# SafeEDGE

This repository is self-contained for SafeEDGE source code, QNX target definitions, build scripts, and test launchers.
It does not require the old `~/Safe/SAFE-EDGE` repository.

It does not vendor third-party SDKs or source trees. QNX SDP 8 and Safe-DDS source code must be installed/provided outside this repository and pointed to with environment variables.

## Running scripts

All scripts live under `scripts/` and resolve paths relative to their own location.
**Always run them from inside the `scripts/` directory:**

```bash
cd scripts
bash <script_name>.sh [options]
```

They also work when invoked from the repository root with a prefix (`bash scripts/<name>.sh`), but the canonical form is from inside `scripts/`.

## Customer Quick Start

For Ubuntu/Debian hosts, install the host packages that can be installed automatically:

```bash
cd scripts
bash install_host_deps.sh
```

Provide the two external inputs that are not stored in this repository:

```bash
export QNX_SDP_ROOT="/path/to/qnx800"
export QNX_HOST="$QNX_SDP_ROOT/host/linux/x86_64"
export QNX_TARGET="$QNX_SDP_ROOT/target/qnx"
export SAFE_DDS_PATH="/path/to/Safe-DDS-source-release"
```

`scripts/env.example` contains the same variables as a source-able template.

Check the environment:

```bash
bash check_setup.sh
```

Build and test (QNX):

```bash
cd scripts
bash build_safedds_qnx.sh -- -j2
bash build_qnx.sh --idl -- -j2
bash launch_tpi_2_3_test.sh
bash launch_tpi_2_1_test.sh
bash launch_tpi_2_2_test.sh
bash launch_tpi_2_5_test.sh
```

## Hypervisor Path

Launch vehicle-side nodes inside a QNX Hypervisor guest (Linux host → QNX host VM → QNX guest VM):

```bash
bash scripts/launch_hypervisor_nodes.sh
```

Stop the hypervisor host VM:

```bash
bash scripts/launch_hypervisor_nodes.sh --stop
```

Prerequisites: same host-side tools as the plain QEMU path. The launcher builds
the guest image first and the host image second. `--no-rebuild` reuses existing
artifacts.

On successful launch, SSH access hints for host and guest are printed.

What is committed vs generated:
- Committed: `qnx/targets/qemu-qnx800-x86_64-hypervisor/` and `qnx/targets/qvm-safe-edge-qnx800-x86_64/` skeletons (`options`, wrappers, stable snippets)
- Generated at runtime: `data_files.custom`, `system_files.custom`, `ifs_start.custom`, `post_start.custom`, `output/`

Build and test (FastDDS / Docker):

```bash
bash build_ubuntu.sh --tests
bash launch_fast_server_test.sh
bash launch_fast_edge_test.sh
```

For Linux-only validation of the common server component:

```bash
bash install_host_deps.sh --linux-only
bash check_setup.sh --linux-only
bash launch_tpi_2_3_test.sh
```

## Repository Paths

- Shared IDL sources: `idl/`
- Safe DDS generated headers: `safe_dds/idl/`
- Shared server code: `common_server/`
- Safe DDS server: `safe_dds/server/`
- Safe DDS edge: `safe_dds/edge/`
- Safe DDS safety domain: `safe_dds/safety/`
- Safe DDS non-safety domain: `safe_dds/non_safety/`
- FastDDS server: `fast_dds/server/`
- FastDDS edge: `fast_dds/edge/`
- FastDDS generated headers: `fast_dds/idl/`
- FastDDS Dockerfiles: `fast_dds/docker/`
- QNX toolchain file: `qnx/toolchains/qnx8.cmake`
- Safe DDS QNX build script: `scripts/build_safedds_qnx.sh`
- Generated Safe DDS QNX package: `qnx/install/safedds-qnx8-x86_64/safedds`
- Bundled QNX QEMU target: `qnx/targets/qemu-qnx800-x86_64`
- Scripts: `scripts/`
- Test logs: `scripts/logs/`

## Versioned QNX Assets

These files are needed by the build and QNX test scripts and are intended to be kept in this repository:

- `qnx/toolchains/qnx8.cmake`
- `scripts/build_safedds_qnx.sh`
- `qnx/targets/qemu-qnx800-x86_64/mkqnximage-wrapper.sh`
- `qnx/targets/qemu-qnx800-x86_64/local/options`
- `qnx/targets/qemu-qnx800-x86_64/local/valgrind.files`
- stable snippets under `qnx/targets/qemu-qnx800-x86_64/local/snippets/`

These files are generated and should not be committed:

- `qnx/build/`
- `qnx/install/`
- `qnx/targets/qemu-qnx800-x86_64/output/`
- `scripts/logs/`
- `qnx/targets/qemu-qnx800-x86_64/local/misc_files/*`
- `qnx/targets/qemu-qnx800-x86_64/local/ssh-ident`
- `qnx/targets/qemu-qnx800-x86_64/local/snippets/ifs_start.custom`
- `qnx/targets/qemu-qnx800-x86_64/local/snippets/post_start.custom`
- `qnx/targets/qemu-qnx800-x86_64/local/snippets/system_files.custom`

The `misc_files` entries include QNX VM keys, `shadow`, and other local image files. The three generated snippets above are rewritten by `launch_tpi_2_1_test.sh` and `launch_tpi_2_2_test.sh`.

## External Prerequisites

### Required to build QNX targets

- QNX SDP 8 installed on the host
- Safe-DDS source tree available on the host
- `SAFE_DDS_PATH` exported and pointing to the Safe-DDS source tree
- Default SDK path used by scripts if `QNX_SDP_ROOT` is not set: `/home/$USER/qnx800`
- Required SDK files/directories:
  - `$QNX_SDP_ROOT/qnxsdp-env.sh`
  - `$QNX_SDP_ROOT/host/linux/x86_64`
  - `$QNX_SDP_ROOT/target/qnx`
- `cmake`
- a C/C++ build toolchain usable by CMake on the host
- Internet access during configure time if GTest is not already available locally

`scripts/install_host_deps.sh` installs the host packages listed above where possible. It does not install QNX SDP 8 or Safe-DDS sources.

### Required to run QNX VM tests

- `qemu-system-x86_64`
- `sshpass`
- `brctl` from `bridge-utils`
- `file`

### Required to build and run FastDDS Docker tests

- Docker

FastDDS is provided by the base Docker image (`eprosima/vulcanexus:kilted-base`); no separate FastDDS installation is required on the host.

### Required to run the Linux test

- `cmake`
- `libcurl` development package
  - Ubuntu/Debian: `sudo apt install libcurl4-openssl-dev`

## Environment Variables

Required for QNX builds:

```bash
export QNX_SDP_ROOT="/home/$USER/qnx800"
export QNX_HOST="$QNX_SDP_ROOT/host/linux/x86_64"
export QNX_TARGET="$QNX_SDP_ROOT/target/qnx"
export SAFE_DDS_PATH="/path/to/Safe-DDS-source-release"
```

Optional variables:

```bash
export QNX_ARCH="x86_64"
export CMAKE_BUILD_TYPE="Release"
```

## Pilot Server API Key

Requests to the Pilot Server read the API key from `/etc/safe-edge/server.ini`.
The key is not stored in this repository and should not be committed.

Example file:

```ini
[pilot_server]
api_key = XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
```

If the file does not exist, the real Pilot Server checks in the Linux test are skipped.

## Setup Check

Install Ubuntu/Debian host packages:

```bash
cd scripts
bash install_host_deps.sh
```

For only the Linux test dependencies:

```bash
bash install_host_deps.sh --linux-only
```

Run:

```bash
bash check_setup.sh
```

What it does:

- checks for required host tools
- validates the expected QNX SDK path
- validates repo paths under `qnx/`
- validates that `SAFE_DDS_PATH` is defined for QNX builds
- reports local/generated QNX folders if they have not been created yet

For a Linux-only check:

```bash
bash check_setup.sh --linux-only
```

## Build

### QNX targets

Build and install Safe DDS for QNX if `qnx/install/safedds-qnx8-x86_64/safedds` is missing:

```bash
cd scripts
bash build_safedds_qnx.sh -- -j2
```

Build all QNX targets from this repository:

```bash
bash build_qnx.sh -- -j2
```

If files under `idl/` changed, regenerate Safe DDS generated headers during the build:

```bash
bash build_qnx.sh --idl -- -j2
```

This configures and installs:

- `common_server`
- `safe_dds/server`
- `safe_dds/edge`
- `safe_dds/safety`
- `safe_dds/non_safety`

Installed binaries end up in:

- `common_server/install/server-common-qnx8-x86_64-Release/bin`
- `safe_dds/install/server-qnx8-x86_64-Release/bin`
- `safe_dds/install/edge-qnx8-x86_64-Release/bin`
- `safe_dds/install/safety-qnx8-x86_64-Release/bin`
- `safe_dds/install/non-safety-qnx8-x86_64-Release/bin`

### FastDDS Docker images

Build the runtime images:

```bash
cd scripts
bash build_ubuntu.sh
```

Build runtime and test images:

```bash
bash build_ubuntu.sh --tests
```

Images produced:

- `safe-edge-server:fast`
- `safe-edge-edge:fast`
- `safe-edge-server:fast-test` (with `--tests`)
- `safe-edge-edge:fast-test` (with `--tests`)

## Test

All test scripts log their output under `scripts/logs/`.

### KPI/TPI 2.3: Linux test

```bash
cd scripts
bash launch_tpi_2_3_test.sh
```

Log: `scripts/logs/launch_tpi_2_3.log`

### TPI 2.1: QNX server test

```bash
cd scripts
bash launch_tpi_2_1_test.sh
```

Log: `scripts/logs/launch_tpi_2_1.log`

### TPI 2.2: QNX edge test

```bash
cd scripts
bash launch_tpi_2_2_test.sh
```

Log: `scripts/logs/launch_tpi_2_2.log`

### TPI 2.5: QNX vehicle node smoke test

```bash
cd scripts
bash launch_tpi_2_5_test.sh
```

This starts the SafeDDS safety and non-safety nodes on the QNX VM, verifies that all six processes stay alive during the smoke-test window, prints their logs, and stops the VM.

The underlying launcher can also leave the nodes running for manual inspection:

```bash
cd scripts
bash aux_vehicle_nodes.sh
```

Logs:

- `scripts/logs/launch_tpi_2_5.log`
- `scripts/logs/aux_vehicle_nodes.log`

### FastDDS server integration test

```bash
cd scripts
bash launch_fast_server_test.sh
```

Log: `scripts/logs/launch_fast_server_test.log`

Builds `safe-edge-server:fast-test` automatically if the image is not present.

### FastDDS edge integration test

```bash
cd scripts
bash launch_fast_edge_test.sh
```

Log: `scripts/logs/launch_fast_edge_test.log`

Builds `safe-edge-edge:fast-test` automatically if the image is not present.

## Notes

- The QNX tests rebuild the QEMU image from `qnx/targets/qemu-qnx800-x86_64`.
- Generated target output is recreated under `qnx/targets/qemu-qnx800-x86_64/output/` and is ignored by git.
- The Linux test may execute real Pilot Server checks if `/etc/safe-edge/server.ini` exists on the host.
- The FastDDS Docker tests are self-contained: each test image spawns the component under test as a subprocess and communicates with it via DDS over the loopback interface.
