#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

usage()
{
    cat >&2 <<'EOF'
usage: build-modules.sh KERNEL_SOURCE [OUTPUT_DIRECTORY]

Build the RTL8188CU USB Wi-Fi stack for the RX3 1.19 production kernel.
KERNEL_SOURCE must be the prepared 3.0.101 tree containing the production
.config, Module.symvers, and kernel.release. Build containers run without
network access and the source tree is mounted read-only.
EOF
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage
    exit 2
fi

kernel_source=$(realpath "$1")
output_directory=$(realpath -m "${2:-./out}")
tool_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
builder_image=localhost/rx3-usb-wifi-kernel-builder:bookworm
expected_release=3.0.101-2790-gc248ed7-svn3098

for required in \
    COPYING \
    .config \
    Module.symvers \
    include/config/kernel.release \
    net/wireless/Makefile \
    net/mac80211/Makefile \
    drivers/net/wireless/rtlwifi/Makefile \
    drivers/net/wireless/rtlwifi/rtl8192cu/Makefile; do
    if [ ! -f "$kernel_source/$required" ]; then
        echo "missing kernel build input: $kernel_source/$required" >&2
        exit 1
    fi
done

kernel_release=$(cat "$kernel_source/include/config/kernel.release")
if [ "$kernel_release" != "$expected_release" ]; then
    echo "unexpected kernel release: $kernel_release" >&2
    exit 1
fi

for setting in CONFIG_MODULES=y CONFIG_MODVERSIONS=y CONFIG_USB=y \
    CONFIG_FW_LOADER=y CONFIG_CRYPTO_AES=y CONFIG_CRYPTO_ARC4=y \
    CONFIG_CRYPTO_MICHAEL_MIC=y CONFIG_LEDS_CLASS=y; do
    if ! grep -q "^${setting}$" "$kernel_source/.config"; then
        echo "kernel tree does not provide required setting $setting" >&2
        exit 1
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/rx3-usb-wifi-build.XXXXXX")
cleanup()
{
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$work/kernel"

podman run --rm --network=none \
    -v "$kernel_source:/kernel:ro" \
    -v "$tool_directory:/module:ro" \
    -v "$work:/build:rw" \
    "$builder_image" sh -eu -c '
        cp -a /kernel/. /build/kernel/
        mkdir /build/compat
        cp /module/Makefile /module/compat-average.c /build/compat/

        /build/kernel/scripts/config --file /build/kernel/.config \
            --enable WIRELESS \
            --disable WIRELESS_EXT \
            --disable WEXT_CORE \
            --module CFG80211 \
            --disable CFG80211_WEXT \
            --enable CFG80211_INTERNAL_REGDB \
            --disable CFG80211_DEFAULT_PS \
            --module MAC80211 \
            --enable MAC80211_RC_MINSTREL \
            --enable MAC80211_RC_MINSTREL_HT \
            --enable MAC80211_RC_DEFAULT_MINSTREL \
            --enable WLAN \
            --module RTL8192CU

        make -C /build/kernel ARCH=arm \
            CROSS_COMPILE=arm-linux-gnueabi- \
            CC=arm-linux-gnueabi-gcc-12 \
            HOSTCC=clang \
            oldnoconfig prepare modules_prepare

        # modules_prepare removes Module.symvers. Restore the production CRCs
        # before modpost combines them with exports from the new WLAN modules.
        cp /kernel/Module.symvers /build/kernel/Module.symvers
        make -C /build/kernel ARCH=arm \
            CROSS_COMPILE=arm-linux-gnueabi- \
            CC=arm-linux-gnueabi-gcc-12 \
            HOSTCC=clang \
            KCFLAGS="-fno-pie -fno-pic" \
            M=/build/compat modules
        make -C /build/kernel ARCH=arm \
            CROSS_COMPILE=arm-linux-gnueabi- \
            CC=arm-linux-gnueabi-gcc-12 \
            HOSTCC=clang \
            KCFLAGS="-fno-pie -fno-pic" \
            M=net/wireless modules
        make -C /build/kernel ARCH=arm \
            CROSS_COMPILE=arm-linux-gnueabi- \
            CC=arm-linux-gnueabi-gcc-12 \
            HOSTCC=clang \
            KCFLAGS="-fno-pie -fno-pic" \
            KBUILD_EXTRA_SYMBOLS="/build/compat/Module.symvers /build/kernel/net/wireless/Module.symvers" \
            M=net/mac80211 modules
        make -C /build/kernel ARCH=arm \
            CROSS_COMPILE=arm-linux-gnueabi- \
            CC=arm-linux-gnueabi-gcc-12 \
            HOSTCC=clang \
            KCFLAGS="-fno-pie -fno-pic" \
            KBUILD_EXTRA_SYMBOLS="/build/compat/Module.symvers /build/kernel/net/wireless/Module.symvers /build/kernel/net/mac80211/Module.symvers" \
            M=drivers/net/wireless/rtlwifi modules

        release="3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7"
        cp /build/compat/compat-average.ko /build/
        readelf -p .modinfo /build/compat-average.ko | grep -Fq "vermagic=$release"
        readelf -x __versions /build/compat-average.ko | grep -Fq module_layou
        ! readelf -sW /build/compat-average.ko | grep -Fq _GLOBAL_OFFSET_TABLE_
        for module in \
            net/wireless/cfg80211.ko \
            net/mac80211/mac80211.ko \
            drivers/net/wireless/rtlwifi/rtlwifi.ko \
            drivers/net/wireless/rtlwifi/rtl8192c/rtl8192c-common.ko \
            drivers/net/wireless/rtlwifi/rtl8192cu/rtl8192cu.ko; do
            test -f "/build/kernel/$module"
            readelf -p .modinfo "/build/kernel/$module" | grep -Fq "vermagic=$release"
            readelf -S "/build/kernel/$module" | grep -Eq "__versions.*[[:space:]][0-9a-fA-F]{6,}[[:space:]]"
            readelf -x __versions "/build/kernel/$module" | grep -Fq module_layou
            ! readelf -sW "/build/kernel/$module" | grep -Fq _GLOBAL_OFFSET_TABLE_
            cp "/build/kernel/$module" /build/
        done
    '

mkdir -p "$output_directory"
for module in compat-average cfg80211 mac80211 rtlwifi rtl8192c-common rtl8192cu; do
    cp "$work/$module.ko" "$output_directory/"
done
cp "$kernel_source/COPYING" "$output_directory/COPYING.linux"
sha256sum "$output_directory"/*.ko
