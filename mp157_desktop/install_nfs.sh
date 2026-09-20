#!/usr/bin/env bash
set -euo pipefail
# Install into an OFFLINE MP157 NFS root. The desktop process must be stopped.
ROOT=$(realpath "${1:?NFS root required}")
BIN=$(realpath "${2:?cross-compiled systemui required}")
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
if [ "$ROOT" = / ] || [ ! -d "$ROOT/etc" ] || [ ! -d "$ROOT/opt/ui" ]; then
    echo 'Expected the ALIENTEK NFS root with etc and opt/ui' >&2; exit 2
fi
HEADER=$(LC_ALL=C readelf -h "$BIN")
if ! grep -Eq 'Class:.*ELF32' <<< "$HEADER" ||
   ! grep -Eq 'Machine:.*ARM$' <<< "$HEADER" ||
   ! grep -Eq 'Type:.*(EXEC|DYN)' <<< "$HEADER" ||
   ! grep -q 'hard-float ABI' <<< "$HEADER"; then
    echo 'Refusing to install a non-ARM systemui binary' >&2; exit 2
fi
if ! LC_ALL=C readelf -l "$BIN" | grep 'interpreter: /lib/ld-linux-armhf.so.3' >/dev/null; then
    echo 'Expected an ARM hard-float Linux executable with the board loader' >&2; exit 2
fi
if ! strings -el "$BIN" | grep '^edgegateway.backend.v1$' >/dev/null; then
    echo 'Missing EdgeGateway desktop integration identity' >&2; exit 2
fi
if [ ! -f "$ROOT/opt/ui/systemui" ] || [ -L "$ROOT/opt/ui/systemui" ]; then
    echo 'Expected regular vendor /opt/ui/systemui; inspect actual startup path first' >&2; exit 2
fi
for part in opt opt/ui opt/edge-gateway opt/edge-gateway/config opt/edge-gateway/data opt/edge-gateway/config/desktop.ini; do
    if [ -L "$ROOT/$part" ]; then echo "Refusing symlink: $part" >&2; exit 2; fi
done
python3 "$HERE/sdk/verify_abi.py" "$BIN" "$ROOT"
BACKUP=$(mktemp "$ROOT/opt/ui/systemui.before-edgegateway.$(date +%Y%m%d-%H%M%S).XXXXXX")
cp -p "$ROOT/opt/ui/systemui" "$BACKUP"
STAGED=$(mktemp "$ROOT/opt/ui/systemui.edge-new.XXXXXX")
install -m 0755 "$BIN" "$STAGED"
mv "$STAGED" "$ROOT/opt/ui/systemui"
install -d "$ROOT/opt/edge-gateway/config" "$ROOT/opt/edge-gateway/data"
if [ ! -e "$ROOT/opt/edge-gateway/config/desktop.ini" ]; then
    install -m 0644 "$HERE/desktop.ini" "$ROOT/opt/edge-gateway/config/desktop.ini"
fi
echo "Installed. Previous desktop: $BACKUP"
echo 'Start the board using the existing TFTP/NFS configuration.'
