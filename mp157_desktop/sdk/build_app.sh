#!/usr/bin/env bash
set -euo pipefail
SDK_ROOT=${EDGE_SDK_ROOT:-$HOME/edgegateway_desktop_sdk}
SOURCE=${1:-$HOME/edgegateway_desktop}
PROJECT=${2:-edge-desktop.pro}
BUILD=${3:-$SDK_ROOT/build-desktop}
BINARY=${4:-edge-desktop}
mkdir -p "$BUILD"
cd "$BUILD"
"$SDK_ROOT/bin/qmake" "$SOURCE/$PROJECT" CONFIG+=release CONFIG-=debug \
    'QMAKE_LFLAGS+=-Wl,-Map,edge-desktop.map -Wl,-t'
make -j2
CHAIN="$SDK_ROOT/toolchains/gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf"
"$CHAIN-readelf" -h "./$BINARY"
"$CHAIN-readelf" -A "./$BINARY"
"$CHAIN-readelf" --version-info "./$BINARY"
"$CHAIN-readelf" -d "./$BINARY"
