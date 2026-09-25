#!/bin/sh
set -eu

: "${LAN_INTERFACE:=lan0}"
: "${LAN_RX3_INTERFACE:=rx3lan}"
: "${USB_INTERFACE:?set USB_INTERFACE to the RX3 USB network interface}"
: "${REKORDBOX_IP:?set REKORDBOX_IP to the Mac running rekordbox}"
: "${RX3_IP:=169.254.100.2}"
: "${RX3_PRIMARY_IP:=169.254.175.153}"
: "${LAN_RX3_IP:=10.0.0.253}"
: "${USB_REKORDBOX_IP:=169.254.100.1}"

iptables -D FORWARD -i "$LAN_RX3_INTERFACE" -o "$USB_INTERFACE" \
    -s "$REKORDBOX_IP" -d "$RX3_IP" -j ACCEPT 2>/dev/null || true
iptables -D FORWARD -i "$USB_INTERFACE" -o "$LAN_INTERFACE" \
    -s "$RX3_IP" -d "$REKORDBOX_IP" -j ACCEPT 2>/dev/null || true
iptables -D FORWARD -i "$USB_INTERFACE" -o "$LAN_INTERFACE" \
    -s "$RX3_PRIMARY_IP" -d "$REKORDBOX_IP" -j ACCEPT 2>/dev/null || true
nft delete table ip rx3_link_export 2>/dev/null || true
ip address del "$LAN_RX3_IP/32" dev "$LAN_RX3_INTERFACE" 2>/dev/null || true
ip link delete "$LAN_RX3_INTERFACE" 2>/dev/null || true
ip address del "$USB_REKORDBOX_IP/16" dev "$USB_INTERFACE" 2>/dev/null || true

echo "RX3 Link Export NAT removed"
