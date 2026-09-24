# RX3 live browser relay

This host-side relay connects independently to the RX3 hardware H.264 streamer
and PCM capture module, then forwards both streams to browsers. Browsers decode
H.264 with WebCodecs and play PCM with an AudioWorklet, so the relay does not
transcode or require FFmpeg. Audio starts only after pressing **Start audio**,
as required by mobile browser autoplay policies.

Run it from the repository root:

```bash
python3 -m tools.rx3_h264.relay \
  --rx3-host 169.254.100.2 --rx3-port 7353 \
  --rx3-audio-port 7355 \
  --bind 127.0.0.1 --port 7353
```

The viewer is then available at `http://127.0.0.1:7353/` and, with the server's
existing nginx tailnet proxy, at `https://7353.prk.network/`. The JSON health
endpoint is `/healthz`.

The RX3 connections stay separate: video uses TCP 7353 and audio uses TCP
7355. The browser endpoints are `/stream` and `/audio`. A slow video client
drops an entire stale GOP; a slow audio client drops its queued PCM and resumes
from the newest block. Audio WebSocket frames batch up to eight RX3 blocks to
avoid sending hundreds of tiny WebSocket messages per second. The AudioWorklet
has a bounded two-second ring and begins playback with 150 ms buffered.
The browser linearly resamples the RX3's 44.1 kHz stream when a phone fixes its
AudioContext to a different hardware rate such as 48 kHz.

## A/V timing

The current device protocols expose independent counters: microseconds from
the video process and sample frames from the audio hook. They do not expose a
shared start epoch. The viewer therefore treats audio as the playback clock
and delays video presentation by the same 150 ms used for PCM preroll. This
keeps the streams close on the low-jitter USB Ethernet link, but it is arrival-
time synchronization rather than sample-accurate synchronization. A shared
device monotonic timestamp in both protocol headers would allow the relay to
calculate and continuously correct the exact offset.

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

The initial tailnet HTTPS and secure WebSocket path sustained 30.0 fps at 1.00
Mbit/s. After increasing the encoder target to 2 Mbit/s and lowering picture QP
from 23 to 20, the relay sustained 30.06 fps. Chromium measured 2.07 Mbit/s and
3 ms from WebSocket receipt to canvas display while showing a moving waveform.
At a 3 Mbit/s encoder target, it retained 30.01 fps while Chromium measured
3.13 Mbit/s and 2 ms decode latency.
