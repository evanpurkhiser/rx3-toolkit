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
    if [ "$actual" != "$expected" ]; then
        echo "checksum mismatch for $url: $actual" >&2
        exit 1
    fi
    mv "$temporary" "$output"
    trap - EXIT HUP INT TERM
}

fetch \
    20df7ae5154b3830355f8ab4269123a87affdea59fe74fe9292a91d0d7e17b2f \
    https://w1.fi/releases/wpa_supplicant-2.10.tar.gz \
    "$destination/wpa_supplicant-2.10.tar.gz"
fetch \
    9fe43ccbeeea72c653bdcf8c93332583135cda46a79507bfd0a483bb57f65939 \
    https://github.com/thom311/libnl/releases/download/libnl3_7_0/libnl-3.7.0.tar.gz \
    "$destination/libnl-3.7.0.tar.gz"
fetch \
    a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 \
    https://musl.libc.org/releases/musl-1.2.5.tar.gz \
    "$destination/musl-1.2.5.tar.gz"
fetch \
    6327de71c544c27fe909d509a3d5605d46b94c102a3c27700f812b95cbe74254 \
    https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/rtlwifi/rtl8192cufw.bin \
    "$destination/rtl8192cufw.bin"
fetch \
    a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e \
    https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/LICENSES/LICENCE.rtlwifi_firmware.txt \
    "$destination/LICENCE.rtlwifi_firmware.txt"
