#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

usage()
{
    echo "usage: build.sh PROFILE SOURCE_DIRECTORY [OUTPUT_DIRECTORY]" >&2
}

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    usage
    exit 2
fi

profile=$1
tool_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$tool_directory/../.." && pwd)
profile_directory=$repository_root/mod/modules/usb-wifi/profiles/$profile
source_directory=$(realpath "$2")
output_directory=$(realpath -m \
    "${3:-$repository_root/build/artifacts/1.19/usb-wifi/$profile}")
docker=${DOCKER:-docker}
image=rx3-usb-wifi-builder:bookworm

test -d "$profile_directory" || {
    echo "unknown USB Wi-Fi profile: $profile" >&2
    exit 1
}

for source in wpa_supplicant-2.10.tar.gz libnl-3.7.0.tar.gz musl-1.2.5.tar.gz; do
    test -f "$source_directory/$source" || {
        echo "missing source: $source_directory/$source" >&2
        exit 1
    }
done

(cd "$source_directory" && sha256sum -c "$tool_directory/sources.sha256")
if test -s "$profile_directory/sources.sha256"; then
    (cd "$source_directory" && sha256sum -c "$profile_directory/sources.sha256")
fi

mkdir -p "$output_directory"
set -- --user "$(id -u):$(id -g)"
case "${docker##*/}" in
    podman) set -- --userns=keep-id "$@" ;;
esac
"$docker" run --rm --network=none \
    "$@" \
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
        readelf -h /out/wpa_supplicant | grep -q "Machine:.*ARM"
        ! readelf -l /out/wpa_supplicant | grep -q "Requesting program interpreter"
        readelf -h /out/wpa_cli | grep -q "Machine:.*ARM"
        ! readelf -l /out/wpa_cli | grep -q "Requesting program interpreter"
        readelf -h /out/rx3-ifrename | grep -q "Machine:.*ARM"
        ! readelf -l /out/rx3-ifrename | grep -q "Requesting program interpreter"
    '

while read -r source target; do
    case "$source" in ''|\#*) continue ;; esac
    case "$source:$target" in
        *..*|/*:*|*:/*) echo "unsafe profile artifact path" >&2; exit 1 ;;
    esac
    mkdir -p "$output_directory/$(dirname "$target")"
    cp "$source_directory/$source" "$output_directory/$target"
done < "$profile_directory/artifacts.list"

cp "$profile_directory/profile.json" "$profile_directory/modules.load" \
    "$profile_directory/firmware.list" "$profile_directory/usb.ids" \
    "$profile_directory/supplicant.driver" "$output_directory/"

(cd "$output_directory" && sha256sum -c "$tool_directory/artifacts.sha256")
