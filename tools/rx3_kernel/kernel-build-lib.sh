#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

rx3_kernel_make()
{
    make -C "$RX3_KERNEL_TREE" \
        ARCH=arm \
        CROSS_COMPILE=arm-linux-gnueabi- \
        CC=arm-linux-gnueabi-gcc-12 \
        HOSTCC=clang \
        KCFLAGS="-fno-pie -fno-pic" \
        "$@"
}

rx3_kernel_prepare()
{
    rx3_kernel_make oldnoconfig prepare modules_prepare

    # Linux 3.0 removes Module.symvers during modules_prepare. The production
    # CRCs define the ABI these additional modules must target.
    cp "$RX3_KERNEL_PRODUCTION_SYMVERS" "$RX3_KERNEL_TREE/Module.symvers"
}
