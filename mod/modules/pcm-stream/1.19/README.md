# Master PCM stream

This optional firmware 1.19 module serves the RX3 recorder bus as signed
16-bit little-endian stereo at 44,100 Hz over an ordinary TCP socket. It works
through the stock rear USB Ethernet interface or any other configured IPv4
interface, such as a Wi-Fi adapter enabled by the `usb-wifi` module. It does not
require or enable a root shell.

The module is guarded for `rbp` SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`. Its preload constructor verifies
the process path and hook bytes before modifying code. The original method is
always called with its original arguments and return value.

## Runtime behavior

The real-time hook copies complete `Float2` blocks into a preallocated SPSC
queue. It performs no allocation, file access, socket operation, lock, wait,
or sample conversion. A normal worker converts queued samples to PCM16 and
sends the framed stream to one TCP client.

TCP provides ordered delivery and retransmits lost network packets while a
connection is alive. A bounded queue absorbs short scheduler and network
stalls. If a client stops reading past the send deadline, the module closes
that connection instead of blocking the mixer. Audio produced while no client
is connected is not retained; a later client starts at the live edge. This is
a live PCM service rather than a durable recorder or replay service.

Every PCM message includes an absolute sample-frame timestamp, sequence number,
and cumulative sender-drop count. A receiver can detect any discontinuity.
The wire representation is lossless relative to the resulting PCM16 stream.
Conversion from the mixer's Float32 samples to PCM16 is quantization.

## Configuration

The module reads these environment variables when `rbp` starts:

| Variable | Default | Range or meaning |
| --- | --- | --- |
| `RX3_PCM_PORT` | `7355` | TCP port 1024 through 65535 |
| `RX3_PCM_BIND` | `0.0.0.0` | IPv4 address on which to listen |

`module.sh` supplies and exports the defaults. Binding to `0.0.0.0` makes the
stream reachable through every configured RX3 IPv4 interface.

The protocol carries no encryption or client authentication. Use it only on a
trusted USB link or trusted LAN.

## Client

Record until Ctrl-C from the repository root:

```sh
python -m tools.rx3_audio.recorder \
  --host RX3_ADDRESS --port 7355 --output rx3-master.wav
```

The client reconnects after a network interruption and reports sequence,
timestamp, and sender-side gaps. A reconnect is a discontinuity in a live
recording; it cannot recover audio generated while the socket was absent.

Runtime state is available in:

```text
/tmp/rx3-pcm-stream.ready
/tmp/rx3-pcm-stream.status
/tmp/rx3-pcm-stream.log
```

The tap is before the physical master-level control and attenuation. It
contains the completed deck, channel-fader, crossfader, and effects mix.
Microphone inclusion follows the RX3 recorder setting.
