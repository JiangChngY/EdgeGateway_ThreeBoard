#!/usr/bin/env bash
set -Eeuo pipefail

TFTP_ROOT="${TFTP_ROOT:-/srv/tftp}"
SERVER_IP="${1:-127.0.0.1}"
TEMP_DIR="$(mktemp -d)"

cleanup() {
    sudo umount "$TEMP_DIR/mp157" 2>/dev/null || true
    sudo umount "$TEMP_DIR/imx6ull" 2>/dev/null || true
    rm -rf -- "$TEMP_DIR"
}
trap cleanup EXIT

mkdir -p "$TEMP_DIR/mp157" "$TEMP_DIR/imx6ull"
systemctl is-active --quiet tftpd-hpa
systemctl is-active --quiet nfs-kernel-server

curl --fail --silent --show-error \
    "tftp://$SERVER_IP/mp157/uImage" -o "$TEMP_DIR/mp157-uImage"
cmp "$TEMP_DIR/mp157-uImage" "$TFTP_ROOT/mp157/uImage"
curl --fail --silent --show-error \
    "tftp://$SERVER_IP/imx6ull/emmc/zImage" -o "$TEMP_DIR/imx6ull-zImage"
cmp "$TEMP_DIR/imx6ull-zImage" "$TFTP_ROOT/imx6ull/emmc/zImage"

sudo mount -t nfs -o vers=3,tcp \
    "$SERVER_IP:/srv/nfs/edgegateway/mp157" "$TEMP_DIR/mp157"
sudo mount -t nfs -o vers=3,tcp \
    "$SERVER_IP:/srv/nfs/edgegateway/imx6ull" "$TEMP_DIR/imx6ull"
test -e "$TEMP_DIR/mp157/bin/sh" || test -L "$TEMP_DIR/mp157/bin/sh"
test -e "$TEMP_DIR/imx6ull/bin/sh" || test -L "$TEMP_DIR/imx6ull/bin/sh"

echo "TFTP downloads and both NFS root filesystems are available."
