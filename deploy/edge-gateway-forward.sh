#!/bin/sh
# Development VM only: allow forwarding solely between the two board IPs.
set -eu
case "${1:-start}" in
start)
    ip -4 addr show dev ens37 | grep -q '192.168.137.2/24'
    ip -4 addr show dev ens38 | grep -q '192.168.138.2/24'
    {
        if nft list table inet edge_gateway >/dev/null 2>&1; then
            echo 'delete table inet edge_gateway'
        fi
        cat <<'RULES'
table inet edge_gateway {
    chain forward {
        type filter hook forward priority 0; policy drop;
        iifname "ens37" oifname "ens38" ip saddr 192.168.137.3 ip daddr 192.168.138.3 accept
        iifname "ens38" oifname "ens37" ip saddr 192.168.138.3 ip daddr 192.168.137.3 accept
    }
}
RULES
    } | nft -f -
    sysctl -w net.ipv4.ip_forward=1
    ;;
stop)
    # This development VM had forwarding disabled before installation.
    sysctl -w net.ipv4.ip_forward=0
    if nft list table inet edge_gateway >/dev/null 2>&1; then
        nft delete table inet edge_gateway
    fi
    ;;
*) exit 2 ;;
esac
