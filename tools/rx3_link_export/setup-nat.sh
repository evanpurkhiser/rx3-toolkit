#!/bin/sh
set -eu

: "${LAN_INTERFACE:=lan0}"
: "${LAN_RX3_INTERFACE:=rx3lan}"
: "${USB_INTERFACE:?set USB_INTERFACE to the RX3 USB network interface}"
: "${REKORDBOX_IP:?set REKORDBOX_IP to the Mac running rekordbox}"
: "${RX3_IP:=169.254.100.2}"
: "${RX3_PRIMARY_IP:=169.254.175.153}"
: "${LAN_RX3_IP:=10.0.0.253}"
: "${LAN_RX3_MAC:=c8:3d:fc:16:af:99}"
: "${USB_REKORDBOX_IP:=169.254.100.1}"
: "${LAN_BROADCAST:=10.0.0.255}"
: "${DBSERVER_TRANSPORT:=broker}"

case "$DBSERVER_TRANSPORT" in
    broker)
        DBSERVER_NAT_RULES=
        ;;
    nat)
        DBSERVER_NAT_RULES="
        iifname \"$LAN_RX3_INTERFACE\" ip saddr $REKORDBOX_IP ip daddr $LAN_RX3_IP meta l4proto tcp dnat to $RX3_IP
        iifname \"$USB_INTERFACE\" ip saddr { $RX3_IP, $RX3_PRIMARY_IP } ip daddr $USB_REKORDBOX_IP meta l4proto tcp dnat to $REKORDBOX_IP"
        ;;
    *)
        echo "DBSERVER_TRANSPORT must be broker or nat" >&2
        exit 2
        ;;
esac

ip link show dev "$LAN_RX3_INTERFACE" >/dev/null 2>&1 ||
    ip link add link "$LAN_INTERFACE" name "$LAN_RX3_INTERFACE" \
        address "$LAN_RX3_MAC" type macvlan mode bridge
ip link set "$LAN_RX3_INTERFACE" up
ip -4 address show dev "$LAN_RX3_INTERFACE" |
    grep -q "inet $LAN_RX3_IP/32 " ||
    ip address add "$LAN_RX3_IP/32" dev "$LAN_RX3_INTERFACE"
ip -4 address show dev "$USB_INTERFACE" |
    grep -q "inet $USB_REKORDBOX_IP/16 " ||
    ip address add "$USB_REKORDBOX_IP/16" dev "$USB_INTERFACE"
ip link set "$USB_INTERFACE" up
sysctl -q -w net.ipv4.ip_forward=1
sysctl -q -w "net.ipv4.conf.$LAN_INTERFACE.rp_filter=2"
sysctl -q -w "net.ipv4.conf.$LAN_RX3_INTERFACE.rp_filter=2"
sysctl -q -w "net.ipv4.conf.$USB_INTERFACE.rp_filter=2"

nft delete table ip rx3_link_export 2>/dev/null || true
nft -f - <<EOF
table ip rx3_link_export {
    chain prerouting {
        type nat hook prerouting priority dstnat; policy accept;
        iifname "$LAN_RX3_INTERFACE" ip saddr $REKORDBOX_IP ip daddr $LAN_RX3_IP udp dport != { 50000, 50001, 50002 } dnat to $RX3_IP
        iifname "$USB_INTERFACE" ip saddr { $RX3_IP, $RX3_PRIMARY_IP } ip daddr $USB_REKORDBOX_IP udp dport != { 50000, 50001, 50002 } dnat to $REKORDBOX_IP
$DBSERVER_NAT_RULES
    }

    chain postrouting {
        type nat hook postrouting priority srcnat; policy accept;
        oifname "$LAN_INTERFACE" ip daddr $LAN_BROADCAST udp dport { 50000, 50001, 50002 } snat to $LAN_RX3_IP
        oifname "$USB_INTERFACE" ip saddr $REKORDBOX_IP ip daddr $RX3_IP snat to $USB_REKORDBOX_IP
        oifname "$LAN_INTERFACE" ip saddr { $RX3_IP, $RX3_PRIMARY_IP } ip daddr $REKORDBOX_IP snat to $LAN_RX3_IP
    }
}
EOF

iptables -C FORWARD -i "$LAN_RX3_INTERFACE" -o "$USB_INTERFACE" \
    -s "$REKORDBOX_IP" -d "$RX3_IP" -j ACCEPT 2>/dev/null || \
iptables -I FORWARD 1 -i "$LAN_RX3_INTERFACE" -o "$USB_INTERFACE" \
    -s "$REKORDBOX_IP" -d "$RX3_IP" -j ACCEPT
if [ "$DBSERVER_TRANSPORT" = nat ]; then
    iptables -C FORWARD -i "$LAN_RX3_INTERFACE" -o "$USB_INTERFACE" \
        -s "$REKORDBOX_IP" -d "$RX3_PRIMARY_IP" -j ACCEPT 2>/dev/null || \
    iptables -I FORWARD 1 -i "$LAN_RX3_INTERFACE" -o "$USB_INTERFACE" \
        -s "$REKORDBOX_IP" -d "$RX3_PRIMARY_IP" -j ACCEPT
fi
iptables -C FORWARD -i "$USB_INTERFACE" -o "$LAN_INTERFACE" \
    -s "$RX3_IP" -d "$REKORDBOX_IP" -j ACCEPT 2>/dev/null || \
iptables -I FORWARD 1 -i "$USB_INTERFACE" -o "$LAN_INTERFACE" \
    -s "$RX3_IP" -d "$REKORDBOX_IP" -j ACCEPT
iptables -C FORWARD -i "$USB_INTERFACE" -o "$LAN_INTERFACE" \
    -s "$RX3_PRIMARY_IP" -d "$REKORDBOX_IP" -j ACCEPT 2>/dev/null || \
iptables -I FORWARD 1 -i "$USB_INTERFACE" -o "$LAN_INTERFACE" \
    -s "$RX3_PRIMARY_IP" -d "$REKORDBOX_IP" -j ACCEPT

echo "RX3 Link Export NAT ready: $REKORDBOX_IP <-> $RX3_IP via $LAN_RX3_IP / $USB_REKORDBOX_IP (dbserver: $DBSERVER_TRANSPORT)"
