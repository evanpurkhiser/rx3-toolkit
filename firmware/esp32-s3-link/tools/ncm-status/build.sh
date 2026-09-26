#!/bin/sh
set -eu

tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output_dir="$tool_dir/build"
image=localhost/rx3-vpu-userspace-builder:bookworm

mkdir -p "$output_dir"

podman run --rm --network=none \
    -v "$tool_dir:/tool:ro" \
    -v "$output_dir:/out:rw" \
    "$image" sh -eu -c '
        arm-linux-gnueabi-gcc \
            -march=armv7-a -mfloat-abi=softfp \
            -O2 -Wall -Wextra -Werror -static \
            /tool/rx3_ncm_status.c -o /out/rx3-ncm-status
        arm-linux-gnueabi-strip /out/rx3-ncm-status
        arm-linux-gnueabi-readelf -h /out/rx3-ncm-status | sed -n "1,20p"
        sha256sum /out/rx3-ncm-status
    '
