#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BSP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BASE="${1:-$HOME/edgegateway_bsp}"
SOURCE_ROOT="${SOURCE_ROOT:-$BASE/src}"
BUILD_ROOT="${BUILD_ROOT:-$BASE/build}"
ARTIFACT_ROOT="${ARTIFACT_ROOT:-$BASE/artifacts}"
UBOOT_SRC="${IMX6ULL_UBOOT_SRC:-$SOURCE_ROOT/imx6ull-uboot}"
LINUX_SRC="${IMX6ULL_LINUX_SRC:-$SOURCE_ROOT/imx6ull-linux}"
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

restore_old_kconfig_generated_files() {
    local name
    for name in zconf.hash.c zconf.lex.c zconf.tab.c; do
        cp "$LINUX_SRC/scripts/kconfig/${name}_shipped" \
            "$LINUX_SRC/scripts/kconfig/$name"
    done
}

restore_old_uboot_kconfig_generated_files() {
    local name
    for name in zconf.hash.c zconf.lex.c zconf.tab.c; do
        cp "$UBOOT_SRC/scripts/kconfig/${name}_shipped" \
            "$UBOOT_SRC/scripts/kconfig/$name"
    done
}

apply_linux_patches() {
    local patch_file
    local patch_dir="$BSP_DIR/patches/imx6ull_linux"

    [[ -d "$patch_dir" ]] || return 0
    while IFS= read -r -d '' patch_file; do
        if patch -d "$LINUX_SRC" -p1 --dry-run --silent < "$patch_file"; then
            patch -d "$LINUX_SRC" -p1 < "$patch_file"
        elif patch -d "$LINUX_SRC" -R -p1 --dry-run --silent < "$patch_file"; then
            echo "Kernel patch already applied: $(basename "$patch_file")"
        else
            echo "Kernel patch cannot be applied cleanly: $patch_file" >&2
            exit 1
        fi
    done < <(find "$patch_dir" -maxdepth 1 -type f -name '*.patch' -print0 | sort -z)
}

build_uboot_variant() {
    local variant="$1"
    local defconfig="mx6ull_alientek_${variant}_defconfig"
    local output="$BUILD_ROOT/imx6ull-uboot-$variant"

    restore_old_uboot_kconfig_generated_files
    reset_build_dir "$output"
    make -C "$UBOOT_SRC" O="$output" ARCH=arm \
        CROSS_COMPILE=arm-linux-gnueabi- "$defconfig"
    make -C "$UBOOT_SRC" O="$output" ARCH=arm \
        CROSS_COMPILE=arm-linux-gnueabi- -j"$JOBS"
    install -d "$ARTIFACT_ROOT/imx6ull/$variant"
    install -m 0644 "$output/u-boot.imx" \
        "$ARTIFACT_ROOT/imx6ull/$variant/u-boot-$variant-edgegateway.imx"
}

build_linux_variant() {
    local variant="$1"
    local defconfig="imx_alientek_${variant}_defconfig"
    local dtb="imx6ull-alientek-${variant}.dtb"
    local target="$ARTIFACT_ROOT/imx6ull/$variant"
    local modules_stage="$BUILD_ROOT/imx6ull-modules-$variant"
    local kernel_release

    # This 4.1.15 vendor tree has incomplete O= support; build it in-tree.
    make -C "$LINUX_SRC" ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- mrproper
    rsync -a "$BSP_DIR/overrides/imx6ull_linux/" "$LINUX_SRC/"
    apply_linux_patches
    restore_old_kconfig_generated_files
    make -C "$LINUX_SRC" ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- "$defconfig"
    # The ALIENTEK board revisions use either an SMSC LAN87xx or a Micrel
    # KSZ80xx PHY.  They must be built-in because NFS root is needed before
    # loadable modules are available.
    "$LINUX_SRC/scripts/config" --file "$LINUX_SRC/.config" \
        --enable SMSC_PHY --enable MICREL_PHY
    make -C "$LINUX_SRC" ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- olddefconfig
    make -C "$LINUX_SRC" ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- \
        -j"$JOBS" zImage dtbs modules

    reset_build_dir "$modules_stage"
    make -C "$LINUX_SRC" ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- \
        INSTALL_MOD_PATH="$modules_stage" INSTALL_MOD_STRIP=1 DEPMOD=true \
        modules_install
    kernel_release="$(make -s -C "$LINUX_SRC" ARCH=arm \
        CROSS_COMPILE=arm-linux-gnueabi- kernelrelease)"
    : > "$modules_stage/lib/modules/$kernel_release/modules.builtin.modinfo"
    /sbin/depmod -b "$modules_stage" "$kernel_release"

    install -d "$target"
    install -m 0644 "$LINUX_SRC/arch/arm/boot/zImage" "$target/zImage"
    install -m 0644 "$LINUX_SRC/arch/arm/boot/dts/$dtb" \
        "$target/imx6ull-alientek-${variant}-edgegateway.dtb"
    install -m 0644 "$LINUX_SRC/.config" "$target/kernel.config"
    tar -C "$modules_stage" -czf "$target/kernel-modules.tar.gz" .
}

require_dir "$UBOOT_SRC"
require_dir "$LINUX_SRC"
command -v arm-linux-gnueabi-gcc >/dev/null

rsync -a "$BSP_DIR/overrides/imx6ull_uboot/" "$UBOOT_SRC/"
rsync -a "$BSP_DIR/overrides/imx6ull_linux/" "$LINUX_SRC/"
build_uboot_variant emmc
build_uboot_variant nand
build_linux_variant emmc
build_linux_variant nand

echo "i.MX6ULL eMMC and NAND builds complete: $ARTIFACT_ROOT/imx6ull"
