<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 IPU capture proof of concept

`rx3-ipu-capture` submits one read-only framebuffer conversion to the i.MX6
IPU. It uses the active `/dev/fb0` RGB565 page as input, allocates a private
DMA output through `/dev/mxc_ipu`, scales to 640x400, converts to planar I420,
and writes the 384,000-byte result to `/tmp/rx3-ipu-frame.i420` by default.

The program is intentionally one-shot. It proves the hardware capture stage
without introducing an encoder, network activity, or a long-running workload.

Live validation on firmware 1.19 completed the conversion and file write in
50 ms to `/tmp` and 90 ms to a USB FAT filesystem. The 640x400 I420 result was
visually compared with the simultaneous RGB565 stream; crop, scale, geometry,
and color all matched. The display remained responsive and the kernel logged
no errors.

## Build

Build inside the offline reverse-engineering container:

```sh
podman run --rm --network=none \
  -v "$PWD:/work" -w /work \
  localhost/rx3-reverse:latest \
  tools/rx3_ipu_capture/build.sh
```

The result is a static ARMv7 EABI executable with direct Linux syscalls, so it
does not depend on the firmware's glibc version. Verify it on the host before
copying it anywhere:

```sh
file build/rx3-ipu-capture
readelf -h build/rx3-ipu-capture
```

## First-device test

The first run should happen from the diagnostic shell while audio is stopped or
idle. Copy the executable to `/tmp`, then run:

```sh
chmod 755 /tmp/rx3-ipu-capture
/tmp/rx3-ipu-capture /tmp/rx3-ipu-frame.i420
wc -c /tmp/rx3-ipu-frame.i420
```

Expected size: `384000` bytes. Copy the frame to the companion and inspect it:

```sh
ffmpeg -f rawvideo -pixel_format yuv420p -video_size 640x400 \
  -i rx3-ipu-frame.i420 -frames:v 1 rx3-ipu-frame.png
```

Do not loop the program during the first test. Record elapsed time, `dmesg`, and
whether the RX3 display remains responsive before attempting sustained capture.

## Safety and ownership

`FBIOGET_FSCREENINFO` supplies the physical framebuffer allocation and stride;
`FBIOGET_VSCREENINFO` supplies the active crop. The program checks the complete
visible region against `smem_len` before submitting its address. Firmware 1.19
currently reports one 1280x800 page, a 2,560-byte stride, and zero offsets.
Wrapped scanout and geometry that the IPU would round to eight-pixel boundaries
are rejected instead of capturing a shifted or partial frame.

The framebuffer physical address is input-only. The IPU output is a separate,
page-aligned coherent DMA allocation created with `IPU_ALLOC`, mapped with
`MAP_SHARED`, and released with `IPU_FREE`. Passing the page-aligned size avoids
the vendor driver's mismatch between its page-rounded allocation record and
the size passed to `dma_alloc_coherent`.

The IPU device worker arbitrates the IC viewfinder and postprocess channels and
waits when both are occupied. It does not use the display channel. The vendor
driver applies its one-second default timeout, and every successful allocation
is released on each error path.

The framebuffer itself is write-combined DMA memory. DirectFB may update it
while the IPU reads, so a frame can tear across a presentation boundary. This
cannot corrupt scanout because the task never writes to framebuffer memory.
The final streamer should trigger after presentation or tolerate a dropped or
torn frame rather than blocking the UI.

The mapped IPU output is coherent DMA memory. `IPU_QUEUE_TASK` returns only
after the completion interrupt and channel teardown, so the CPU can consume
the I420 bytes after the ioctl returns. A later VPU handoff must preserve this
ownership boundary and keep two or three output buffers so capture never
overwrites a frame still owned by the encoder.

## ABI provenance

The ioctl numbers and 136-byte `struct ipu_task` layout come from firmware
1.19's exact `include/linux/ipu.h` and `drivers/mxc/ipu3/ipu_device.c` sources.
The interface is 32-bit ARM-specific. `IPU_ALLOC` returns a physical address in
the same integer supplied with the requested allocation size; `mmap2` uses that
physical page number as its offset.
