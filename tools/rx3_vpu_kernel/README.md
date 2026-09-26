# RX3 1.19 VPU kernel module

This directory preserves the minimal source change and reproducible external
module build for the i.MX6Quad VPU driver in firmware 1.19. The module has been
loaded on an RX3, initialized the VPU, supported a hardware H.264 encode, and
unloaded cleanly.

The vendor driver calls `memblock_analyze()` and
`memblock_end_of_DRAM_with_reserved()` during module initialization. Those two
kernel symbols are not exported. `mxc_vpu-module.patch` replaces the calls with
a required read-only `dram_top` module parameter. The parameter retains the
upper-bound check performed by `VPU_IOC_PHYMEM_CHECK`; the RX3 value comes from
the live `/proc/iomem` map.

Fetch the checksum-pinned Pioneer GPL source, build the shared toolchain, and
run this feature recipe through the common validation layer:

```sh
make kernel-source FIRMWARE=1.19
make kernel-builder
make kernel-modules \
  MODULE=vpu \
  FIRMWARE=1.19 \
  KERNEL_SOURCE=build/kernel-source/1.19
```

The feature recipe performs the kernel source transformation with strict
context checks. Its checksummed 63-entry `production.symvers` contains exactly
the production symbols imported by `mxc_vpu.ko`. The common builder copies the
published source tree, restores the production release suffix and symbol CRCs,
runs the external module build with networking disabled, and rejects missing,
unused, duplicate, or mismatched ABI entries.

The generated `build/artifacts/1.19/vpu/mxc_vpu.ko` has these properties:

```text
SHA-256  096d0ccff06259ddc2b9594bed987fd7453d11edffe12a279cdb86531f96fbd3
format   ELF 32-bit LSB relocatable, ARM EABI5
vermagic 3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7
depends  none
param    dram_top: highest valid RX3 physical RAM address (ulong)
```

The matching live value is `dram_top=0x4fffffff`. Loading is deliberately a
separate, manual validation step described in
[`docs/hardware-video-encoding-1.19.md`](../../docs/hardware-video-encoding-1.19.md).

The module has been loaded and unloaded successfully on firmware 1.19. It
created `/dev/mxc_vpu`, claimed IRQs 35 and 44, and logged `VPU initialized`.
The explicit non-PIC/non-PIE build flags are required with Debian GCC 12; the
compiler otherwise leaves an unresolved `_GLOBAL_OFFSET_TABLE_` reference.
The shared builder rejects that relocation along with an incorrect architecture,
vermagic, modversion table, or pinned output checksum. Kernel modules are
generated artifacts beneath `build/artifacts/1.19/vpu/` and stay out of Git.
