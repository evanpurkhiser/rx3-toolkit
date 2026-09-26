<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 1.19 master PCM path

The firmware 1.19 player creates the complete master mix in software before
ALSA sends it to the output codec. A module can copy either the recorder bus or
the post-master-volume bus without reading an internal hardware link. All
addresses and layouts in this document apply to `/root/pdj/rbp` with SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`.

## Signal path

The recovered path is:

```text
decoded decks and external inputs
              │
              ▼
   PlayEngine::update
              │
              ▼
   MixerEngine::update
              │
              ▼
 MasterOutChannel::update ─────► WavWriter::copyBuffer
              │                    built-in recorder bus
              ▼
      MasterOut::update
       master volume/ATT
              │
              ▼
 DeviceRouteMngr output 0
              │
              ▼
 DjEngineIF::audioDeviceIOCallback
              │
              ▼
   ALSAThread float-to-S24
              │
              ▼
 snd_pcm_writei(hw:cs4344audiorev8,0)
              │
              ▼
        output codec/DAC
```

`DjEngineIF::audioDeviceIOCallback` at `0x00044534` receives the audio block,
clears its output buffers, publishes their pointers through
`DeviceRouteMngr`, and calls `PlayEngine::update` followed by
`MixerEngine::update`. `audioDeviceAboutToStart` at `0x000447b8` explicitly
checks for 44,100 Hz and initializes the equalizer, Beat FX, Sound Color FX,
and mixer with that rate and the negotiated block size.

The internal sample type is `common::Float2`: two adjacent 32-bit floats for
left and right, or eight bytes per stereo frame. `MasterOut::update` loads one
`Float2` per frame, applies the smoothed master gain to both channels, stores
the result directly in output-device buffer zero, and updates its output level
meter.

The output device enum is visible in each constructor:

| Device | Enum | Constructor evidence |
| --- | ---: | --- |
| Master | 0 | `MasterOut` passes 0 to `AbstractOutputDevice` |
| Headphones | 1 | `HeadPhone` passes 1 |
| Booth | 2 | `BoothMonitor` passes 2 |

The custom JUCE ALSA thread converts each float pair to signed 24-bit samples
in 32-bit slots, clamps to the 24-bit range, and makes three independent
`snd_pcm_writei` calls.
The firmware names the playback devices
`hw:cs4344audiorev8,0`, `hw:cs4344audiorev8,1`, and
`hw:cs4344audiorev8,2`. Live descriptor inspection identified these as master,
headphones, and booth respectively.

This means the hardware codec performs conversion and analog output. It does
not perform the deck mix, channel faders, crossfader, Beat FX, master summing,
or master level calculation.

## Existing recorder tap

The firmware already contains a master recorder. `MasterOutChannel::update`
at `0x00059490` calls:

```c
int WavWriter::copyBuffer(common::Float2 const *samples, int frames);
```

`WavWriter::copyBuffer` is at `0x0005bb48`. The method is called for every
mixed block and internally decides whether recording is active. Its caller can
select between two mixer buffers according to the microphone-recording
setting. The default recorder path includes the microphone. The surrounding
API includes:

The integer return was recovered from the caller and callee because C++ name
mangling does not encode it. The caller tests `r0` after the call; inactive
paths return zero and accepted recording paths explicitly return one. The
preload preserves that value when it forwards to the original method.

| Function | Address |
| --- | ---: |
| `MixerEngine::prepareRecording` | `0x00057744` |
| `MixerEngine::startRecording` | `0x00057754` |
| `MixerEngine::stopRecording` | `0x000577d4` |
| `MixerEngine::trackMark` | `0x00057864` |
| `WavWriter::copyBuffer` | `0x0005bb48` |

This recorder is independent confirmation that a software master PCM bus
exists. It also provides the simplest first hook because the arguments already
contain a stereo pointer and frame count. A wrapper can copy the block whether
or not the built-in recorder is active, then call the original method.

The recorder worker multiplies these floats by 32,768, saturates them to signed
16-bit stereo, and writes four bytes per frame into its 44.1 kHz WAV file.

## Candidate capture points

| Tap | Format | Level | Assessment |
| --- | --- | --- | --- |
| `WavWriter::copyBuffer`, `0x0005bb48` | Stereo `Float2`, 44.1 kHz | Recorder bus, before master level | Best first prototype; explicit pointer and frame count |
| `MasterOut::update`, `0x0005af70` | Stereo `Float2`, 44.1 kHz | After master level and attenuation | Exact digital feed supplied to the master ALSA route |
| `snd_pcm_writei` call at `0x003c55dc` | Stereo signed 24-bit in 32-bit slots | Final master device bytes | Exact hardware payload, but coupled to ALSA internals and one of three handles |

The verified first eight bytes for guarded hooks are:

| Function | Guard bytes |
| --- | --- |
| `MasterOutChannel::update` | `f8 40 2d e9 00 40 a0 e1` |
| `MasterOut::update` | `04 30 90 e5 70 40 2d e9` |
| `WavWriter::copyBuffer` | `f0 40 2d e9 00 40 a0 e1` |

The recorder tap is normally the useful stream for recording, broadcast, or
visualization because moving the physical master knob does not change its
level. The post-master tap is useful when the consumer must reproduce exactly
what is sent to the master DAC. Both include the completed deck mix; microphone
inclusion on the recorder bus follows its recorder setting and needs one live
capture to label definitively.

Opening an ALSA capture PCM does not provide a master monitor. The three
capture endpoints on card 1 are physical Input 2, microphone, and Input 1. A
second process also cannot read samples back from an ALSA playback handle.

## Stock rear USB audio

The stock `g_pmulti` audio function cannot send this bus to a computer. Its
audio streaming endpoint is host-to-RX3 playback at 44.1 kHz, 16-bit stereo.
The apparent device-to-host isochronous endpoint emits dummy implicit-feedback
data used for clock control.

The GPL source states that the function supports playback only. Its
`/dev/paudiog0` file operations expose no `read` or `write`; a private ioctl
copies USB playback frames from the driver into `rbp`. Consequently, selecting
the RX3 as a recording input on a host cannot recover the master stream through
the current gadget implementation.

A bidirectional USB Audio Class modification remains possible, but it would
require changing `g_pmulti`, adding a genuine capture streaming interface, and
feeding it continuously from `rbp`. The existing USB Link Ethernet interface
is a much smaller first implementation.

## Streaming design

The audio callback must remain real-time safe. A capture module should:

1. install one guarded trampoline while `rbp` starts;
2. preallocate a single-producer/single-consumer ring buffer;
3. copy each `Float2` block into the ring without allocation, locks, file I/O,
   or network calls;
4. let a normal-priority worker consume the ring and frame the stream;
5. drop a complete block and increment a visible counter if the consumer falls
   behind.

A raw TCP service on the existing USB Link address `169.254.100.2` is enough
for the first version. Each connection should begin with a small header carrying
the protocol version, sample rate, channel count, sample format, starting frame
counter, and tap identity. Subsequent chunks should include the first frame
counter and dropped-frame total so a client can detect discontinuities.

The continuous bandwidth is modest:

| Wire format | Bytes/second | Bit rate |
| --- | ---: | ---: |
| Float32 stereo | 352,800 | 2.82 Mbit/s |
| Packed 24-bit stereo | 264,600 | 2.12 Mbit/s |
| Signed 16-bit stereo | 176,400 | 1.41 Mbit/s |

Float32 avoids conversion in the audio thread. The worker can optionally emit
signed 16-bit little-endian PCM for simpler clients. All formats fit easily
within the rear 100 Mbit/s USB Ethernet adapter.

## Live PCM stream result

The firmware 1.19 hardware prototype hooked `WavWriter::copyBuffer`, copied each
block into a 32-slot SPSC ring, converted to PCM16 in a nice-10 worker, and
served it on TCP port 7355 over USB Link Ethernet. Its common `RX3A` framing
carried the first-frame counter and cumulative dropped-frame count for every
block. The standalone module now uses a larger split sample/descriptor queue and
bounded nonblocking sends; that implementation still needs a hardware smoke
test.

An idle-device capture on September 24, 2026 produced 14.93 seconds of valid
44.1 kHz, 16-bit stereo WAV data. The hook delivered 10,291 64-frame blocks at
1.4112 Mbit/s with no sequence gaps, timestamp gaps, or sender drops. All
samples were zero because neither deck was playing.

A second 56.66-second capture included 18 seconds of playback audio and
delivered 39,041 blocks through one connection with no gaps or sender drops.
Both channels contained signal, with overall peaks of -13.38 dBFS left and
-11.64 dBFS right. FFmpeg decoded the complete WAV without errors. Channel
identity and microphone-setting behavior still require controlled source
tests.

## FLAC compression measurement

The active 18.158-second portion of that capture contains 800,774 stereo
PCM16 frames and occupies 3,203,174 bytes as WAV. FLAC 1.5.0 produced these
results:

| FLAC level | Bytes | PCM size | Saved | Average bit rate |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 1,937,733 | 60.5% | 39.5% | 854 kbit/s |
| 1 | 1,868,295 | 58.3% | 41.7% | 823 kbit/s |
| 3 | 1,848,245 | 57.7% | 42.3% | 814 kbit/s |
| 5 | 1,767,826 | 55.2% | 44.8% | 779 kbit/s |
| 8 | 1,740,548 | 54.3% | 45.7% | 767 kbit/s |

The full 56.658-second capture shrinks from 9,994,540 bytes to 1,959,322
bytes at level 0 because 38.5 seconds are digital silence. That 80.4% saving
does not represent continuous program audio. A zero-run record in the stream
protocol can represent idle periods cheaply without invoking a codec.

Level 0 cuts active-audio traffic from 1.4112 Mbit/s to about 0.854 Mbit/s.
Level 8 saves only another 87 kbit/s. The host benchmark cannot predict ARM
CPU use, but it shows that the fastest preset captures most of the available
compression and is the appropriate first live benchmark.

Firmware 1.19 already ships ARM EABI5 `libFLAC.so.8.3.0`, identified as
reference libFLAC 1.3.3. Its exported API includes streaming encoder creation,
compression-level selection, interleaved processing, and finish functions.
The RX3 can therefore measure presets against the actual audio workload without
installing a codec library. The i.MX6 VPU does not accelerate FLAC; encoding
uses a Cortex-A9 core.
