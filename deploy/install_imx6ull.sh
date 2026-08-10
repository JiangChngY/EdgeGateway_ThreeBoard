#!/bin/sh
set -eu

FORCE_CONFIG=0
if [ "$#" -eq 2 ] && [ "$1" = "--force-config" ]; then
    FORCE_CONFIG=1
    shift
fi
if [ "$#" -ne 1 ]; then
    echo "usage: $0 [--force-config] /path/to/edge-aggregator" >&2
    exit 2
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
install -d /opt/edge-gateway/bin /opt/edge-gateway/config /opt/edge-gateway/data
install -m 0755 "$1" /opt/edge-gateway/bin/edge-aggregator
if [ "$FORCE_CONFIG" -eq 1 ] || [ ! -e /opt/edge-gateway/config/imx6ull.ini ]; then
    install -m 0644 "$SCRIPT_DIR/../config/imx6ull.ini" /opt/edge-gateway/config/imx6ull.ini
else
    echo "preserved existing /opt/edge-gateway/config/imx6ull.ini"
fi
install -m 0644 "$SCRIPT_DIR/edge-aggregator.service" /etc/systemd/system/edge-aggregator.service
systemctl daemon-reload
systemctl enable edge-aggregator.service
echo "installed; run: systemctl start edge-aggregator"
