#!/usr/bin/env bash
# Isolated application SDK: never installs or modifies the target NFS runtime.
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SDK_ROOT=${EDGE_SDK_ROOT:-$HOME/edgegateway_desktop_sdk}
case "$SDK_ROOT" in "$HOME/edgegateway_desktop_sdk") ;; *) echo 'Unexpected SDK root' >&2; exit 2;; esac
mkdir -p "$SDK_ROOT/downloads" "$SDK_ROOT/toolchains" "$SDK_ROOT/logs"
PACKAGE=gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf
URL=https://developer.arm.com/-/media/Files/downloads/gnu-a/9.2-2019.12/binrel
cd "$SDK_ROOT/downloads"
if ! test -f "$PACKAGE.tar.xz"; then
    python3 "$SCRIPT_DIR/download_toolchain.py" "$SDK_ROOT/downloads"
fi
curl --fail --location --retry 3 --connect-timeout 20 --max-time 90 \
    "$URL/$PACKAGE.tar.xz.asc" -o "$PACKAGE.tar.xz.asc"
md5sum --check "$PACKAGE.tar.xz.asc"
printf '%s  %s\n' 51bbaf22a4d3e7a393264c4ef1e45566701c516274dde19c4892c911caa85617 "$PACKAGE.tar.xz" | sha256sum --check
xz --test "$PACKAGE.tar.xz"
if ! test -x "$SDK_ROOT/toolchains/$PACKAGE/bin/arm-none-linux-gnueabihf-g++"; then
    tar -xJf "$PACKAGE.tar.xz" -C "$SDK_ROOT/toolchains"
fi
"$SDK_ROOT/toolchains/$PACKAGE/bin/arm-none-linux-gnueabihf-g++" --version
python3 "$SCRIPT_DIR/prepare_sdk.py" "$SDK_ROOT"
