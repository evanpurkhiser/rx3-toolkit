# RX3 H.264 browser relay

This host-side relay connects to the RX3 hardware H.264 streamer and forwards
complete Annex-B access units to browsers over a WebSocket. Browsers decode the
stream directly with WebCodecs, so the relay does not transcode or require
FFmpeg.

Run it from the repository root:

```bash
python3 -m tools.rx3_h264.relay \
  --rx3-host 169.254.100.2 --rx3-port 7353 \
  --bind 127.0.0.1 --port 7353
```

The viewer is then available at `http://127.0.0.1:7353/`. The JSON health
endpoint is `/healthz`. Bind to another address or place a TLS reverse proxy in
front of the relay when serving a separate browser.

## Wire format

Each TCP and WebSocket message begins with this 24-byte network-order header:

```text
magic[4] = RX3H
version  u8 = 1
type     u8 = 1 config, 2 access unit, 3 heartbeat
flags   u16 = bit 0 set for an IDR access unit
sequence u32
timestamp_us u64
payload_length u32
payload[payload_length]
```

The config payload contains Annex-B SPS and PPS NAL units. Access-unit payloads
contain one complete Annex-B access unit. The relay retains the latest config
for newly connected browsers, reconnects to the RX3 after disconnects, and
drops stale GOPs for slow browser clients and resumes at the next IDR frame.

## Live result

The initial HTTPS and secure WebSocket path sustained 30.0 fps at 1.00
Mbit/s. After increasing the encoder target to 2 Mbit/s and lowering picture QP
from 23 to 20, the relay sustained 30.06 fps. Chromium measured 2.07 Mbit/s and
3 ms from WebSocket receipt to canvas display while showing a moving waveform.
At a 3 Mbit/s encoder target, it retained 30.01 fps while Chromium measured
3.13 Mbit/s and 2 ms decode latency.
