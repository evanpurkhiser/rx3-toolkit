#!/bin/sh
set -eu

tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
iperf_archive=iperf-3.21.tar.gz
iperf_sha256=656e4405ebd620121de7ceca3eaf43a88f79ea1b857d041a6a0b1314801acdd8
iperf_url=https://downloads.es.net/pub/iperf/$iperf_archive
musl_archive=musl-1.2.5.tar.gz
musl_sha256=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
musl_url=https://musl.libc.org/releases/$musl_archive
image=localhost/rx3-vpu-userspace-builder:bookworm

mkdir -p "$tool_dir/dist" "$tool_dir/build"

download()
{
    destination=$1
    url=$2
    [ -f "$destination" ] || curl -fL --retry 3 -o "$destination" "$url"
}

download "$tool_dir/dist/$iperf_archive" "$iperf_url"
download "$tool_dir/dist/$musl_archive" "$musl_url"
printf '%s  %s\n' "$iperf_sha256" "$tool_dir/dist/$iperf_archive" |
    sha256sum -c -
printf '%s  %s\n' "$musl_sha256" "$tool_dir/dist/$musl_archive" |
    sha256sum -c -

podman run --rm \
    -v "$tool_dir/dist/$iperf_archive:/iperf.tar.gz:ro" \
    -v "$tool_dir/dist/$musl_archive:/musl.tar.gz:ro" \
    -v "$tool_dir/build:/out:Z" \
    "$image" sh -lc '
        set -eu
        rm -rf /tmp/iperf /tmp/musl /tmp/musl-prefix
        mkdir /tmp/iperf /tmp/musl

        tar -xzf /musl.tar.gz -C /tmp/musl --strip-components=1
        cd /tmp/musl
        CC=arm-linux-gnueabi-gcc \
        CROSS_COMPILE=arm-linux-gnueabi- \
            ./configure \
                --target=arm-linux-musleabi \
                --prefix=/tmp/musl-prefix \
                --disable-shared
        make -s -j2
        make -s install

        tar -xzf /iperf.tar.gz -C /tmp/iperf --strip-components=1
        cd /tmp/iperf
        CC=/tmp/musl-prefix/bin/musl-gcc \
        AR=arm-linux-gnueabi-ar \
        RANLIB=arm-linux-gnueabi-ranlib \
        CFLAGS="-Os -ffunction-sections -fdata-sections" \
        LDFLAGS="-static -Wl,--gc-sections" \
            ./configure \
                --host=arm-linux-musleabi \
                --enable-static-bin \
                --disable-shared \
                --without-sctp
        make -s -j2
        arm-linux-gnueabi-strip src/iperf3
        cp src/iperf3 /out/iperf3
        arm-linux-gnueabi-readelf -h /out/iperf3 | sed -n "1,20p"
        arm-linux-gnueabi-readelf -d /out/iperf3 || true
        sha256sum /out/iperf3
        wc -c /out/iperf3
    '
