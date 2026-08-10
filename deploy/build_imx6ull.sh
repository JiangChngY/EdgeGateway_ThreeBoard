#!/bin/sh
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 /path/to/environment-setup-cortexa7... [build-dir]" >&2
    exit 2
fi

SDK_ENV=$1
BUILD_DIR=${2:-build-imx6ull}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../imx6ull_aggregator" && pwd)

. "$SDK_ENV"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
qmake "$PROJECT_DIR/imx6ull_aggregator.pro"
make -j2
echo "built: $BUILD_DIR/edge-aggregator"

