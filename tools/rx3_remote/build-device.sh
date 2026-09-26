#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: build-device.sh OUTPUT_DIRECTORY" >&2
    exit 2
fi

repository=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output=$(realpath -m "$1")
mkdir -p "$output"

podman run --rm --network=none \
    -v "$repository:/src:ro" \
    -v "$output:/out:rw" \
    localhost/rx3-reverse:latest sh -eu -c '
        cd /src
        python3 mod/modules/remote-control/1.19/test_regressions.py
        clang --target=arm-linux-gnueabi \
            -march=armv7-a -marm -mfloat-abi=softfp -mfpu=neon \
            -fPIC -fno-stack-protector \
            -fno-builtin-memcmp -fno-builtin-bcmp \
            -O2 -Wall -Wextra -Werror -fuse-ld=lld -shared -nostdlib \
            -Wl,--hash-style=sysv -Wl,--build-id=none \
            -o /out/librx3_remote_control.so \
            mod/modules/remote-control/1.19/rx3_remote_control.c
        file /out/librx3_remote_control.so
        readelf -h /out/librx3_remote_control.so
        sha256sum /out/librx3_remote_control.so
    '
