#!/bin/sh
set -eu

usage() {
	cat >&2 <<'EOF'
usage: build-modules.sh KERNEL_SOURCE [OUTPUT_DIRECTORY]

Build usbnet.ko and cdc_ncm.ko for the RX3 1.19 production kernel.
KERNEL_SOURCE must be the prepared 3.0.101 source tree containing the
production .config, Module.symvers, and kernel.release. Both build containers
run without networking.
EOF
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
	usage
	exit 2
fi

kernel_source=$(realpath "$1")
output_directory=$(realpath -m "${2:-./out}")
module_source=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
reverse_image=localhost/rx3-reverse:latest
toolchain_image=localhost/rx3-usb-serial-builder:bookworm
expected_release=3.0.101-2790-gc248ed7-svn3098

for required in \
	.config \
	Module.symvers \
	drivers/net/usb/usbnet.c \
	drivers/net/usb/cdc_ncm.c \
	include/config/kernel.release; do
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

case "$(grep '^CONFIG_MODVERSIONS=' "$kernel_source/.config" || true)" in
	CONFIG_MODVERSIONS=y) ;;
	*)
		echo "kernel tree does not enable CONFIG_MODVERSIONS" >&2
		exit 1
		;;
esac

for builtin in CONFIG_USB CONFIG_NET CONFIG_MII CONFIG_CRC32; do
	if ! grep -q "^${builtin}=y$" "$kernel_source/.config"; then
		echo "kernel tree does not build required dependency $builtin in" >&2
		exit 1
	fi
done

stage=$(mktemp -d "${TMPDIR:-/tmp}/rx3-usbnet-toolchain.XXXXXX")
work=$(mktemp -d "${TMPDIR:-/tmp}/rx3-usbnet-build.XXXXXX")
cleanup() {
	rm -rf "$stage" "$work"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$stage/bin" "$stage/lib/gcc-cross" \
	"$stage/arm-linux-gnueabi" "$stage/runtime"

podman run --rm --network=none \
	-v "$stage:/toolchain:rw" \
	"$toolchain_image" sh -eu -c '
		cp -a /usr/bin/arm-linux-gnueabi-* /toolchain/bin/
		cp -a /usr/lib/gcc-cross/. /toolchain/lib/gcc-cross/
		cp -a /usr/arm-linux-gnueabi/. /toolchain/arm-linux-gnueabi/
		cp -a /usr/lib/x86_64-linux-gnu/libbfd-*-armel.so /toolchain/runtime/
		cp -a /usr/lib/x86_64-linux-gnu/libctf*-armel.so.* /toolchain/runtime/
		cp -a /usr/lib/x86_64-linux-gnu/libopcodes-*-armel.so /toolchain/runtime/
		cp -a /usr/lib/x86_64-linux-gnu/libsframe.so.* /toolchain/runtime/
		cp -a /usr/lib/x86_64-linux-gnu/libisl.so.* /toolchain/runtime/
		cp -a /usr/lib/x86_64-linux-gnu/libmpc.so.* /toolchain/runtime/
	'

podman run --rm --network=none \
	-v "$kernel_source:/kernel:ro" \
	-v "$module_source:/module:ro" \
	-v "$work:/build:rw" \
	-v "$stage/bin:/toolchain/bin:ro" \
	-v "$stage/lib/gcc-cross:/toolchain/lib/gcc-cross:ro" \
	-v "$stage/arm-linux-gnueabi:/toolchain/arm-linux-gnueabi:ro" \
	-v "$stage/runtime:/toolchain/runtime:ro" \
	"$reverse_image" sh -eu -c '
		cp /kernel/drivers/net/usb/usbnet.c /build/usbnet.c
		cp /kernel/drivers/net/usb/cdc_ncm.c /build/cdc_ncm.c
		cp /module/Makefile /build/Makefile
		cd /build
		export LD_LIBRARY_PATH=/toolchain/runtime
		make -C /kernel M=/build ARCH=arm \
			CROSS_COMPILE=/toolchain/bin/arm-linux-gnueabi- \
			CC=/toolchain/bin/arm-linux-gnueabi-gcc-12 clean
		make -C /kernel M=/build ARCH=arm \
			CROSS_COMPILE=/toolchain/bin/arm-linux-gnueabi- \
			CC=/toolchain/bin/arm-linux-gnueabi-gcc-12 modules

		usbnet_info=$(readelf -p .modinfo /build/usbnet.ko)
		cdc_ncm_info=$(readelf -p .modinfo /build/cdc_ncm.ko)
		echo "$usbnet_info" | grep -Fq \
			"vermagic=3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7"
		echo "$usbnet_info" | grep -Eq "depends=$"
		echo "$cdc_ncm_info" | grep -Fq \
			"vermagic=3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7"
		echo "$cdc_ncm_info" | grep -Fq "depends=usbnet"
		echo "$cdc_ncm_info" | grep -Fq \
			"alias=usb:v*p*d*dc*dsc*dp*ic02isc0Dip00*"

		readelf -S /build/usbnet.ko | grep -Fq "__versions"
		readelf -S /build/cdc_ncm.ko | grep -Fq "__versions"
	'

mkdir -p "$output_directory"
cp "$work/usbnet.ko" "$work/cdc_ncm.ko" "$output_directory/"
sha256sum "$output_directory/usbnet.ko" "$output_directory/cdc_ncm.ko"
