# RX3 master PCM stream

The `pcm-stream` prototype hooks the firmware 1.19 master-recorder bus and
serves signed 16-bit little-endian stereo PCM at 44,100 Hz on TCP port 7355.
The stream consumes 176,400 bytes per second before TCP overhead.

Build the hook with Clang and LLD inside the offline rootless Podman image:

```sh
./tools/rx3_pcm_stream/build.sh /tmp/rx3-pcm-build
```

The runtime packager normally supplies a real firmware key and includes the
required USB Link root-shell dependency.

Record on the computer until Ctrl-C with the host-side recorder:

```sh
python -m tools.rx3_audio.recorder \
  --host 169.254.100.2 --port 7355 --output /tmp/rx3-master.wav
```

The sender follows the `tools/rx3_audio/protocol.py` framing exactly. Every
config and PCM message begins with its 28-byte `!4sBBHIIQI` header. The config
declares 44,100 Hz, two channels, PCM16LE, and the observed firmware block size.
PCM messages carry the first sample-frame index and cumulative dropped-frame
counter in the common header, followed by complete interleaved sample frames.

## Hook and real-time boundary

`WavWriter::copyBuffer(common::Float2 const *, int)` is the guarded hook at
`0x0005bb48` in rbp SHA-1 `cf309238491e73cdbdc1f08a09f7a3177e079068`.
The ARM ABI is `r0 = this`, `r1 = interleaved stereo Float2`, `r2 = frames`,
with an integer status result in `r0`. The verified entry bytes are
`f0 40 2d e9 00 40 a0 e1`.

The return type is recovered from the caller and callee rather than C++ name
mangling, which omits return types. The caller compares `r0` with zero after
the call. The inactive callee path returns the zero byte it loaded into `r0`,
while its accepted paths explicitly load `1` into `r0` before returning.

The callback copies a complete block into a preallocated 32-slot SPSC ring and
publishes its write index with an ARM memory barrier. It performs no allocation,
locking, file operation, socket operation, conversion, or wait. A full ring or
a block above 4,096 frames is dropped whole and counted. A nice-10 worker owns
float-to-PCM16 conversion and the TCP socket. Its 20 ms send deadline disconnects
a stalled client rather than allowing receiver backpressure into the audio
thread.

The preload constructor verifies `/proc/self/exe == /root/pdj/rbp` before
reading or patching the fixed address. Processes launched by rbp may inherit
`LD_PRELOAD`; they load the library but leave their text untouched.

This tap is before the physical master-level knob and attenuation. It receives
the completed deck/channel/crossfader/FX mix. Microphone inclusion follows the
RX3 recorder setting: the caller selects its no-mic buffer or its buffer with
the talk-over-processed microphone before invoking this method. One live
comparison with the built-in recorder remains necessary to label that setting
and verify left/right polarity.

## Hardware result

The firmware 1.19 hardware run on September 24, 2026 first captured 14.93
seconds while the player was idle. The receiver observed 10,291 64-frame
blocks at the exact 1.4112 Mbit/s PCM payload rate, with one TCP connection,
no sequence gaps, no timestamp gaps, and no sender-side drops. Every sample was
zero because neither deck was playing.

A second 56.66-second capture included 18 seconds of playback audio. It
delivered 39,041 blocks through one connection with no sequence gaps, timestamp
gaps, or sender-side drops. Both channels contained signal, with overall peaks
of -13.38 dBFS left and -11.64 dBFS right. FFmpeg decoded the complete WAV
without errors. This validates the hook, ring, conversion, framing, USB
Ethernet path, and host WAV writer together. Channel identity and the recorder
microphone setting still need controlled source tests.
