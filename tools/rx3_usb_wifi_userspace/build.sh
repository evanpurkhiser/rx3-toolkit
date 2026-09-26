#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

usage()
{
    echo "usage: build.sh SOURCE_DIRECTORY [OUTPUT_DIRECTORY]" >&2
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage
    exit 2
fi

source_directory=$(realpath "$1")
output_directory=$(realpath -m "${2:-./out}")
tool_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
image=localhost/rx3-usb-wifi-builder:bookworm

for source in wpa_supplicant-2.10.tar.gz libnl-3.7.0.tar.gz musl-1.2.5.tar.gz \
    rtl8192cufw.bin LICENCE.rtlwifi_firmware.txt; do
    test -f "$source_directory/$source" || {
        echo "missing source: $source_directory/$source" >&2
        exit 1
    }
done

mkdir -p "$output_directory"
podman run --rm --network=none \
    -v "$source_directory:/sources:ro" \
    -v "$tool_directory:/tool:ro" \
    -v "$output_directory:/out:rw" \
    "$image" sh -eu -c '
        work=$(mktemp -d)
        trap "rm -rf $work" EXIT HUP INT TERM
        cd "$work"
        tar -xzf /sources/libnl-3.7.0.tar.gz
        tar -xzf /sources/musl-1.2.5.tar.gz
        tar -xzf /sources/wpa_supplicant-2.10.tar.gz

        cd musl-1.2.5
        CC=arm-linux-gnueabi-gcc \
        CROSS_COMPILE=arm-linux-gnueabi- \
        CFLAGS="-Os -march=armv7-a -marm -mfloat-abi=softfp -fno-ident" \
            ./configure \
                --target=arm-linux-musleabi \
                --prefix="$work/musl-prefix" \
                --disable-shared >/dev/null
        make -j2 >/dev/null
        make install >/dev/null
        cp -a /usr/arm-linux-gnueabi/include/linux "$work/musl-prefix/include/"
        cp -a /usr/arm-linux-gnueabi/include/asm "$work/musl-prefix/include/"
        cp -a /usr/arm-linux-gnueabi/include/asm-generic "$work/musl-prefix/include/"

        cd "$work/libnl-3.7.0"
        CC="$work/musl-prefix/bin/musl-gcc" \
        AR=arm-linux-gnueabi-ar \
        RANLIB=arm-linux-gnueabi-ranlib \
        CFLAGS="-Os -march=armv7-a -marm -mfloat-abi=softfp" \
            ./configure \
            --host=arm-linux-musleabi \
            --prefix="$work/prefix" \
            --enable-static \
            --disable-shared \
            --disable-cli >/dev/null
        make -j2 >/dev/null
        make install >/dev/null

        cd "$work/wpa_supplicant-2.10/wpa_supplicant"
        cat > .config <<EOF
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_CTRL_IFACE=y
CONFIG_BACKEND=file
CONFIG_TLS=internal
CONFIG_INTERNAL_LIBTOMMATH=y
CONFIG_CRYPTO=internal
CONFIG_SHA256=y
CFLAGS += -Os -march=armv7-a -marm -mfloat-abi=softfp -ffunction-sections -fdata-sections -I$work/prefix/include/libnl3
LDFLAGS += -static -Wl,--gc-sections -L$work/prefix/lib
EOF
        make -j2 \
            CC="$work/musl-prefix/bin/musl-gcc" \
            AR=arm-linux-gnueabi-ar \
            RANLIB=arm-linux-gnueabi-ranlib \
            PKG_CONFIG_LIBDIR="$work/prefix/lib/pkgconfig" \
            wpa_supplicant wpa_cli >/dev/null
        arm-linux-gnueabi-strip wpa_supplicant wpa_cli

        "$work/musl-prefix/bin/musl-gcc" \
            -Os -march=armv7-a -marm -mfloat-abi=softfp \
            -static -Wl,--gc-sections -o /out/rx3-ifrename /tool/ifrename.c
        arm-linux-gnueabi-strip /out/rx3-ifrename

        cp wpa_supplicant /out/
        cp wpa_cli /out/
        mkdir -p /out/rtlwifi
        cp /sources/rtl8192cufw.bin /out/rtlwifi/
        cp /sources/LICENCE.rtlwifi_firmware.txt /out/
        mkdir -p /out/licenses
        cp "$work/wpa_supplicant-2.10/COPYING" \
            /out/licenses/COPYING.wpa_supplicant
        cp "$work/libnl-3.7.0/COPYING" /out/licenses/COPYING.libnl
        cp "$work/musl-1.2.5/COPYRIGHT" /out/licenses/COPYRIGHT.musl

        readelf -h /out/wpa_supplicant | grep -q "Machine:.*ARM"
        ! readelf -l /out/wpa_supplicant | grep -q "Requesting program interpreter"
        readelf -h /out/wpa_cli | grep -q "Machine:.*ARM"
        ! readelf -l /out/wpa_cli | grep -q "Requesting program interpreter"
        readelf -h /out/rx3-ifrename | grep -q "Machine:.*ARM"
        ! readelf -l /out/rx3-ifrename | grep -q "Requesting program interpreter"
    '

sha256sum \
    "$output_directory/wpa_supplicant" \
    "$output_directory/wpa_cli" \
    "$output_directory/rx3-ifrename" \
    "$output_directory/rtlwifi/rtl8192cufw.bin" \
    "$output_directory/LICENCE.rtlwifi_firmware.txt" \
    "$output_directory/licenses/COPYING.wpa_supplicant" \
    "$output_directory/licenses/COPYING.libnl" \
    "$output_directory/licenses/COPYRIGHT.musl"
