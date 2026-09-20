#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BSP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BASE="${1:-$HOME/edgegateway_bsp}"
ARTIFACT_ROOT="${ARTIFACT_ROOT:-$BSP_DIR/artifacts}"
MP_ROOTFS_ARCHIVE="${MP_ROOTFS_ARCHIVE:-$BASE/archives/mp157-qt5.12.9-rootfs.tar.bz2}"
IMX_ROOTFS_ARCHIVE="${IMX_ROOTFS_ARCHIVE:-$BASE/archives/imx6ull-factory-rootfs.tar.bz2}"
IMX_VARIANT="${IMX_VARIANT:-emmc}"
TFTP_ROOT="${TFTP_ROOT:-/srv/tftp}"
NFS_ROOT="${NFS_ROOT:-/srv/nfs/edgegateway}"

case "$IMX_VARIANT" in
    emmc|nand) ;;
    *) echo "IMX_VARIANT must be emmc or nand" >&2; exit 2 ;;
esac

require_file() {
    if [[ ! -f "$1" ]]; then
        echo "Missing required file: $1" >&2
        exit 1
    fi
}

extract_once() {
    local archive="$1"
    local destination="$2"
    local marker="$destination/.edgegateway-rootfs-ready"

    if [[ -f "$marker" ]]; then
        echo "Rootfs already prepared: $destination"
        return
    fi
    if sudo find "$destination" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
        echo "Refusing to extract into non-empty directory: $destination" >&2
        exit 1
    fi
    sudo tar --numeric-owner -xjf "$archive" -C "$destination"
    sudo touch "$marker"
}

for file in \
    "$MP_ROOTFS_ARCHIVE" \
    "$IMX_ROOTFS_ARCHIVE" \
    "$ARTIFACT_ROOT/mp157/uImage" \
    "$ARTIFACT_ROOT/mp157/stm32mp157d-atk-edgegateway.dtb" \
    "$ARTIFACT_ROOT/mp157/u-boot-stm32mp157d-atk-edgegateway.stm32" \
    "$ARTIFACT_ROOT/mp157/kernel-modules.tar.gz" \
    "$ARTIFACT_ROOT/imx6ull/emmc/zImage" \
    "$ARTIFACT_ROOT/imx6ull/emmc/imx6ull-alientek-emmc-edgegateway.dtb" \
    "$ARTIFACT_ROOT/imx6ull/emmc/u-boot-emmc-edgegateway.imx" \
    "$ARTIFACT_ROOT/imx6ull/nand/zImage" \
    "$ARTIFACT_ROOT/imx6ull/nand/imx6ull-alientek-nand-edgegateway.dtb" \
    "$ARTIFACT_ROOT/imx6ull/nand/u-boot-nand-edgegateway.imx"; do
    require_file "$file"
done

sudo install -d -m 0755 \
    "$TFTP_ROOT/mp157/firmware" \
    "$TFTP_ROOT/imx6ull/emmc/firmware" \
    "$TFTP_ROOT/imx6ull/nand/firmware" \
    "$NFS_ROOT/mp157" \
    "$NFS_ROOT/imx6ull" \
    /etc/exports.d

extract_once "$MP_ROOTFS_ARCHIVE" "$NFS_ROOT/mp157"
extract_once "$IMX_ROOTFS_ARCHIVE" "$NFS_ROOT/imx6ull"
require_file "$ARTIFACT_ROOT/imx6ull/$IMX_VARIANT/kernel-modules.tar.gz"
sudo tar -xzf "$ARTIFACT_ROOT/mp157/kernel-modules.tar.gz" -C "$NFS_ROOT/mp157"
sudo tar -xzf "$ARTIFACT_ROOT/imx6ull/$IMX_VARIANT/kernel-modules.tar.gz" \
    -C "$NFS_ROOT/imx6ull"

sudo install -m 0644 "$ARTIFACT_ROOT/mp157/uImage" "$TFTP_ROOT/mp157/uImage"
sudo install -m 0644 "$ARTIFACT_ROOT/mp157/stm32mp157d-atk-edgegateway.dtb" \
    "$TFTP_ROOT/mp157/stm32mp157d-atk-edgegateway.dtb"
sudo install -m 0644 "$ARTIFACT_ROOT/mp157/u-boot-stm32mp157d-atk-edgegateway.stm32" \
    "$TFTP_ROOT/mp157/firmware/u-boot-stm32mp157d-atk-edgegateway.stm32"

for variant in emmc nand; do
    sudo install -m 0644 "$ARTIFACT_ROOT/imx6ull/$variant/zImage" \
        "$TFTP_ROOT/imx6ull/$variant/zImage"
    sudo install -m 0644 \
        "$ARTIFACT_ROOT/imx6ull/$variant/imx6ull-alientek-$variant-edgegateway.dtb" \
        "$TFTP_ROOT/imx6ull/$variant/imx6ull-alientek-$variant-edgegateway.dtb"
    sudo install -m 0644 "$ARTIFACT_ROOT/imx6ull/$variant/u-boot-$variant-edgegateway.imx" \
        "$TFTP_ROOT/imx6ull/$variant/firmware/u-boot-$variant-edgegateway.imx"
done

sudo install -m 0644 "$BSP_DIR/server/edgegateway.exports" \
    /etc/exports.d/edgegateway.exports
sudo install -m 0644 "$BSP_DIR/server/50-wired-nfs.network" \
    "$NFS_ROOT/mp157/lib/systemd/network/50-wired-nfs.network"
sudo install -D -m 0644 "$BSP_DIR/server/edgegateway-tmpfiles.conf" \
    "$NFS_ROOT/mp157/etc/tmpfiles.d/edgegateway.conf"
sudo install -D -m 0644 "$BSP_DIR/server/edgegateway-volatile.conf" \
    "$NFS_ROOT/mp157/etc/systemd/system/systemd-tmpfiles-setup.service.d/edgegateway.conf"
sudo install -m 0644 "$BSP_DIR/server/interfaces-imx6ull-nfs" \
    "$NFS_ROOT/imx6ull/etc/network/interfaces"
# Kernel modules are installed directly under /lib/modules by this project.
# Mask the vendor helper which incorrectly expects /boot/$(uname -r).
sudo ln -sfn /dev/null "$NFS_ROOT/mp157/etc/systemd/system/link-modules.service"
# This netboot configuration starts Linux directly without an OP-TEE device.
# Do not repeatedly start the unused userspace supplicant.
sudo ln -sfn /dev/null "$NFS_ROOT/mp157/etc/systemd/system/tee-supplicant.service"
sudo install -d -m 0755 \
    "$NFS_ROOT/mp157/var/volatile/log" \
    "$NFS_ROOT/mp157/var/volatile/lib/nfs/statd"
sudo install -d -m 1777 "$NFS_ROOT/mp157/var/volatile/tmp"
sudo touch "$NFS_ROOT/mp157/var/volatile/log/lastlog"
sudo chmod 0664 "$NFS_ROOT/mp157/var/volatile/log/lastlog"
sudo install -m 0644 "$BSP_DIR/server/tftpd-hpa" /etc/default/tftpd-hpa
sudo exportfs -rav
sudo systemctl enable --now rpcbind nfs-kernel-server tftpd-hpa
sudo systemctl restart nfs-kernel-server tftpd-hpa

echo "TFTP files:"
find "$TFTP_ROOT" -maxdepth 4 -type f -printf '%p %s bytes\n' | sort
echo "NFS exports:"
sudo exportfs -v
echo "Installed i.MX6ULL kernel modules for: $IMX_VARIANT"
