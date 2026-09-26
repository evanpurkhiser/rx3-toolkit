#!/bin/sh
set -eu

usage() {
	cat >&2 <<'EOF'
usage: build-module.sh KERNEL_SOURCE [OUTPUT_DIRECTORY]

Build mxc_vpu.ko for the RX3 1.19 production kernel. KERNEL_SOURCE must be the
prepared 3.0.101 source tree containing the production .config, Module.symvers,
and kernel.release. Build the local Podman image documented in
tools/rx3_vpu_userspace/README.md first. The build disables container networking.
EOF
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
	usage
	exit 2
fi

kernel_source=$(realpath "$1")
output_directory=$(realpath -m "${2:-./out}")
module_source=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
toolchain_image=localhost/rx3-vpu-userspace-builder:bookworm
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

work=$(mktemp -d "${TMPDIR:-/tmp}/rx3-vpu-build.XXXXXX")
cleanup() {
	rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

podman run --rm --network=none \
	-v "$kernel_source:/kernel:ro" \
	-v "$module_source:/module:ro" \
	-v "$work:/build:rw" \
	"$toolchain_image" sh -eu -c '
		cp /kernel/drivers/mxc/vpu/mxc_vpu.c /build/mxc_vpu.c.orig
		python3 /module/apply-module-patch.py
		cp /module/Makefile /build/Makefile
		cd /build
		make -C /kernel M=/build ARCH=arm \
			CROSS_COMPILE=arm-linux-gnueabi- \
			CC=arm-linux-gnueabi-gcc-12 clean
		make -C /kernel M=/build ARCH=arm \
			CROSS_COMPILE=arm-linux-gnueabi- \
			CC=arm-linux-gnueabi-gcc-12 modules
	'

mkdir -p "$output_directory"
cp "$work/mxc_vpu.ko" "$output_directory/mxc_vpu.ko"
sha256sum "$output_directory/mxc_vpu.ko"
