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

The NXP 3.0.101 release materials list
`imx-vpu-lib-3.0.101-4.1.1.tar.gz`, making that release line the best initial
match for firmware 1.19. VPU firmware and libraries must come from the same BSP
generation because their command and microcode interfaces are coupled. NXP's
[i.MX6 Linux reference manual](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/imx-processors/87230/3/Linux_6DQ_RM_L3.0.35_1.1.0.pdf)
describes the vendor driver, `imx-vpu-lib`, and firmware arrangement.

The vendor kernel driver cannot be compiled unchanged as a loadable module in
this RX3 kernel. Its initialization calls `memblock_analyze()` and
`memblock_end_of_DRAM_with_reserved()`, neither of which is exported to modules;
the kernel also has `CONFIG_MODVERSIONS=y`, so every remaining kernel symbol
must match its recorded CRC. The two unavailable calls only establish the upper
physical-memory bound used by
`VPU_IOC_PHYMEM_CHECK`. A module-safe version should accept the validated RAM
limit as a read-only module parameter or replace the check with equivalent
resource validation. Removing the bound entirely would make a bad physical
address capable of hanging the system.

The platform device already carries the VPU register, IPI IRQ, JPEG IRQ,
reset, IRAM, clock, and regulator data. Loading the corrected module should bind
to that existing device and create `/dev/mxc_vpu`; a custom kernel image is not
required for the first experiment.

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

## Recommended phases

1. Add inexpensive compression to the existing dirty-rectangle protocol and
   record delivered frame rate, payload rate, and RX3 CPU use.
2. Exercise `/dev/mxc_ipu` with a read-only framebuffer input and a private
   640x400 YUV420 DMA output buffer. Verify scaling, colors, active-page
   selection, and latency without involving the VPU.
3. Feed the IPU output to the installed `tjCompressFromYUV` implementation and
   serve MJPEG. This validates the complete capture and browser path before
   introducing a kernel module.
4. Build and load the corrected `mxc_vpu.ko`, then validate `/dev/mxc_vpu` with
   one-frame MJPEG encoding using matching microcode and `libvpu`.
5. Switch the VPU to H.264 Baseline and add companion-side fragmented MP4 or
   WebRTC packaging. Measure end-to-end latency, encoder bitrate, dropped
   frames, audio stability, and UI responsiveness under moving waveforms.

The RX3 boots with `isolcpus=3`. Its IRQ policy assigns SDMA to CPU 3, USB and
Ethernet to CPU 2, and IPU/Vivante interrupts to CPU 1. Software encoder and
compression threads must stay off CPU 3 so they do not compete with the audio
path. Use normal or reduced scheduling priority, bound all network writes, and
prefer frame drops over work accumulation.
