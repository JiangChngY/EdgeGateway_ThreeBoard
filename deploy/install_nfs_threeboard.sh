#!/usr/bin/env bash
# Development VM deployment. Replaces executables atomically; never flashes boards.
set -euo pipefail
SOURCE=$(realpath "${1:?project source directory}")
DESKTOP=$(realpath "${2:?ARM systemui}")
COLLECTOR=$(realpath "${3:?ARM edge-aggregator}")
ROOT=$(realpath "${4:-/srv/nfs/edgegateway}")
test "$(id -u)" = 0
test "$ROOT" = /srv/nfs/edgegateway
for part in "$ROOT/mp157" "$ROOT/imx6ull" "$ROOT/mp157/opt" "$ROOT/mp157/opt/ui" \
    "$ROOT/imx6ull/opt" "$ROOT/imx6ull/etc/init.d" "$ROOT/mp157/etc/systemd/system"; do
    test -d "$part" && test ! -L "$part"
done
for board in mp157 imx6ull; do
    for part in opt/edge-gateway opt/edge-gateway/bin opt/edge-gateway/config opt/edge-gateway/data; do
        test ! -L "$ROOT/$board/$part"
    done
done
test ! -L "$ROOT/mp157/opt/ui/systemui"
test ! -L "$ROOT/mp157/opt/edge-gateway/config/desktop.ini"
test ! -L "$ROOT/imx6ull/opt/edge-gateway/config/imx6ull.ini"
python3 "$SOURCE/mp157_desktop/sdk/verify_abi.py" "$DESKTOP" "$ROOT/mp157" >/dev/null
python3 "$SOURCE/mp157_desktop/sdk/verify_abi.py" "$COLLECTOR" "$ROOT/imx6ull" >/dev/null
strings -el "$DESKTOP" | grep 'edgegateway.backend.v1' >/dev/null
if ! systemctl is-active --quiet edge-gateway-forward.service; then
    test "$(sysctl -n net.ipv4.ip_forward)" = 0
    test -z "$(nft list ruleset)" || { echo 'Inspect existing firewall before deploying the VM forwarding service' >&2; exit 2; }
fi
mkdir -p /srv/edgegateway-backups
BACKUP=$(mktemp -d /srv/edgegateway-backups/update-$(date +%Y%m%d-%H%M%S)-XXXXXX)
mkdir "$BACKUP/original"
systemctl is-enabled edge-gateway-forward.service > "$BACKUP/forward-enabled" 2>/dev/null || true
systemctl is-active edge-gateway-forward.service > "$BACKUP/forward-active" 2>/dev/null || true
backup() {
    local target=$1
    if [ -d "$target" ] && [ ! -L "$target" ]; then
        echo "Expected a file, found directory: $target" >&2; return 2
    fi
    if [ -e "$target" ] || [ -L "$target" ]; then
        cp -a --parents "$target" "$BACKUP/original"
        printf 'restore\t%s\n' "$target" >> "$BACKUP/files.tsv"
    else
        printf 'remove\t%s\n' "$target" >> "$BACKUP/files.tsv"
    fi
}
put() {
    local source=$1 target=$2 mode=$3 stage
    backup "$target"
    mkdir -p "$(dirname "$target")"
    stage=$(mktemp "$(dirname "$target")/.edge-stage.XXXXXX")
    install -m "$mode" "$source" "$stage"
    mv -f "$stage" "$target"
}
cat > "$BACKUP/rollback.sh" <<'ROLLBACK'
#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
test "$(id -u)" = 0
systemctl disable --now edge-gateway-forward.service || true
while IFS=$'\t' read -r action target; do
    case "$target" in /srv/nfs/edgegateway/*|/usr/local/sbin/edge-gateway-forward|/etc/systemd/system/edge-gateway-forward.service) ;; *) exit 2;; esac
    if [ "$action" = restore ]; then
        # A restored symlink must replace the installed link, not follow it.
        rm -f -- "$target"
        cp -a -- "$HERE/original$target" "$target"
    else
        rm -f -- "$target"
    fi
done < "$HERE/files.tsv"
systemctl daemon-reload
if grep -qx enabled "$HERE/forward-enabled"; then systemctl enable edge-gateway-forward.service; fi
if grep -qx active "$HERE/forward-active"; then systemctl start edge-gateway-forward.service; fi
echo 'Files restored. Restart board applications after rollback; data databases were retained.'
ROLLBACK
chmod 700 "$BACKUP/rollback.sh"
trap 'trap - ERR; echo "Deployment failed; restoring from $BACKUP" >&2; "$BACKUP/rollback.sh"; exit 1' ERR
put "$DESKTOP" "$ROOT/mp157/opt/ui/systemui" 755
put "$COLLECTOR" "$ROOT/imx6ull/opt/edge-gateway/bin/edge-aggregator" 755
if [ ! -e "$ROOT/imx6ull/opt/edge-gateway/config/imx6ull.ini" ]; then
    put "$SOURCE/config/imx6ull.ini" "$ROOT/imx6ull/opt/edge-gateway/config/imx6ull.ini" 644
fi
backup "$ROOT/mp157/opt/edge-gateway/config/desktop.ini"
python3 - "$ROOT/mp157/opt/edge-gateway/config/desktop.ini" <<'PY'
import configparser, os, sys, tempfile
from pathlib import Path
p = Path(sys.argv[1]); p.parent.mkdir(parents=True, exist_ok=True)
c = configparser.ConfigParser(); c.read(p)
for section, values in {'serial': {'port':'/dev/ttyUSB0','auto_open':'true'},
                        'storage': {'database':'/opt/edge-gateway/data/desktop.db'},
                        'uplink': {'host':'192.168.138.3','port':'9000','enabled':'true'}}.items():
    if not c.has_section(section): c.add_section(section)
    for key, value in values.items():
        if not c.has_option(section, key): c.set(section, key, value)
with tempfile.NamedTemporaryFile(mode='w', dir=p.parent, delete=False) as f:
    c.write(f); tmp=f.name
os.chmod(tmp, 0o644); os.replace(tmp,p)
PY
put "$SOURCE/deploy/edge-aggregator.init" "$ROOT/imx6ull/etc/init.d/edge-aggregator" 755
for level in 2 3 4 5; do
    link="$ROOT/imx6ull/etc/rc$level.d/S95edge-aggregator"
    backup "$link"; ln -sfn ../init.d/edge-aggregator "$link"
done
for level in 0 1 6; do
    link="$ROOT/imx6ull/etc/rc$level.d/K05edge-aggregator"
    backup "$link"; ln -sfn ../init.d/edge-aggregator "$link"
done
put "$SOURCE/deploy/edge-peer-route.service" "$ROOT/mp157/etc/systemd/system/edge-peer-route.service" 644
link="$ROOT/mp157/etc/systemd/system/multi-user.target.wants/edge-peer-route.service"
backup "$link"; mkdir -p "$(dirname "$link")"; ln -sfn ../edge-peer-route.service "$link"
put "$SOURCE/deploy/edge-gateway-forward.sh" /usr/local/sbin/edge-gateway-forward 755
put "$SOURCE/deploy/edge-gateway-forward.service" /etc/systemd/system/edge-gateway-forward.service 644
systemctl daemon-reload
systemctl enable --now edge-gateway-forward.service
sha256sum "$ROOT/mp157/opt/ui/systemui" "$ROOT/imx6ull/opt/edge-gateway/bin/edge-aggregator"
echo "BACKUP=$BACKUP"
echo 'Installed in NFS roots. Board boot/touch/physical sensors still require hardware acceptance.'
