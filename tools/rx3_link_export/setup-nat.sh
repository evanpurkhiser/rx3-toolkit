#!/bin/sh
set -eu

: "${LAN_INTERFACE:=lan0}"
: "${USB_INTERFACE:?set USB_INTERFACE to the RX3 USB network interface}"
: "${REKORDBOX_IP:?set REKORDBOX_IP to the Mac running rekordbox}"
: "${RX3_IP:?set RX3_IP to the RX3 link-local address}"
: "${LAN_RX3_IP:=10.0.0.253}"
: "${USB_REKORDBOX_IP:=169.254.100.1}"

ip address replace "$LAN_RX3_IP/32" dev "$LAN_INTERFACE"
ip address replace "$USB_REKORDBOX_IP/16" dev "$USB_INTERFACE"
ip link set "$USB_INTERFACE" up
sysctl -q -w net.ipv4.ip_forward=1
sysctl -q -w "net.ipv4.conf.$LAN_INTERFACE.rp_filter=0"
sysctl -q -w "net.ipv4.conf.$USB_INTERFACE.rp_filter=0"

nft delete table ip rx3_link_export 2>/dev/null || true
nft -f - <<EOF
table ip rx3_link_export {
    chain prerouting {
        type nat hook prerouting priority dstnat; policy accept;
        iifname "$LAN_INTERFACE" ip saddr $REKORDBOX_IP ip daddr $LAN_RX3_IP dnat to $RX3_IP
        iifname "$USB_INTERFACE" ip saddr $RX3_IP ip daddr $USB_REKORDBOX_IP dnat to $REKORDBOX_IP
    }

    chain postrouting {
        type nat hook postrouting priority srcnat; policy accept;
        oifname "$USB_INTERFACE" ip saddr $REKORDBOX_IP ip daddr $RX3_IP snat to $USB_REKORDBOX_IP
        oifname "$LAN_INTERFACE" ip saddr $RX3_IP ip daddr $REKORDBOX_IP snat to $LAN_RX3_IP
    }
}
EOF

iptables -C FORWARD -i "$LAN_INTERFACE" -o "$USB_INTERFACE" \
    -s "$REKORDBOX_IP" -d "$RX3_IP" -j ACCEPT 2>/dev/null || \
iptables -I FORWARD 1 -i "$LAN_INTERFACE" -o "$USB_INTERFACE" \
    -s "$REKORDBOX_IP" -d "$RX3_IP" -j ACCEPT
iptables -C FORWARD -i "$USB_INTERFACE" -o "$LAN_INTERFACE" \
    -s "$RX3_IP" -d "$REKORDBOX_IP" -j ACCEPT 2>/dev/null || \
iptables -I FORWARD 1 -i "$USB_INTERFACE" -o "$LAN_INTERFACE" \
    -s "$RX3_IP" -d "$REKORDBOX_IP" -j ACCEPT

echo "RX3 Link Export NAT ready: $REKORDBOX_IP <-> $RX3_IP via $LAN_RX3_IP / $USB_REKORDBOX_IP"
