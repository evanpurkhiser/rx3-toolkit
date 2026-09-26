#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository=$(CDPATH= cd -- "$tool_dir/../.." && pwd)
archive=dropbear-2026.94.tar.bz2
archive_sha256=e098034a843699200c8c977a991fff73159735bf795d5f72ef672c41a6b1ae81
musl_archive=musl-1.2.5.tar.gz
musl_archive_sha256=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
image=localhost/rx3-vpu-userspace-builder:bookworm
archive_url=https://matt.ucc.asn.au/dropbear/releases/$archive
musl_archive_url=https://musl.libc.org/releases/$musl_archive

cd "$tool_dir"
[ -f "dist/$archive" ] || {
    mkdir -p dist
    curl -fL --retry 3 -o "dist/$archive" "$archive_url"
}
[ -f "dist/$musl_archive" ] || {
    mkdir -p dist
    curl -fL --retry 3 -o "dist/$musl_archive" "$musl_archive_url"
}
printf '%s  %s\n' "$archive_sha256" "dist/$archive" | sha256sum -c -
printf '%s  %s\n' "$musl_archive_sha256" "dist/$musl_archive" | \
    sha256sum -c -
mkdir -p build

podman run --rm --network=none \
    -v "$tool_dir/dist/$archive:/dropbear.tar.bz2:ro" \
    -v "$tool_dir/dist/$musl_archive:/musl.tar.gz:ro" \
    -v "$tool_dir/localoptions.h:/localoptions.h:ro" \
    -v "$tool_dir/rx3-static-root.patch:/rx3-static-root.patch:ro" \
    -v "$tool_dir/build:/out:rw" \
    "$image" sh -eu -c '
        rm -rf /tmp/dropbear /tmp/musl-source /tmp/musl-prefix
        mkdir /tmp/dropbear /tmp/musl-source
        tar -xjf /dropbear.tar.bz2 -C /tmp/dropbear --strip-components=1
        patch -d /tmp/dropbear -p1 < /rx3-static-root.patch
        tar -xzf /musl.tar.gz -C /tmp/musl-source --strip-components=1
        cd /tmp/musl-source
        CC=arm-linux-gnueabi-gcc \
        CROSS_COMPILE=arm-linux-gnueabi- \
        CFLAGS="-Os -march=armv7-a -marm -mfloat-abi=softfp -fno-ident" \
        ./configure \
            --target=arm-linux-musleabi \
            --prefix=/tmp/musl-prefix \
            --disable-shared
        make -s -j2
        make -s install

        cp /localoptions.h /tmp/dropbear/localoptions.h
        cd /tmp/dropbear
        SOURCE_DATE_EPOCH=1784782320 \
        CC=/tmp/musl-prefix/bin/musl-gcc \
        AR=arm-linux-gnueabi-ar \
        RANLIB=arm-linux-gnueabi-ranlib \
        CFLAGS="-Os -march=armv7-a -marm -mfloat-abi=softfp -fno-ident -ffunction-sections -fdata-sections" \
        LDFLAGS="-static -Wl,--gc-sections,--build-id=none" \
        LTM_CFLAGS="-Os -march=armv7-a -marm -mfloat-abi=softfp -fno-ident -ffunction-sections -fdata-sections -Wno-undef" \
        ./configure \
            --build=x86_64-linux-gnu \
            --host=arm-linux-musleabi \
            --enable-static \
            --enable-bundled-libtom \
            --disable-zlib \
            --disable-syslog \
            --disable-lastlog \
            --disable-utmp \
            --disable-utmpx \
            --disable-wtmp \
            --disable-wtmpx \
            --disable-loginfunc \
            --disable-pututline \
            --disable-pututxline
        make -s -j2 PROGRAMS="dropbear dropbearkey" MULTI=1 STATIC=1
        arm-linux-gnueabi-strip dropbearmulti
        cp dropbearmulti /out/dropbearmulti
        arm-linux-gnueabi-readelf -h /out/dropbearmulti | sed -n "1,20p"
        arm-linux-gnueabi-readelf -d /out/dropbearmulti | \
            grep NEEDED && exit 1 || true
        sha256sum /out/dropbearmulti
    '

cp "$tool_dir/build/dropbearmulti" \
    "$repository/mod/modules/rx3-ssh/1.19/dropbearmulti"
