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

for required in \
    net/wireless/Makefile \
    net/mac80211/Makefile \
    drivers/net/wireless/rtlwifi/Makefile \
    drivers/net/wireless/rtlwifi/rtl8192cu/Makefile; do
    [ -f "$kernel_tree/$required" ] || {
        echo "missing Wi-Fi kernel source: $required" >&2
        exit 1
    }
done

for setting in CONFIG_USB=y CONFIG_FW_LOADER=y CONFIG_CRYPTO_AES=y \
    CONFIG_CRYPTO_ARC4=y CONFIG_CRYPTO_MICHAEL_MIC=y CONFIG_LEDS_CLASS=y; do
    grep -q "^${setting}$" "$kernel_tree/.config" || {
        echo "kernel tree does not provide required setting $setting" >&2
        exit 1
    }
done

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
    --module MAC80211 \
    --enable MAC80211_RC_MINSTREL \
    --enable MAC80211_RC_MINSTREL_HT \
    --enable MAC80211_RC_DEFAULT_MINSTREL \
    --enable WLAN \
    --module RTL8192CU

rx3_kernel_prepare
rx3_kernel_make M=/build/compat modules
rx3_kernel_make M=net/wireless modules
rx3_kernel_make \
    KBUILD_EXTRA_SYMBOLS="/build/compat/Module.symvers $kernel_tree/net/wireless/Module.symvers" \
    M=net/mac80211 modules
rx3_kernel_make \
    KBUILD_EXTRA_SYMBOLS="/build/compat/Module.symvers $kernel_tree/net/wireless/Module.symvers $kernel_tree/net/mac80211/Module.symvers" \
    M=drivers/net/wireless/rtlwifi modules

cp /build/compat/compat-average.ko "$output_directory/"
cp "$kernel_tree/net/wireless/cfg80211.ko" "$output_directory/"
cp "$kernel_tree/net/mac80211/mac80211.ko" "$output_directory/"
cp "$kernel_tree/drivers/net/wireless/rtlwifi/rtlwifi.ko" "$output_directory/"
cp "$kernel_tree/drivers/net/wireless/rtlwifi/rtl8192c/rtl8192c-common.ko" \
    "$output_directory/"
cp "$kernel_tree/drivers/net/wireless/rtlwifi/rtl8192cu/rtl8192cu.ko" \
    "$output_directory/"

(cd "$output_directory" && sha256sum -c /recipe/modules.sha256)
