#!/usr/bin/env bash
set -euo pipefail
# Usage: bash build_systemui.sh /absolute/vendor/systemui /absolute/build [sdk-env]
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "Usage: bash $0 /absolute/vendor/systemui /absolute/build [sdk-env]" >&2
    exit 2
fi
SOURCE=$(realpath "${1:?systemui source directory required}")
BUILD=$(realpath -m "${2:?separate build directory required}")
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
case "$BUILD/" in
    "$SOURCE/"*) echo 'Use a build directory outside the source tree' >&2; exit 2 ;;
esac
if [ ! -f "$SOURCE/systemui.pro" ]; then
    echo "Missing vendor project: $SOURCE/systemui.pro" >&2
    exit 2
fi
if [ "$#" -ge 3 ]; then
    SDK_ENV=$(realpath "$3")
    set +u
    source "$SDK_ENV"
    set -u
fi
QMAKE=${QMAKE:-qmake}
JOBS=${JOBS:-4}
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo 'JOBS must be a positive integer' >&2
    exit 2
fi
for tool in python3 "$QMAKE" make file tee; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build tool: $tool" >&2
        exit 2
    fi
done
mkdir -p "$BUILD" "$(dirname "$BUILD")/ui"
LOG="$BUILD/build-$(date -u +%Y%m%dT%H%M%SZ).log"
exec > >(tee -a "$LOG") 2>&1
trap 'result=$?; if [ "$result" -ne 0 ]; then echo "Build failed (exit $result). Log: $LOG" >&2; fi' EXIT
echo "Source: $SOURCE"
echo "Build: $BUILD"
echo "Log: $LOG"
"$QMAKE" -v
QT_VERSION=$("$QMAKE" -query QT_VERSION)
if [[ "$QT_VERSION" != 5.* ]]; then
    echo "The vendor systemui requires Qt 5; selected qmake reports $QT_VERSION" >&2
    exit 2
fi
python3 "$HERE/integrate_systemui.py" "$SOURCE"
cd "$BUILD"
"$QMAKE" "$SOURCE/systemui.pro"
make -j"$JOBS"
file "$BUILD/systemui"
echo "Built systemui successfully. Log: $LOG"
echo 'This script does not install anything. Check ARM architecture and Qt SDK compatibility before target installation.'
