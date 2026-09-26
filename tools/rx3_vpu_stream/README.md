# RX3 hardware H.264 stream

`rx3-vpu-stream` keeps the i.MX6 IPU and VPU open while converting the active
RGB565 framebuffer to 640×400 I420 and encoding H.264 Baseline at 30 fps. It
listens for one TCP client on port 7353, reachable through the USB Link Export
address `169.254.100.2`. The encoder targets 3 Mbit/s with picture QP 20 and a
one-second GOP.

Build in the offline rootless Podman toolchain:

```sh
./tools/rx3_vpu_stream/build.sh \
  /path/to/imx-vpu-3.10.17-1.0.0/vpu \
  /path/to/extracted/rx3/rootfs \
  build/rx3-vpu-stream
```

The RX3 needs `mxc_vpu.ko` loaded with the discovered DRAM limit and the VPU
firmware and `libvpu.so.4` installed in `/tmp/rx3-vpu`:

```sh
insmod /tmp/mxc_vpu.ko dram_top=0x4fffffff
LD_LIBRARY_PATH=/tmp/rx3-vpu VPU_FW_PATH=/tmp/rx3-vpu \
  /tmp/rx3-vpu-stream
```

Each TCP record starts with a 24-byte network-order header described by
`!4sBBH I Q I`: magic `RX3H`, version 1, record type, flags, sequence, monotonic
PTS in microseconds, and payload length. Type 1 carries Annex-B SPS/PPS, type 2
carries one Annex-B access unit, and flag bit 0 marks an IDR/configuration
record.

Three IPU DMA buffers rotate through capture and synchronous VPU submission.
Each access unit has a 100 ms send deadline. A client that cannot accept the
current record is disconnected so frames never build up in userspace;
reconnecting receives SPS/PPS and a forced IDR before dependent frames.

## Live result

Firmware 1.19 sustained 90 frames in 2.97 seconds, or 30.33 fps. The capture
contained one SPS/PPS configuration record, three IDRs, and 87 P-frames.
FFmpeg decoded every frame as 640×400 YUV420 Constrained Baseline H.264 level
3.0. That initial QP 23 test used approximately 1.01 Mbit/s. The QP 20 quality
setting sustained 30.06 fps; Chromium measured 2.07 Mbit/s and 3 ms decode
latency during a moving waveform. Raising only the target rate to 3 Mbit/s
retained 30.01 fps; Chromium measured 3.13 Mbit/s and 2 ms decode latency.
