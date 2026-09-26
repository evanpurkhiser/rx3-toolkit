#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: build.sh VPU_SOURCE RX3_ROOTFS OUTPUT_DIRECTORY" >&2
    exit 2
fi

vpu_source=$(realpath "$1")
rx3_rootfs=$(realpath "$2")
output=$(realpath -m "$3")
tool_source=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
image=localhost/rx3-vpu-userspace-builder:bookworm
mkdir -p "$output"

podman run --rm --network=none \
    -v "$vpu_source:/vpu:ro" \
    -v "$rx3_rootfs:/sysroot:ro" \
    -v "$tool_source:/tool:ro" \
    -v "$output:/out:rw" \
    "$image" sh -eu -c '
        cd /out
        rm -f ./*.o ./libvpu.so ./libvpu.so.4 ./rx3-vpu-stream
        for source in vpu_io vpu_util vpu_lib vpu_gdi vpu_debug; do
            arm-linux-gnueabi-gcc -DIMX6Q -DPLATFORM_STR=\"IMX6Q\" \
                -march=armv7-a -mfpu=neon -mfloat-abi=softfp \
                -O2 -fPIC -I/vpu -c "/vpu/$source.c" -o "$source.o"
        done
        arm-linux-gnueabi-gcc -shared -nostdlib -Wl,-soname,libvpu.so.4 \
            vpu_io.o vpu_util.o vpu_lib.o vpu_gdi.o vpu_debug.o \
            -lgcc -o libvpu.so.4
        ln -s libvpu.so.4 libvpu.so
        arm-linux-gnueabi-gcc -march=armv7-a -mfpu=neon \
            -mfloat-abi=softfp -O2 -Wall -Wextra -Werror -fno-pic -fno-pie \
            -I/vpu -c /tool/rx3_vpu_stream.c -o rx3_vpu_stream.o
        arm-linux-gnueabi-gcc -nostdlib -no-pie -Wl,-e,_start \
            -Wl,--dynamic-linker,/lib/ld-linux.so.3 \
            -Wl,-rpath,/tmp/rx3-vpu -L/out rx3_vpu_stream.o \
            -Wl,--no-as-needed -lvpu /sysroot/lib/libpthread.so.0 \
            /sysroot/lib/libc.so.6 /sysroot/lib/ld-linux.so.3 \
            -lgcc -o rx3-vpu-stream
        arm-linux-gnueabi-readelf -h rx3-vpu-stream
        arm-linux-gnueabi-readelf --version-info rx3-vpu-stream
        sha256sum libvpu.so.4 rx3-vpu-stream
    '
