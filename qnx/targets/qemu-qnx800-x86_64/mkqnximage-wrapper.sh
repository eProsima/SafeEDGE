#!/usr/bin/env bash
set -euo pipefail

QNX_USER="${USER:-$(id -un)}"
: "${QNX_SDP_ROOT:=/home/${QNX_USER}/qnx800}"

# shellcheck source=/dev/null
source "${QNX_SDP_ROOT}/qnxsdp-env.sh"
exec mkqnximage "$@"
