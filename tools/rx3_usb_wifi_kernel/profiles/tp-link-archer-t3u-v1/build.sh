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
[ -f /recipe/driver/os_dep/linux/usb_intf.c ] || {
    echo "prepared RTL88x2BU driver source is missing" >&2
    exit 1
}

cp -a /recipe/driver /build/driver

sed -i \
    -e 's/^CONFIG_P2P = y$/CONFIG_P2P = n/' \
    -e 's/^CONFIG_MP_INCLUDED = y$/CONFIG_MP_INCLUDED = n/' \
    -e 's/^CONFIG_EFUSE_CONFIG_FILE = y$/CONFIG_EFUSE_CONFIG_FILE = n/' \
    -e 's/^CONFIG_LOAD_PHY_PARA_FROM_FILE = y$/CONFIG_LOAD_PHY_PARA_FROM_FILE = n/' \
    -e 's/^CONFIG_BR_EXT = y$/CONFIG_BR_EXT = n/' \
    -e 's/^CONFIG_WIFI_MONITOR = y$/CONFIG_WIFI_MONITOR = n/' \
    -e 's/^CONFIG_RTW_DEBUG = y$/CONFIG_RTW_DEBUG = n/' \
    -e 's/^CONFIG_PROC_DEBUG = y$/CONFIG_PROC_DEBUG = n/' \
    /build/driver/Makefile
sed -i '0,/ -DRTW_USE_CFG80211_STA_EVENT/s///' /build/driver/Makefile

mkdir -p /build/compat
cp /recipe/Makefile /recipe/compat-average.c /build/compat/

"$kernel_tree/scripts/config" --file "$kernel_tree/.config" \
    --enable WIRELESS \
    --disable WIRELESS_EXT \
    --disable WEXT_CORE \
    --module CFG80211 \
    --disable CFG80211_WEXT \
    --enable CFG80211_INTERNAL_REGDB \
    --disable CFG80211_DEFAULT_PS \
    --enable WLAN

rx3_kernel_prepare
rx3_kernel_make M=/build/compat modules
rx3_kernel_make M=net/wireless modules
rx3_kernel_make \
    KBUILD_EXTRA_SYMBOLS="/build/compat/Module.symvers $kernel_tree/net/wireless/Module.symvers" \
    CONFIG_RTL8822B=y CONFIG_RTL8822BU=m \
    M=/build/driver modules

cp /build/compat/compat-average.ko "$output_directory/"
cp "$kernel_tree/net/wireless/cfg80211.ko" "$output_directory/"
cp /build/driver/88x2bu.ko "$output_directory/"

(cd "$output_directory" && sha256sum -c /recipe/modules.sha256)
