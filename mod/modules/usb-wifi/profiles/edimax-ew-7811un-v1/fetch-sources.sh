#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

destination=${1:-sources}
mkdir -p "$destination"

fetch()
{
    expected=$1
    url=$2
    output=$3
    temporary=$output.tmp.$$
    trap 'rm -f "$temporary"' EXIT HUP INT TERM
    curl -fL --retry 3 -o "$temporary" "$url"
    actual=$(sha256sum "$temporary" | awk '{print $1}')
    [ "$actual" = "$expected" ] || {
        echo "checksum mismatch for $url: $actual" >&2
        exit 1
    }
    mv "$temporary" "$output"
    trap - EXIT HUP INT TERM
}

fetch \
    6327de71c544c27fe909d509a3d5605d46b94c102a3c27700f812b95cbe74254 \
    https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/rtlwifi/rtl8192cufw.bin \
    "$destination/rtl8192cufw.bin"
fetch \
    a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e \
    https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/LICENSES/LICENCE.rtlwifi_firmware.txt \
    "$destination/LICENCE.rtlwifi_firmware.txt"
