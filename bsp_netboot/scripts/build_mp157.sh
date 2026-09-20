#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BSP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BASE="${1:-$HOME/edgegateway_bsp}"
SOURCE_ROOT="${SOURCE_ROOT:-$BASE/src}"
BUILD_ROOT="${BUILD_ROOT:-$BASE/build}"
ARTIFACT_ROOT="${ARTIFACT_ROOT:-$BASE/artifacts}"
UBOOT_SRC="${MP157_UBOOT_SRC:-$SOURCE_ROOT/mp157-uboot}"
LINUX_SRC="${MP157_LINUX_SRC:-$SOURCE_ROOT/mp157-linux}"
UBOOT_OUT="$BUILD_ROOT/mp157-uboot"
LINUX_OUT="$BUILD_ROOT/mp157-linux"
MODULES_STAGE="$BUILD_ROOT/mp157-modules"
JOBS="${JOBS:-$(nproc)}"

require_dir() {
    if [[ ! -d "$1" ]]; then
        echo "Missing source directory: $1" >&2
        exit 1
    fi
}

reset_build_dir() {
    local target="$1"
    case "$target" in
        "$BUILD_ROOT"/*) rm -rf -- "$target" ;;
        *) echo "Refusing to remove unexpected path: $target" >&2; exit 1 ;;
    esac
    mkdir -p "$target"
}

require_dir "$UBOOT_SRC"
require_dir "$LINUX_SRC"
command -v arm-linux-gnueabihf-gcc >/dev/null
command -v arm-linux-gnueabi-gcc >/dev/null

rsync -a "$BSP_DIR/overrides/mp157_uboot/" "$UBOOT_SRC/"
rsync -a "$BSP_DIR/overrides/mp157_linux/" "$LINUX_SRC/"
reset_build_dir "$UBOOT_OUT"
reset_build_dir "$LINUX_OUT"
reset_build_dir "$MODULES_STAGE"

make -C "$UBOOT_SRC" O="$UBOOT_OUT" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabihf- stm32mp15_atk_trusted_defconfig
make -C "$UBOOT_SRC" O="$UBOOT_OUT" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabihf- -j"$JOBS"

make -C "$LINUX_SRC" O="$LINUX_OUT" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabi- stm32mp1_atk_defconfig
make -C "$LINUX_SRC" O="$LINUX_OUT" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabi- -j"$JOBS" \
    LOADADDR=0xc2000040 uImage dtbs
make -C "$LINUX_SRC" O="$LINUX_OUT" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabi- -j"$JOBS" modules
# The vendor 5.4 tree creates modules.builtin.modinfo during the vmlinux
# modpost pass. Run it explicitly before modules_install.
make -C "$LINUX_SRC" O="$LINUX_OUT" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabi- -j"$JOBS" vmlinux
make -C "$LINUX_SRC" O="$LINUX_OUT" ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabi- \
    INSTALL_MOD_PATH="$MODULES_STAGE" INSTALL_MOD_STRIP=1 modules_install

install -d "$ARTIFACT_ROOT/mp157"
install -m 0644 "$UBOOT_OUT/u-boot.stm32" \
    "$ARTIFACT_ROOT/mp157/u-boot-stm32mp157d-atk-edgegateway.stm32"
install -m 0644 "$LINUX_OUT/arch/arm/boot/uImage" \
    "$ARTIFACT_ROOT/mp157/uImage"
install -m 0644 "$LINUX_OUT/arch/arm/boot/dts/stm32mp157d-atk.dtb" \
    "$ARTIFACT_ROOT/mp157/stm32mp157d-atk-edgegateway.dtb"
install -m 0644 "$LINUX_OUT/.config" \
    "$ARTIFACT_ROOT/mp157/kernel.config"
tar -C "$MODULES_STAGE" -czf \
    "$ARTIFACT_ROOT/mp157/kernel-modules.tar.gz" .

echo "STM32MP157 build complete: $ARTIFACT_ROOT/mp157"
