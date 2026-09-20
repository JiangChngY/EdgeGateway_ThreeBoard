#!/usr/bin/env bash
set -euo pipefail
SDK_ROOT=$HOME/edgegateway_desktop_sdk
ROOT="$SDK_ROOT/sysroot"
APP=${1:-$SDK_ROOT/build-desktop/edge-desktop}
mkdir -p "$SDK_ROOT/smoke/run" "$SDK_ROOT/smoke/cache"
chmod 700 "$SDK_ROOT/smoke/run"
cd "$SDK_ROOT/smoke"
qemu-arm -L "$ROOT" "$ROOT/lib/ld-linux-armhf.so.3" \
    --library-path "$ROOT/lib:$ROOT/usr/lib:$ROOT/vendor/lib" --list "$APP"
xvfb-run -a -s '-screen 0 1280x800x24 -nolisten tcp' timeout 45s qemu-arm -L "$ROOT" \
    -E LD_LIBRARY_PATH="$ROOT/lib:$ROOT/usr/lib:$ROOT/vendor/lib" \
    -E LD_BIND_NOW=1 \
    -E QT_PLUGIN_PATH="$ROOT/usr/lib/plugins" \
    -E QML2_IMPORT_PATH="$ROOT/usr/lib/qml" \
    -E QT_QPA_PLATFORM=xcb \
    -E QT_X11_NO_MITSHM=1 -E QT_XCB_GL_INTEGRATION=none \
    -E QT_QUICK_BACKEND=software \
    -E QT_QPA_FONTDIR="$ROOT/usr/share/fonts" \
    -E FONTCONFIG_FILE="$ROOT/etc/fonts/fonts.conf" \
    -E XDG_RUNTIME_DIR="$SDK_ROOT/smoke/run" \
    -E XDG_CACHE_HOME="$SDK_ROOT/smoke/cache" \
    -E EDGE_DESKTOP_CONFIG="$SDK_ROOT/scripts/smoke.ini" \
    -E EDGE_CAPTURE="$SDK_ROOT/smoke/edge-desktop-arm.png" \
    "$APP" -platform xcb
test -s "$SDK_ROOT/smoke/edge-desktop-arm.png"
echo QEMU_ARM_XVFB_CAPTURE_PASS
