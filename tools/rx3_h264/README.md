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

The viewer is then available at `http://127.0.0.1:7353/` and, with the server's
existing nginx tailnet proxy, at `https://7353.prk.network/`. The JSON health
endpoint is `/healthz`.

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

The tailnet HTTPS and secure WebSocket path sustained 30.0 fps at 1.00 Mbit/s.
Chromium's WebCodecs decoder displayed the live screen with a measured 2 ms
receive-to-display decode delay. The JSON health endpoint reported a stable
upstream connection with no errors during the final run.
