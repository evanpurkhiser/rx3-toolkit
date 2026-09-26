# RX3 VPU userspace probe

This directory builds the smallest userspace validation for the RX3's i.MX6Q
VPU. The probe loads the matching microcode through `libvpu`, reads the hardware
and library versions, then releases the VPU. It performs no encode operation.

Build the toolchain image once:

```sh
podman build \
  -t localhost/rx3-vpu-userspace-builder:bookworm \
  -f tools/rx3_vpu_userspace/Containerfile \
  tools/rx3_vpu_userspace
```

The shared image also contains `bzip2`, QEMU user-mode emulation, and the
OpenSSH client. The RX3 Dropbear build uses these tools for container-only
source extraction and its ARM authentication smoke test.

Then build the probe without network access:

```sh
tools/rx3_vpu_userspace/build-probe.sh \
  /path/to/imx-vpu-3.10.17-1.0.0/vpu \
  /path/to/rx3/system-root \
  build/rx3-vpu-userspace
```

The 3.10.17 package contains the same `libvpu` 5.4.23 used by Freescale's
3.0.101_4.1.1 BSP. Linking the executable against the extracted RX3 libraries
keeps its symbol requirements at GLIBC 2.4, within the firmware's glibc 2.13.
The library itself intentionally leaves libc and pthread references for the
main executable to provide at runtime.

Firmware can remain outside the root filesystem during validation:

```sh
insmod mxc_vpu.ko dram_top=0x4fffffff
env LD_LIBRARY_PATH=/tmp/rx3-vpu \
    VPU_FW_PATH=/tmp/rx3-vpu \
    /tmp/rx3-vpu/rx3-vpu-probe
```

Live firmware 1.19 output was:

```text
Product Info: i.MX6Q/D/S
firmware 3.1.1 code 46056, library 5.4.23
```

## Hardware encode validation

`mxc-vpu-test-5.4.23.patch` adapts NXP's `mxc_vpu_test` 3.14.28 encoder to the
5.4.23 API by using the older SPS command and removing VUI and SOF-stuffing
fields that are absent from this library. The resulting ARM binary encoded the
IPU capture with:

```sh
env LD_LIBRARY_PATH=/tmp/rx3-vpu \
    VPU_FW_PATH=/tmp/rx3-vpu \
    ./mxc_vpu_test.out -E \
    "-i frame.i420 -o frame.264 -w 640 -h 400 -f 2 -c 1 -g 1 -b 1000 -t 0"
```

The one-frame test reported 40.12 encoder frames per second and 30.78 aggregate
frames per second. Its 12,451-byte Annex-B output contains a Baseline level 3.0
SPS for exactly 640x400, a PPS, and an IDR slice. This proves the hardware path
and API compatibility. It does not measure sustained capture, encoding, and
network delivery; that requires a persistent encoder session and DMA buffer
ring.
