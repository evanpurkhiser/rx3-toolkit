# RX3 PCM recorder

This host-side proof of concept receives framed PCM from an RX3 process over
the USB Ethernet link and writes it directly to a seekable WAV file. The sender
declares its format before sending audio; the recorder currently accepts signed
16-bit little-endian PCM and validates the sample rate and channel count.

Run it from the repository root:

```sh
python -m tools.rx3_audio.recorder \
  --host 169.254.100.2 \
  --port 7355 \
  --output /tmp/rx3-audio.wav
```

The recorder reconnects when the device-side process restarts. A reconnect may
continue the same WAV when the stream configuration is unchanged. It rewrites
the RIFF and data lengths once per statistics interval and on shutdown, so the
recording remains playable during a long capture. TCP backpressure, a 256 KiB
receive buffer, and a 1 MiB protocol payload limit bound host memory use.

## Wire protocol

Every message begins with this 28-byte network-byte-order header:

| Field | Type | Meaning |
| --- | --- | --- |
| magic | 4 bytes | `RX3A` |
| version | u8 | `1` |
| type | u8 | config `1`, PCM `2`, heartbeat `3` |
| flags | u16 | reserved, send zero |
| sequence | u32 | increments for each message, wrapping naturally |
| payload length | u32 | bytes following the header |
| timestamp | u64 | index of the first interleaved sample frame |
| dropped frames | u32 | cumulative sender-side dropped sample frames |

The config payload is `sample_rate:u32, channels:u16, format:u16,
frames_per_block:u32`, also in network byte order. Format `1` is interleaved
signed 16-bit little-endian PCM. A PCM payload contains complete interleaved
sample frames. The recorder reports sequence gaps, timestamp gaps, and changes
in the sender drop counter separately.
