# RX3 PCM recorder

This host-side recorder receives framed PCM from an RX3 over TCP and writes it
directly to a seekable WAV file. The sender declares its format before sending
audio; the recorder accepts signed 16-bit little-endian PCM and validates the
sample rate and channel count.

Run it from the repository root:

```sh
python -m tools.rx3_audio.recorder \
  --host 169.254.100.2 \
  --port 7355 \
  --output /tmp/rx3-audio.wav
```

The recorder reconnects when the socket or device-side process restarts and
continues the same WAV when the format is unchanged. It preserves the expected
sample-frame cursor across connections, so its final statistics report audio
that was unavailable while disconnected and timestamp resets caused by an
`rbp` restart. It rewrites the RIFF and data lengths once per statistics
interval and on shutdown, keeping the recording playable during a long
capture. A RIFF/WAV file holds at most 4 GiB, about 6 hours 45 minutes for this
stream; the recorder stops before exceeding that limit. TCP backpressure, a
256 KiB receive buffer, and a 1 MiB protocol
payload limit bound host memory use.

## Wire protocol

Every message begins with this 28-byte network-byte-order header:

| Field | Type | Meaning |
| --- | --- | --- |
| magic | 4 bytes | `RX3A` |
| version | u8 | `1` |
| type | u8 | config `1`, PCM `2` |
| flags | u16 | reserved, send zero |
| sequence | u32 | increments for each message, wrapping naturally |
| payload length | u32 | bytes following the header |
| timestamp | u64 | index of the first interleaved sample frame |
| dropped frames | u32 | cumulative sender-side dropped sample frames |

The config payload is `sample_rate:u32, channels:u16, format:u16,
max_frames_per_block:u32`, also in network byte order. Format `1` is interleaved
signed 16-bit little-endian PCM. A PCM payload contains complete interleaved
sample frames. The recorder reports sequence gaps, timestamp gaps, and changes
in the sender drop counter separately.
