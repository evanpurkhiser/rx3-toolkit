<!-- SPDX-License-Identifier: MPL-2.0 -->
# Framebuffer stream

This module streams `/dev/fb0` over any configured RX3 network interface. It
queries the active resolution, stride, bit depth, color bitfields, and visible
offset at runtime. It listens on TCP port 7351; the validated rear USB-B Link
Export endpoint is `169.254.100.2:7351`. Network setup is intentionally outside
this module so it can also run over a configured USB Wi-Fi interface.

The first frame sent to a client is a complete keyframe. Later frames encode
the full-frame RGB565 XOR as skip/literal runs and compress those runs with a
self-contained LZ4 block encoder. The firmware's zlib at level 1 and a 32x32
tile XOR codec remain available as successive fallbacks. The worker scans at
30 frames per second by default; set
`RX3_FB_FPS` to an integer from 1 through 30 before `rbp` starts to select a
lower rate. `RX3_FB_PORT` may select another unprivileged TCP port.

Only one client is served at a time. Socket writes are nonblocking and have a
one-second deadline. A receiver that cannot keep up is disconnected and may
reconnect for a fresh keyframe. Capture and networking happen on a detached
worker and never run from the display or audio threads.

## Protocol v1

Integers use network byte order. Each packet starts with a packed 16-byte
header:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `RX3F` |
| 4 | 1 | Protocol version `1` |
| 5 | 1 | Message type |
| 6 | 2 | Flags; bit 0 marks a keyframe |
| 8 | 4 | Sequence number |
| 12 | 4 | Payload byte count |

Message type 1 describes the width, height, stride, RGB565LE pixel format, and
tile size. Type 2 contains a monotonic nanosecond timestamp and rectangle count,
followed by rectangle headers and pixel data. The top two bits of a rectangle's
32-bit data length select its codec: zero is raw RGB565, one is XOR skip/literal
RLE, two is LZ4-compressed XOR-RLE, and three is zlib-compressed XOR-RLE. The
remaining 30 bits hold the encoded byte count. Flag bit 0 marks a complete
keyframe. An unchanged frame is omitted.

An XOR-RLE control byte represents 1 through 128 pixels. A clear high bit skips
pixels already present in the receiver; a set high bit is followed by one
little-endian RGB565 XOR value per pixel. Runs may stop at framebuffer row
boundaries. Codec two uses the standard raw LZ4 block format. Codec three
applies a standard zlib wrapper to the whole RLE byte stream.

The stream reads the physical framebuffer asynchronously, so a rectangle can
contain tearing if the display changes while its rows are copied.
