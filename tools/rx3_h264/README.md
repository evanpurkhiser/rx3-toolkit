<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 H.264 TCP client

The RX3 streamer sends framed Annex-B H.264 over TCP port 7353. This small
client removes the framing and writes the SPS/PPS and access units directly to
a file or standard output.

Capture 300 frames:

    python3 -m tools.rx3_h264.client --frames 300 --output capture.h264

Feed a local decoder without an intermediate file:

    python3 -m tools.rx3_h264.client | ffplay -fflags nobuffer -f h264 -

The default device address is 169.254.100.2. Use --host and --port for a
different network configuration. Status is written to standard error so
standard output remains a clean H.264 byte stream.

Each TCP record starts with a 24-byte network-order header described by
!4sBBH I Q I: magic RX3H, version 1, record type, flags, sequence, monotonic
timestamp in microseconds, and payload length. Type 1 carries Annex-B SPS/PPS,
type 2 carries one Annex-B access unit, and flag bit 0 marks a keyframe. The
client validates framing, sequence continuity, and that configuration precedes
video.
