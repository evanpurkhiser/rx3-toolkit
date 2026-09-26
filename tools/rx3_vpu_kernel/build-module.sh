#!/bin/sh
set -eu

usage() {
	cat >&2 <<'EOF'
usage: build-module.sh KERNEL_SOURCE [OUTPUT_DIRECTORY]

Build mxc_vpu.ko for the RX3 1.19 production kernel. KERNEL_SOURCE must be the
prepared 3.0.101 source tree containing the production .config, Module.symvers,
and kernel.release. The build uses only the two local Podman images named below
and disables networking for both containers.
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
	drivers/mxc/vpu/mxc_vpu.c \
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

stage=$(mktemp -d "${TMPDIR:-/tmp}/rx3-vpu-toolchain.XXXXXX")
work=$(mktemp -d "${TMPDIR:-/tmp}/rx3-vpu-build.XXXXXX")
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
		cp /kernel/drivers/mxc/vpu/mxc_vpu.c /build/mxc_vpu.c.orig
		python3 /module/apply-module-patch.py
		cp /module/Makefile /build/Makefile
		cd /build
		export LD_LIBRARY_PATH=/toolchain/runtime
		make -C /kernel M=/build ARCH=arm \
			CROSS_COMPILE=/toolchain/bin/arm-linux-gnueabi- \
			CC=/toolchain/bin/arm-linux-gnueabi-gcc-12 clean
		make -C /kernel M=/build ARCH=arm \
			CROSS_COMPILE=/toolchain/bin/arm-linux-gnueabi- \
			CC=/toolchain/bin/arm-linux-gnueabi-gcc-12 modules
	'

mkdir -p "$output_directory"
cp "$work/mxc_vpu.ko" "$output_directory/mxc_vpu.ko"
sha256sum "$output_directory/mxc_vpu.ko"
