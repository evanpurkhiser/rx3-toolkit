#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "Run this script through sudo so tcpdump can open the interfaces." >&2
    exit 1
fi

if ! command -v tcpdump >/dev/null; then
    echo "tcpdump is required." >&2
    exit 1
fi

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
stamp=${1:-$(date -u +%Y%m%dT%H%M%SZ)}
capture_dir="$repo_root/captures/link-export-$stamp"
usb_mac=c8:3d:fc:16:af:9a

find_interface_by_mac() {
    local address_file

    for address_file in /sys/class/net/*/address; do
        if [[ $(<"$address_file") == "$usb_mac" ]]; then
            basename "$(dirname "$address_file")"
            return 0
        fi
    done

    return 1
}

usb_interface=$(find_interface_by_mac) || {
    echo "RX3 USB interface with MAC $usb_mac is not present." >&2
    exit 1
}

interfaces=(lan0 rx3lan "$usb_interface")
pids=()

mkdir -p "$capture_dir"

stop_captures() {
    local pid

    trap - INT TERM EXIT

    for pid in "${pids[@]}"; do
        kill -INT "$pid" 2>/dev/null || true
    done

    wait || true

    if [[ -n ${SUDO_USER:-} ]]; then
        chown -R "$SUDO_USER" "$capture_dir"
    fi

    echo "Captures saved in $capture_dir"
}

trap stop_captures INT TERM EXIT

for interface in "${interfaces[@]}"; do
    if [[ ! -e /sys/class/net/$interface ]]; then
        echo "Required interface $interface is not present." >&2
        exit 1
    fi

    tcpdump \
        -i "$interface" \
        -n \
        -U \
        -s 0 \
        -B 4096 \
        -w "$capture_dir/server-$interface.pcap" &
    pids+=("$!")
done

cat >"$capture_dir/session.txt" <<EOF
started_utc=$(date -u --iso-8601=ns)
lan_interface=lan0
lan_output_interface=rx3lan
usb_interface=$usb_interface
usb_mac=$usb_mac
EOF

echo "Server captures armed in $capture_dir"
echo "Press Ctrl-C only after the complete Rekordbox experiment."

wait
