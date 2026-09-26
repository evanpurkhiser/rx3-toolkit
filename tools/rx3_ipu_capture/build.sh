#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output=${1:-"$script_dir/../../build/rx3-ipu-capture"}

mkdir -p "$(dirname -- "$output")"

clang --target=arm-linux-gnueabi \
    -march=armv7-a -mfloat-abi=softfp \
    -Os -ffreestanding -fno-builtin -fno-stack-protector -fno-pic \
    -Wall -Wextra -Werror \
    -nostdlib -static -fuse-ld=lld -Wl,-e,_start -Wl,--build-id=none \
    "$script_dir/rx3_ipu_capture.c" -o "$output"
