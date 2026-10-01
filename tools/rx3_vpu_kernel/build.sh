#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

kernel_tree=$1
output_directory=$2
. /tool/kernel-build-lib.sh

[ "$kernel_tree" = "$RX3_KERNEL_TREE" ] || {
    echo "kernel recipe received an unexpected build tree" >&2
    exit 1
}
[ -f "$kernel_tree/drivers/mxc/vpu/mxc_vpu.c" ] || {
    echo "kernel tree does not provide drivers/mxc/vpu/mxc_vpu.c" >&2
    exit 1
}

cp "$kernel_tree/drivers/mxc/vpu/mxc_vpu.c" /build/mxc_vpu.c.orig
python3 /recipe/apply-module-patch.py
cp /recipe/Makefile /build/Makefile

rx3_kernel_prepare
rx3_kernel_make M=/build clean
rx3_kernel_make M=/build modules

cp /build/mxc_vpu.ko "$output_directory/"
(cd "$output_directory" && sha256sum -c /recipe/modules.sha256)
