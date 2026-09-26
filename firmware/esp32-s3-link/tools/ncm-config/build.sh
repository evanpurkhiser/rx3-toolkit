#!/bin/sh
set -eu

tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$tool_dir/../.." && pwd)
output_dir="$tool_dir/build"
image=localhost/rx3-vpu-userspace-builder:bookworm

mkdir -p "$output_dir"

podman run --rm --network=none \
    -v "$project_dir:/project:ro" \
    -v "$output_dir:/out:rw" \
    "$image" sh -eu -c '
        arm-linux-gnueabi-gcc \
            -march=armv7-a -mfloat-abi=softfp \
            -O2 -Wall -Wextra -Werror -static \
            /project/tools/ncm-config/rx3_ncm_config.c -o /out/rx3-s3-config
        arm-linux-gnueabi-strip /out/rx3-s3-config
        arm-linux-gnueabi-readelf -h /out/rx3-s3-config | sed -n "1,20p"
        sha256sum /out/rx3-s3-config
    '
