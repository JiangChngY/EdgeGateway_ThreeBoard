#!/bin/sh
set -eu

FORCE_CONFIG=0
if [ "$#" -eq 2 ] && [ "$1" = "--force-config" ]; then
    FORCE_CONFIG=1
    shift
fi
if [ "$#" -ne 1 ]; then
    echo "usage: $0 [--force-config] /path/to/edge-hmi" >&2
    exit 2
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
install -d /opt/edge-gateway/bin /opt/edge-gateway/config /opt/edge-gateway/data
install -m 0755 "$1" /opt/edge-gateway/bin/edge-hmi
if [ "$FORCE_CONFIG" -eq 1 ] || [ ! -e /opt/edge-gateway/config/mp157.ini ]; then
    install -m 0644 "$SCRIPT_DIR/../config/mp157.ini" /opt/edge-gateway/config/mp157.ini
else
    echo "preserved existing /opt/edge-gateway/config/mp157.ini"
fi
install -m 0644 "$SCRIPT_DIR/edge-hmi.service" /etc/systemd/system/edge-hmi.service
systemctl daemon-reload
systemctl enable edge-hmi.service
echo "installed; run: systemctl start edge-hmi"
