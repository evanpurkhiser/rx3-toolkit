# RX3 1.19 hardware video encoding

The RX3 contains an i.MX6Quad VPU capable of real-time H.264 and MJPEG
encoding. Firmware 1.19 registers the VPU platform device, but omits its kernel
driver, firmware, and userspace library. The existing IPU remains available for
hardware scaling and RGB-to-YUV conversion, so a complete low-overhead capture
path is feasible after restoring the matching VPU stack.

## Confirmed hardware and firmware state

The released kernel configuration identifies the platform and its enabled media
blocks:

```text
CONFIG_SOC_IMX6Q=y
CONFIG_MACH_MX6Q_SABRESD=y
CONFIG_IMX_HAVE_PLATFORM_IMX_VPU=y
CONFIG_MXC_IPU=y
CONFIG_MXC_IPU_V3=y
CONFIG_MXC_IPU_V3H=y
# CONFIG_MXC_VPU is not set
```

The RX3-specific path in `board-mx6q_sabresd.c` calls both
`imx6q_add_ipuv3()` and `imx6q_add_vpu()`. Live inspection confirms the
resulting device state:

```text
/sys/devices/platform/mxc_vpu       present
modalias                            platform:mxc_vpu
/dev/mxc_ipu                        present, character device 253:0
/dev/mxc_vpu                        absent
```

IPU interrupts 37 through 40 are registered and IRQ 38 is active. The VPU
platform node proves that the board registered the block and that the i.MX6 VPU
disable fuse did not reject it. `/dev/mxc_vpu` is absent because the character
driver was not built.

The firmware package selection also excludes every part of the userspace VPU
stack:

```text
# CONFIG_PKG_FIRMWARE_IMX is not set
# CONFIG_PKG_IMX_VPU_LIB is not set
# CONFIG_PKG_LIBFSLVPUWRAP is not set
# CONFIG_PKG_FFMPEG is not set
# CONFIG_PKG_GSTREAMER_CORE is not set
```

The root filesystem contains no VPU microcode or `libvpu`. It does contain
libjpeg-turbo 1.5.3 with ARM NEON encoder routines and the
`tjCompressFromYUV` API, which provides a useful software-encoding fallback.

NXP specifies 1080p30 H.264 Baseline encoding for the i.MX6Dual/Quad VPU. Its
MJPEG encoder is rated at 160 megapixels per second. A 1280x800 stream at 30 fps
is 30.7 megapixels per second; a 640x400 stream is 7.7 megapixels per second.
The hardware therefore has substantial capacity for the RX3 display. See the
[i.MX6Quad product information](https://www.nxp.com/products/i.MX6Q) and the
[i.MX VPU API reference](https://www.nxp.com/docs/en/reference-manual/i.MX_VPU_Application_Programming_Interface_Linux_RM.pdf).

## Capture and encode pipeline

The intended hardware path is:

```text
/dev/fb0 RGB565
        |
        | framebuffer physical address and active yoffset
        v
/dev/mxc_ipu
  scale 1280x800 to 640x400
  convert RGB565 to YUV420
        |
        | contiguous DMA buffer
        v
/dev/mxc_vpu
  H.264 Baseline or MJPEG
        |
        v
USB network -> companion server -> browser
```

`FBIOGET_FSCREENINFO` supplies the framebuffer physical base and stride;
`FBIOGET_VSCREENINFO` supplies the active offset. The IPU userspace interface
provides `IPU_ALLOC`, `IPU_CHECK_TASK`, `IPU_QUEUE_TASK`, and `IPU_FREE`.
`IPU_ALLOC` creates a physically contiguous output buffer that can be mapped by
the process and passed to the VPU as YUV420 planes. A two-to-one scale is well
within the IPU limits and removes RGB conversion and resizing from the ARM
cores.

The matching `libvpu` interface then drives `/dev/mxc_vpu` through calls such as
`vpu_Init`, `vpu_EncOpen`, `vpu_EncRegisterFrameBuffer`,
`vpu_EncStartOneFrame`, and `vpu_EncGetOutputInfo`. H.264 encoding accepts
YUV420 input. DMA ownership and cache visibility must be validated when the IPU
output buffer is handed to the VPU.

The framebuffer streamer must continue to use the active scanout page. It must
also drop frames rather than delaying `rbp`; no encoder or network work belongs
on the display presentation thread.

## Missing VPU components

Three additions are required:

1. An `mxc_vpu.ko` built against the exact RX3 kernel configuration and symbol
   versions.
2. Matching i.MX6Quad microcode, normally
   `/lib/firmware/vpu/vpu_fw_imx6q.bin`.
3. A compatible `imx-vpu-lib` providing `libvpu` and its headers.

The [NXP 3.0.101 release materials](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/imx-processors/80811/1/MX_6_Linux_3.0.101_4.1.1_Patch_Release_Notes.pdf)
list
`firmware-imx-3.0.101-4.1.1.tar.gz` and
`imx-vpu-lib-3.0.101-4.1.1.tar.gz`. The latter provides `libvpu` 5.4.23.
These are the matching initial userspace components for firmware 1.19. VPU
firmware and libraries must come from the same BSP generation because their
command and microcode interfaces are coupled. `libvpu` accepts `VPU_FW_PATH` as
the directory containing `vpu_fw_imx6q.bin`; its conventional installed path is
`/lib/firmware/vpu/vpu_fw_imx6q.bin`. NXP's
[i.MX6 Linux reference manual](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/imx-processors/87230/3/Linux_6DQ_RM_L3.0.35_1.1.0.pdf)
describes the vendor driver, `imx-vpu-lib`, and firmware arrangement.

The vendor kernel driver calls `memblock_analyze()` and
`memblock_end_of_DRAM_with_reserved()`, neither of which is exported to modules;
the kernel also has `CONFIG_MODVERSIONS=y`, so every remaining kernel symbol
must match its recorded CRC. The two unavailable calls only establish the upper
physical-memory bound used by
`VPU_IOC_PHYMEM_CHECK`. The module build in
[`tools/rx3_vpu_kernel`](../tools/rx3_vpu_kernel/) replaces those calls with a
required, read-only `dram_top` parameter. The live RX3 memory map ends at
`0x4fffffff`, so the initial load contract is:

```sh
insmod ./mxc_vpu.ko dram_top=0x4fffffff
```

The parameter preserves the driver's rejection of physical addresses above
RAM. Loading without it returns `EINVAL`. A wrong upper bound can allow an
invalid DMA address to reach the hardware, so the value must be checked against
`/proc/iomem` before every load on a new firmware or hardware revision.

The platform device already carries the VPU register, IPI IRQ, JPEG IRQ,
reset, IRAM, clock, and regulator data. The corrected module binds to that
existing device and creates `/dev/mxc_vpu`; a custom kernel image is not
required.

## Reproduced module build

The corrected driver builds as an external module against the production 1.19
kernel tree and its recovered `Module.symvers`. The build ran offline inside
`localhost/rx3-reverse:latest` with an ARM EABI GCC 12.2 toolchain staged from
the existing USB-serial builder image. `modpost` reported no unresolved
symbols. The checked-in artifact has these properties:

```text
file     ELF 32-bit LSB relocatable, ARM EABI5
size     19,796 bytes
SHA-256  096d0ccff06259ddc2b9594bed987fd7453d11edffe12a279cdb86531f96fbd3
vermagic 3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7
depends  none
param    dram_top: highest valid RX3 physical RAM address (ulong)
```

The full build command is captured by
[`tools/rx3_vpu_kernel/build-module.sh`](../tools/rx3_vpu_kernel/build-module.sh).
It verifies the exact kernel release and `CONFIG_MODVERSIONS=y`, applies the
small source patch inside the network-isolated reverse-engineering container,
and performs the external-module build so `modpost` imports the production
symbol CRCs.

The registered platform resources expected during probe are:

| Resource | Firmware 1.19 value |
| --- | --- |
| Platform name | `mxc_vpu` |
| Register range | `0x02040000-0x02043fff` |
| JPEG IRQ | 35 |
| IPI IRQ | 44 |
| Internal RAM | `0x21000` bytes |
| Clock | `vpu_clk` |
| Regulator | `cpu_vddvpu` |
| Device node after probe | `/dev/mxc_vpu` |

The first hardware load on firmware 1.19 succeeded. The module created character
device `/dev/mxc_vpu` with major 246, claimed JPEG IRQ 35 and codec IRQ 44, and
logged `VPU initialized`. Unloading removed the device and released both IRQs.
An earlier GCC 12 build failed safely before probe because it contained an
unresolved `_GLOBAL_OFFSET_TABLE_` reference. The external Makefile now forces
non-PIC and non-PIE code, and offline ELF inspection verifies that the reference
is absent.

The vendor probe has weak cleanup on some failure paths, and userspace calls
program physical DMA addresses. Every load must therefore reconfirm the highest
inclusive System RAM address in `/proc/iomem`, supply it as `dram_top`, and
inspect `dmesg` before opening the device. The module, version probe, private
H.264 frame, and IPU framebuffer conversion have all passed as separate bounded
tests. A persistent streamer is the remaining integration boundary.

## Live hardware validation

The IPU, VPU driver, firmware, library, and encoder have now been exercised on
firmware 1.19 in separate bounded tests:

| Stage | Result |
| --- | --- |
| IPU RGB565 to 640x400 I420 | 50 ms including process startup and `/tmp` write |
| VPU initialization | firmware 3.1.1 build 46056, library 5.4.23, 90 ms |
| One-frame H.264 encode | 40.12 encoder fps, 30.78 aggregate fps |
| H.264 output | 12,451 bytes, Baseline level 3.0, 640x400 IDR |

The IPU frame matched the simultaneous RGB565 stream in crop, geometry, and
color. The H.264 stream contains one SPS, one PPS, and one IDR NAL unit. The
codec IRQ count advanced once. Each test released its userspace allocations,
and the VPU module unloaded cleanly afterward.

The aggregate one-frame result crosses the 30 fps target even with file input
and output. The continuous streamer now retains three I420 buffers, keeps the
VPU session open, and sends encoded access units directly to the companion
without writing intermediate or output frames to disk.

The live pipeline sustained 90 frames in 2.97 seconds, or 30.33 fps. FFmpeg
decoded all 90 frames as 640x400 YUV420 Constrained Baseline H.264 level 3.0.
The capture contained SPS, PPS, three IDRs, and 87 P-frames and averaged 1.01
Mbit/s. The browser relay sustained 30.0 fps at 1.00 Mbit/s over tailnet HTTPS;
Chromium WebCodecs reported approximately 2 ms from WebSocket receipt to canvas
display.

Increasing the target to 2 Mbit/s and lowering picture QP from 23 to 20 retained
30.06 fps. A live Chromium sample measured 2.07 Mbit/s and 3 ms WebCodecs decode
latency. Fine waveform edges and small text remained clearer during motion,
with no observed frame-rate cost.

A 3 Mbit/s target with the same QP 20 setting also retained 30.01 fps. Chromium
measured 3.13 Mbit/s and 2 ms decode latency. This is the active quality setting;
the 1 and 2 Mbit/s binaries remain available on the test USB drive for direct
rollback comparisons.

## Browser transport

MJPEG is the shortest browser path. The companion can proxy JPEG frames as a
multipart HTTP stream, and the VPU or the installed NEON libjpeg-turbo can
produce each frame independently. Its bandwidth will be higher than H.264, but
it is simple to validate and has predictable latency.

H.264 is the preferred final transport because temporal prediction compresses
the mostly static UI and moving waveform efficiently. The VPU emits an Annex-B
elementary stream, which a browser video element cannot consume directly. The
companion should preserve SPS/PPS and remux the stream into fragmented MP4 for
Media Source Extensions, or package it for WebRTC. H.264 Baseline, no B-frames,
a roughly one-second GOP, and a 0.5 to 2 Mbit/s target are appropriate starting
settings for 640x400 at 30 fps.

The existing WebSocket canvas remains useful for diagnostic tile streams. It
can also carry cheaply compressed dirty rectangles while the VPU path is being
brought up, but it does not provide the bandwidth efficiency or browser decode
offload of H.264.

## Continuous-stream implementation

The production loop should keep the IPU and VPU descriptors, VPU encoder, and
bitstream buffer open for the lifetime of a client session. Two or three I420
DMA buffers form an ownership ring: one is available to the IPU, one may be
owned by the VPU, and one may wait for either stage. A timer selects a new
framebuffer page at 30 Hz and drops that capture when no IPU buffer is free.
The encoder emits an IDR and SPS/PPS when a client connects, followed by
P-frames with a roughly one-second GOP. A bounded socket queue similarly drops
complete access units when the USB network cannot keep up.

The companion should first remux Annex-B access units into fragmented MP4 for
Media Source Extensions. WebRTC is a later transport option when interactive
latency and congestion control justify the added signaling. Validation should
measure sustained delivered frame rate, bitrate, latency, dropped frames, RX3
CPU use, audio stability, and UI responsiveness under moving waveforms.

The RX3 boots with `isolcpus=3`. Its IRQ policy assigns SDMA to CPU 3, USB and
Ethernet to CPU 2, and IPU/Vivante interrupts to CPU 1. Software encoder and
compression threads must stay off CPU 3 so they do not compete with the audio
path. Use normal or reduced scheduling priority, bound all network writes, and
prefer frame drops over work accumulation.
