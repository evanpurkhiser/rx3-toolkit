# RX3 1.19 VPU userspace bring-up

The matching userspace stack is small. The Linux `L3.0.101_4.1.1` release
uses VPU library 5.4.23, firmware 3.1.1.46056, and VPU wrapper 1.0.46. A
minimal encoder does not need the wrapper or GStreamer. It needs `libvpu`, the
i.MX6Q firmware file, libc, and libpthread after `/dev/mxc_vpu` exists.

## Matching packages

Freescale's
[3.0.101_4.1.1 release notes](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/imx-processors/80811/1/MX_6_Linux_3.0.101_4.1.1_Patch_Release_Notes.pdf)
name these packages:

| Package | Content |
| --- | --- |
| `imx-vpu-lib-3.0.101-4.1.1.tar.gz` | `libvpu` 5.4.23 |
| `firmware-imx-3.0.101-4.1.1.tar.gz` | i.MX firmware, including VPU microcode |
| `libfslvpuwrap-1.0.46.tar.gz` | Optional higher-level codec wrapper |

The AlphaTheta RX3
[GPL download page](https://www.pioneerdj.com/hi-in/support/open-source-code-distribution/gnu-open-source-license/)
also lists the complete `IMX_MMCODEC_3.0.101_4.1.1_Bundle.tar.gz` and split
`L3.0.101_4.1.1_141016_source.tar.gz` archive. Its historical asset URLs are:

```text
https://www.pioneerdj.com/-/media/pioneerdj/downloads/opensource-code/gnu/xdj-rx3/imx_mmcodec_30101_411_bundletar.zip
https://www.pioneerdj.com/-/media/pioneerdj/downloads/opensource-code/gnu/xdj-rx3/l30101_411_141016_sourcetargz00.zip
https://www.pioneerdj.com/-/media/pioneerdj/downloads/opensource-code/gnu/xdj-rx3/l30101_411_141016_sourcetargz01.zip
```

Those asset URLs currently redirect to the new AlphaTheta support site and
return HTTP 403. None of the packages were present under `/home/evan` before
this investigation.

The matching sources remain available from the Gateworks Freescale package
mirror. The 3.10.17 package has the same 5.4.23 library named by the 3.0.101
release:

```text
https://dev.gateworks.com/sources/imx-vpu-3.10.17-1.0.0.bin
SHA256 cd8a7bd50ff3274db76a331cc6622d3ba4bb7c790ce778f303e49187df2dfd72

https://dev.gateworks.com/sources/firmware-imx-3.10.17-1.0.0.bin
SHA256 768d857dfc1bec344fbd95665e450030d5a0d541a695027f6d7815e0309bea37

https://dev.gateworks.com/sources/imx-lib-3.10.17-1.0.0.tar.gz
SHA256 f42605971977e5fe1ed9e7ce17ea3f97586a23fbc60fa0f679940d379c72303e
```

The extracted i.MX6Q microcode is 253,968 bytes:

```text
firmware/vpu/vpu_fw_imx6q.bin
SHA256 cbe061cb143e3c9d21a0a5647d49a8eb3f01e7fe5a12822a966333209a113fd0
header: platform `MX6Q`, 126976 16-bit firmware words
```

`libvpu` loads it from `/lib/firmware/vpu/vpu_fw_imx6q.bin`. The
`VPU_FW_PATH` environment variable can point at another directory during
bring-up.

## ABI and dependencies

RX3 userspace is ARM EABI5, ARMv7-A, soft-float calling convention with VFPv3
and NEON instructions available. Build flags should include:

```text
-march=armv7-a -mfpu=neon -mfloat-abi=softfp
```

The root filesystem uses glibc 2.13 and `/lib/ld-linux.so.3`. Link against the
RX3 sysroot so a newer build host does not introduce newer GLIBC symbol
versions.

`libvpu.so.4` links only libc and libpthread. It opens `/dev/mxc_vpu`, maps the
register aperture, asks the driver for physical and process-shared working
memory, uploads the microcode, and waits for VPU interrupts. The non-Android
build uses `pthread_mutex_timedlock`, which the RX3's libpthread exports as
`GLIBC_2.4`.

The vendor `mxc_vpu_test` links `libvpu`, `libipu`, `librt`, and libpthread
because one binary includes capture, display, loopback, and transcoding.
A file-to-VPU one-frame program does not need `libipu` or `librt`: the VPU
driver allocates and maps the input, reference, work, and bitstream buffers.
The later framebuffer streamer will use `libipu` or direct `/dev/mxc_ipu`
ioctls to scale and convert RGB565 to YUV420.

## Smallest encoder validation

The shortest initial validation is the Freescale `mxc_vpu_test` file-input
path. A 640 by 400 planar YUV420 frame is exactly 384,000 bytes. With a build
adapted to the 5.4.23 headers, one H.264 frame is:

```sh
mxc_vpu_test.out -E \
  "-i frame.i420 -o frame.264 -w 640 -h 400 -f 2 -c 1 -g 1 -b 1000"
```

For one independent JPEG frame, change the format to 7 and output name:

```sh
mxc_vpu_test.out -E \
  "-i frame.i420 -o frame.jpg -w 640 -h 400 -f 7 -c 1"
```

The available 3.14.28 test source is slightly newer than libvpu 5.4.23. Its
encoder uses several later fields and commands (`ENC_GET_VIDEO_HEADER`, AVC
VUI fields, and `ENC_ENABLE_SOF_STUFF`). Those references must be removed or
changed to the 5.4.23 equivalents; the central calls remain compatible.

A smaller dedicated POC should perform this sequence:

1. Call `vpu_Init` and check library and firmware versions.
2. Allocate a bitstream buffer with `IOGetPhyMem`.
3. Open an AVC or MJPEG encoder with `vpu_EncOpen`.
4. Query initial information and allocate the minimum reference framebuffers.
5. Register them with `vpu_EncRegisterFrameBuffer`.
6. Allocate one source framebuffer, copy the I420 planes, and call
   `vpu_EncStartOneFrame`.
7. Wait with `vpu_IsBusy` and `vpu_WaitForInt`, then call
   `vpu_EncGetOutputInfo` and write the returned bitstream bytes.
8. Close the encoder, free physical buffers, and call `vpu_UnInit`.

Live validation on firmware 1.19 completed every step above. The corrected
kernel module created `/dev/mxc_vpu`; `vpu_Init` reported firmware 3.1.1 build
46056 and library 5.4.23; and the vendor encoder produced a 640x400 H.264
Baseline level 3.0 IDR frame. The VPU reported 40.12 encoder frames per second
and 30.78 aggregate frames per second for the one-frame file test. The encoded
access unit was 12,451 bytes and contained SPS, PPS, and IDR NAL units.

The remaining implementation work is a persistent capture and encode loop plus
browser transport. The hardware stack itself is validated on the RX3.
