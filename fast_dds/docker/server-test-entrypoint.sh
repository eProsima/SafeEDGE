#!/bin/sh
set -e
if [ -n "${PILOT_API_KEY:-}" ]; then
    mkdir -p /etc/safe-edge
    printf '[pilot_server]\napi_key = %s\n' "${PILOT_API_KEY}" \
        > /etc/safe-edge/server.ini
fi
exec "$@"
