"""Relay RX3 H.264 video and PCM audio to browsers without transcoding."""

from __future__ import annotations

import argparse
import base64
from collections import deque
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
import socket
import struct
import threading
import time

from .protocol import ACCESS_UNIT, CONFIG, Decoder, Message, ProtocolError
from tools.rx3_audio.protocol import (
    CONFIG as AUDIO_CONFIG,
    PCM,
    Decoder as AudioDecoder,
    Message as AudioMessage,
    ProtocolError as AudioProtocolError,
)


LOG = logging.getLogger("rx3-h264-relay")
_WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


INDEX_HTML = r"""<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>RX3 Live</title>
<style>
  :root { color-scheme: dark; font-family: ui-monospace, SFMono-Regular, monospace }
  * { box-sizing: border-box }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center;
         background: #080b10; color: #dbe7f7 }
  main { width: min(100vw, 1400px); padding: 12px }
  header { display: flex; flex-wrap: wrap; gap: 18px; align-items: baseline;
           padding: 0 2px 10px; font-size: 13px; color: #91a4bc }
  strong { color: #f3f7fc; font-size: 15px }
  canvas { display: block; width: 100%; height: auto; aspect-ratio: 8 / 5;
           background: #000; border: 1px solid #263244; border-radius: 6px }
  .ok { color: #65e6a5 } .bad { color: #ff7c85 }
  button { border: 1px solid #526784; border-radius: 4px; padding: 5px 10px;
           background: #182231; color: #f3f7fc; font: inherit }
</style>
<main>
  <header><strong>RX3 LIVE</strong><span id="state">connecting</span>
    <span id="rate">—</span><span id="bandwidth">—</span><span id="latency">—</span>
    <button id="audio">Start audio</button><span id="audio-state">audio idle</span></header>
  <canvas id="display" width="640" height="400"></canvas>
</main>
<script>
const state = document.querySelector('#state');
const rate = document.querySelector('#rate');
const bandwidth = document.querySelector('#bandwidth');
const latency = document.querySelector('#latency');
const canvas = document.querySelector('#display');
const audioButton = document.querySelector('#audio');
const audioState = document.querySelector('#audio-state');
const context = canvas.getContext('2d', {alpha: false, desynchronized: true});
let decoder, configPrefix, frames = 0, bytes = 0, statsAt = performance.now();
let awaitingKeyframe = true;
const arrivals = new Map();
let audioContext, audioNode, audioSocket, audioBufferedMs = 0;
let sourceAudioRate = 44100, resamplePosition = 1, resampleTail = null;
const AUDIO_PREROLL_MS = 125;

function configure() {
  if (!('VideoDecoder' in globalThis)) {
    state.textContent = 'WebCodecs unavailable'; state.className = 'bad'; return false;
  }
  if (decoder) decoder.close();
  arrivals.clear(); awaitingKeyframe = true;
  decoder = new VideoDecoder({
    output(frame) {
      if (canvas.width !== frame.displayWidth || canvas.height !== frame.displayHeight) {
        canvas.width = frame.displayWidth; canvas.height = frame.displayHeight;
      }
      const arrivedAt = arrivals.get(frame.timestamp);
      if (arrivedAt !== undefined) {
        const decodeMs = (performance.now() - arrivedAt).toFixed(0);
        latency.textContent = audioContext && audioContext.state === 'running' ?
          `${decodeMs} ms video · ${AUDIO_PREROLL_MS} ms target` : `${decodeMs} ms decode`;
        arrivals.delete(frame.timestamp);
      }
      context.drawImage(frame, 0, 0, canvas.width, canvas.height);
      frame.close(); frames++;
    },
    error(error) { state.textContent = `decoder: ${error.message}`; state.className = 'bad'; }
  });
  decoder.configure({codec: 'avc1.42401E', optimizeForLatency: true});
  return true;
}

function uint64(view, offset) {
  return Number(view.getBigUint64(offset));
}

function receive(buffer) {
  const view = new DataView(buffer);
  if (view.byteLength < 24 || view.getUint32(0) !== 0x52583348 || view.getUint8(4) !== 1) return;
  const type = view.getUint8(5), flags = view.getUint16(6), timestamp = uint64(view, 12);
  const length = view.getUint32(20);
  if (24 + length !== view.byteLength) return;
  bytes += view.byteLength;
  const payload = new Uint8Array(buffer, 24, length);
  if (type === 1) { configPrefix = payload.slice(); return; }
  if (type !== 2 || !decoder || decoder.state === 'closed') return;

  const keyframe = Boolean(flags & 1);
  if (awaitingKeyframe && !keyframe) return;
  let data = payload;
  if (keyframe && configPrefix) {
    data = new Uint8Array(configPrefix.length + payload.length);
    data.set(configPrefix); data.set(payload, configPrefix.length);
  }
  awaitingKeyframe = false;
  arrivals.set(timestamp, performance.now());
  const chunk = new EncodedVideoChunk({type: keyframe ? 'key' : 'delta', timestamp, data});
  const targetDecoder = decoder;
  const decode = () => {
    if (decoder !== targetDecoder || targetDecoder.state === 'closed') return;
    if (targetDecoder.decodeQueueSize > 12) { configure(); return; }
    targetDecoder.decode(chunk);
  };
  if (audioContext && audioContext.state === 'running') setTimeout(decode, AUDIO_PREROLL_MS);
  else decode();

  const now = performance.now(), elapsed = now - statsAt;
  if (elapsed >= 1000) {
    rate.textContent = `${(frames * 1000 / elapsed).toFixed(1)} fps`;
    bandwidth.textContent = `${(bytes * 8 / elapsed / 1000).toFixed(2)} Mbit/s`;
    frames = 0; bytes = 0; statsAt = now;
  }
}

function connect() {
  if (!configure()) return;
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const websocket = new WebSocket(`${scheme}://${location.host}/stream`);
  websocket.binaryType = 'arraybuffer';
  websocket.onopen = () => { state.textContent = 'connected'; state.className = 'ok'; };
  websocket.onmessage = event => receive(event.data);
  websocket.onclose = () => {
    state.textContent = 'reconnecting'; state.className = 'bad';
    setTimeout(connect, 1000);
  };
  websocket.onerror = () => websocket.close();
}
connect();

const workletSource = `
class Rx3PcmPlayer extends AudioWorkletProcessor {
  constructor() {
    super();
    this.channels = [new Float32Array(sampleRate * 2), new Float32Array(sampleRate * 2)];
    this.read = 0; this.write = 0; this.available = 0; this.primed = false;
    this.target = Math.round(sampleRate * ${AUDIO_PREROLL_MS} / 1000);
    this.reportAt = 0;
    this.port.onmessage = ({data}) => {
      if (data.reset) { this.read = this.write = this.available = 0; this.primed = false; return; }
      const left = data.left, right = data.right;
      const capacity = this.channels[0].length;
      const needed = Math.max(0, this.available + left.length - capacity);
      this.read = (this.read + needed) % capacity; this.available -= needed;
      for (let i = 0; i < left.length; i++) {
        this.channels[0][this.write] = left[i]; this.channels[1][this.write] = right[i];
        this.write = (this.write + 1) % capacity;
      }
      this.available += left.length;
      if (!this.primed && this.available >= this.target) this.primed = true;
    };
  }
  process(inputs, outputs) {
    const output = outputs[0], count = output[0].length, capacity = this.channels[0].length;
    if (this.primed && this.available < count) this.primed = false;
    for (let i = 0; i < count; i++) {
      const have = this.primed && this.available > 0;
      output[0][i] = have ? this.channels[0][this.read] : 0;
      output[1][i] = have ? this.channels[1][this.read] : 0;
      if (have) { this.read = (this.read + 1) % capacity; this.available--; }
    }
    this.reportAt += count;
    if (this.reportAt >= sampleRate / 4) {
      this.port.postMessage({bufferedMs: this.available * 1000 / sampleRate, primed: this.primed});
      this.reportAt = 0;
    }
    return true;
  }
}
registerProcessor('rx3-pcm-player', Rx3PcmPlayer);`;

function connectAudio() {
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  audioSocket = new WebSocket(`${scheme}://${location.host}/audio`);
  audioSocket.binaryType = 'arraybuffer';
  audioSocket.onopen = () => { audioState.textContent = 'audio buffering'; audioState.className = 'ok'; };
  audioSocket.onmessage = ({data}) => {
    const view = new DataView(data);
    const blocks = [];
    let offset = 0, totalFrames = 0;
    while (offset + 28 <= view.byteLength) {
      if (view.getUint32(offset) !== 0x52583341 || view.getUint8(offset + 4) !== 1) return;
      const type = view.getUint8(offset + 5), length = view.getUint32(offset + 12);
      if (offset + 28 + length > view.byteLength) return;
      if (type === 1) {
        const rate = view.getUint32(offset + 28), channels = view.getUint16(offset + 32);
        const format = view.getUint16(offset + 34);
        if (channels !== 2 || format !== 1) {
          audioState.textContent = `unsupported ${rate} Hz/${channels}ch`; audioState.className = 'bad';
          audioSocket.close(); return;
        }
        if (rate !== sourceAudioRate) {
          sourceAudioRate = rate; resamplePosition = 1; resampleTail = null;
        }
      } else if (type === 2 && length % 4 === 0) {
        blocks.push([offset + 28, length / 4]); totalFrames += length / 4;
      }
      offset += 28 + length;
    }
    if (offset !== view.byteLength || totalFrames === 0) return;
    const sourceLeft = new Float32Array(totalFrames), sourceRight = new Float32Array(totalFrames);
    let output = 0;
    for (const [start, frames] of blocks) {
      for (let i = 0; i < frames; i++, output++) {
        sourceLeft[output] = view.getInt16(start + i * 4, true) / 32768;
        sourceRight[output] = view.getInt16(start + i * 4 + 2, true) / 32768;
      }
    }
    const [left, right] = resample(sourceLeft, sourceRight);
    audioNode.port.postMessage({left, right}, [left.buffer, right.buffer]);
  };
  audioSocket.onclose = () => {
    resamplePosition = 1; resampleTail = null;
    audioNode.port.postMessage({reset: true});
    audioState.textContent = 'audio reconnecting'; audioState.className = 'bad';
    setTimeout(connectAudio, 1000);
  };
  audioSocket.onerror = () => audioSocket.close();
}

function resample(sourceLeft, sourceRight) {
  if (sourceLeft.length === 0) return [sourceLeft, sourceRight];
  if (resampleTail === null) resampleTail = [sourceLeft[0], sourceRight[0]];
  const step = sourceAudioRate / audioContext.sampleRate;
  const capacity = Math.ceil((sourceLeft.length + 1) / step) + 1;
  const left = new Float32Array(capacity), right = new Float32Array(capacity);
  const sample = (channel, index) => index === 0 ? resampleTail[channel] :
    (channel === 0 ? sourceLeft[index - 1] : sourceRight[index - 1]);
  let count = 0;
  while (resamplePosition < sourceLeft.length) {
    const index = Math.floor(resamplePosition), fraction = resamplePosition - index;
    left[count] = sample(0, index) + (sample(0, index + 1) - sample(0, index)) * fraction;
    right[count] = sample(1, index) + (sample(1, index + 1) - sample(1, index)) * fraction;
    count++; resamplePosition += step;
  }
  resamplePosition -= sourceLeft.length;
  resampleTail = [sourceLeft[sourceLeft.length - 1], sourceRight[sourceRight.length - 1]];
  return [left.slice(0, count), right.slice(0, count)];
}

function unlockAudio() {
  const buffer = audioContext.createBuffer(1, 1, audioContext.sampleRate);
  const source = audioContext.createBufferSource();
  source.buffer = buffer;
  source.connect(audioContext.destination);
  source.start();
  return audioContext.resume();
}

function updateAudioControls() {
  const running = audioContext && audioContext.state === 'running';
  audioButton.disabled = Boolean(running && audioNode);
  audioButton.textContent = running && audioNode ? 'Audio running' : 'Resume audio';
  if (!running) {
    audioState.textContent = `audio ${audioContext ? audioContext.state : 'idle'} · tap Resume audio`;
    audioState.className = 'bad';
  }
}

audioButton.onclick = async () => {
  audioButton.disabled = true;
  try {
    if (audioContext) {
      await unlockAudio();
      updateAudioControls();
      return;
    }

    const AudioContextClass = globalThis.AudioContext || globalThis.webkitAudioContext;
    if (!AudioContextClass) throw new Error('Web Audio unavailable');
    audioContext = new AudioContextClass({latencyHint: 'interactive'});
    audioContext.onstatechange = updateAudioControls;

    // Invoke resume synchronously inside the tap. Mobile Safari expires its
    // user activation if addModule is awaited first.
    const resumed = unlockAudio();
    await resumed;

    const blob = new Blob([workletSource], {type: 'text/javascript'});
    const moduleUrl = URL.createObjectURL(blob);
    try {
      await audioContext.audioWorklet.addModule(moduleUrl);
    } finally {
      URL.revokeObjectURL(moduleUrl);
    }
    audioNode = new AudioWorkletNode(audioContext, 'rx3-pcm-player', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
    });
    audioNode.connect(audioContext.destination);
    audioNode.port.onmessage = ({data}) => {
      audioBufferedMs = data.bufferedMs;
      audioState.textContent = `audio ${data.primed ? 'live' : 'buffering'} · ${audioBufferedMs.toFixed(0)} ms`;
      audioState.className = data.primed ? 'ok' : '';
    };
    updateAudioControls();
    connectAudio();
  } catch (error) {
    audioState.textContent = `audio error: ${error.message}`;
    audioState.className = 'bad';
    audioButton.disabled = false;
    audioButton.textContent = 'Retry audio';
  }
};
</script>
</html>
"""


def _websocket_frame(payload: bytes) -> bytes:
    length = len(payload)
    if length < 126:
        return struct.pack("!BB", 0x82, length) + payload
    if length <= 0xFFFF:
        return struct.pack("!BBH", 0x82, 126, length) + payload
    return struct.pack("!BBQ", 0x82, 127, length) + payload


class Subscription:
    """A bounded GOP-aware queue that cannot backpressure the RX3."""

    def __init__(self, maximum: int = 64) -> None:
        self._condition = threading.Condition()
        self._messages: deque[bytes] = deque(maxlen=maximum)
        self._config: bytes | None = None
        self._waiting_for_keyframe = True
        self.closed = False
        self.dropped = 0

    def put(self, message: Message) -> None:
        with self._condition:
            if self.closed:
                return
            encoded = message.encode()
            if message.type == CONFIG:
                self._config = encoded
                self.dropped += len(self._messages)
                self._messages.clear()
                self._waiting_for_keyframe = True
                return
            if message.type != ACCESS_UNIT:
                return
            if self._waiting_for_keyframe and not message.is_keyframe:
                self.dropped += 1
                return
            if len(self._messages) == self._messages.maxlen:
                self.dropped += len(self._messages) + 1
                self._messages.clear()
                self._waiting_for_keyframe = True
                if not message.is_keyframe:
                    return
            if self._waiting_for_keyframe:
                if self._config is not None:
                    self._messages.append(self._config)
                self._waiting_for_keyframe = False
            self._messages.append(encoded)
            self._condition.notify()

    def get(self, timeout: float) -> bytes | None:
        with self._condition:
            if not self._messages and not self.closed:
                self._condition.wait(timeout)
            if self._messages:
                return self._messages.popleft()
            return None

    def close(self) -> None:
        with self._condition:
            self.closed = True
            self._condition.notify_all()


class RelayHub:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._clients: set[Subscription] = set()
        self._config: bytes | None = None
        self._connected = False
        self._last_message_at: float | None = None
        self._frame_times: deque[float] = deque()
        self._bytes = 0
        self._frames = 0
        self._reconnects = 0
        self._errors = 0

    def set_connected(self, connected: bool) -> None:
        with self._lock:
            self._connected = connected
            if connected:
                self._reconnects += 1

    def record_error(self) -> None:
        with self._lock:
            self._errors += 1

    def publish(self, message: Message) -> None:
        encoded = message.encode()
        now = time.monotonic()
        with self._lock:
            self._last_message_at = now
            self._bytes += len(encoded)
            if message.type == CONFIG:
                self._config = encoded
            elif message.type == ACCESS_UNIT:
                self._frames += 1
                self._frame_times.append(now)
                while self._frame_times and self._frame_times[0] < now - 5:
                    self._frame_times.popleft()
            clients = tuple(self._clients)

        for client in clients:
            client.put(message)

    def subscribe(self) -> Subscription:
        subscription = Subscription()
        with self._lock:
            self._clients.add(subscription)
            config = self._config
        if config is not None:
            decoder = Decoder()
            [message] = decoder.feed(config)
            subscription.put(message)
        return subscription

    def unsubscribe(self, subscription: Subscription) -> None:
        with self._lock:
            self._clients.discard(subscription)
        subscription.close()

    def health(self) -> dict[str, object]:
        now = time.monotonic()
        with self._lock:
            age = None if self._last_message_at is None else now - self._last_message_at
            frame_times = tuple(t for t in self._frame_times if t >= now - 5)
            span = now - frame_times[0] if frame_times else 0
            fps = len(frame_times) / min(5, span) if span > 0 else 0
            return {
                "ok": self._connected and age is not None and age < 3,
                "upstream_connected": self._connected,
                "last_message_age_seconds": None if age is None else round(age, 3),
                "fps_5s": round(fps, 2),
                "frames": self._frames,
                "bytes": self._bytes,
                "clients": len(self._clients),
                "client_drops": sum(client.dropped for client in self._clients),
                "connections": self._reconnects,
                "errors": self._errors,
            }


class AudioSubscription:
    """Bounded low-latency PCM queue for one browser."""

    def __init__(self, maximum: int = 256) -> None:
        self._condition = threading.Condition()
        self._messages: deque[bytes] = deque(maxlen=maximum)
        self._config: bytes | None = None
        self.closed = False
        self.dropped = 0

    def put(self, message: AudioMessage) -> None:
        with self._condition:
            if self.closed:
                return
            encoded = message.encode()
            if message.type == AUDIO_CONFIG:
                self._config = encoded
                self.dropped += len(self._messages)
                self._messages.clear()
                self._messages.append(encoded)
                self._condition.notify()
                return
            if message.type != PCM:
                return
            if len(self._messages) == self._messages.maxlen:
                self.dropped += len(self._messages)
                self._messages.clear()
                if self._config is not None:
                    self._messages.append(self._config)
            self._messages.append(encoded)
            self._condition.notify()

    def get(self, timeout: float) -> bytes | None:
        with self._condition:
            if not self._messages and not self.closed:
                self._condition.wait(timeout)
            return self._messages.popleft() if self._messages else None

    def get_batch(self, timeout: float, maximum: int = 4) -> bytes | None:
        with self._condition:
            if not self._messages and not self.closed:
                self._condition.wait(timeout)
            if not self._messages:
                return None
            messages = [self._messages.popleft()]
            while self._messages and len(messages) < maximum:
                messages.append(self._messages.popleft())
            return b"".join(messages)

    def close(self) -> None:
        with self._condition:
            self.closed = True
            self._condition.notify_all()


class AudioHub:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._clients: set[AudioSubscription] = set()
        self._config: AudioMessage | None = None
        self._connected = False
        self._last_message_at: float | None = None
        self._blocks = 0
        self._bytes = 0
        self._errors = 0

    def set_connected(self, connected: bool) -> None:
        with self._lock:
            self._connected = connected

    def record_error(self) -> None:
        with self._lock:
            self._errors += 1

    def publish(self, message: AudioMessage) -> None:
        now = time.monotonic()
        with self._lock:
            self._last_message_at = now
            self._bytes += len(message.payload) + 28
            if message.type == AUDIO_CONFIG:
                self._config = message
            elif message.type == PCM:
                self._blocks += 1
            clients = tuple(self._clients)
        for client in clients:
            client.put(message)

    def subscribe(self) -> AudioSubscription:
        subscription = AudioSubscription()
        with self._lock:
            self._clients.add(subscription)
            config = self._config
        if config is not None:
            subscription.put(config)
        return subscription

    def unsubscribe(self, subscription: AudioSubscription) -> None:
        with self._lock:
            self._clients.discard(subscription)
        subscription.close()

    def health(self) -> dict[str, object]:
        now = time.monotonic()
        with self._lock:
            age = None if self._last_message_at is None else now - self._last_message_at
            return {
                "ok": self._connected and age is not None and age < 3,
                "upstream_connected": self._connected,
                "last_message_age_seconds": None if age is None else round(age, 3),
                "blocks": self._blocks,
                "bytes": self._bytes,
                "clients": len(self._clients),
                "client_drops": sum(client.dropped for client in self._clients),
                "errors": self._errors,
            }


class Upstream(threading.Thread):
    def __init__(self, hub: RelayHub, host: str, port: int) -> None:
        super().__init__(name="rx3-h264-upstream", daemon=True)
        self.hub = hub
        self.host = host
        self.port = port

    def run(self) -> None:
        while True:
            try:
                self._receive()
            except (OSError, ProtocolError) as error:
                self.hub.record_error()
                LOG.warning("RX3 stream disconnected: %s", error)
            finally:
                self.hub.set_connected(False)
            time.sleep(1)

    def _receive(self) -> None:
        LOG.info("connecting to RX3 stream at %s:%d", self.host, self.port)
        with socket.create_connection((self.host, self.port), timeout=5) as upstream:
            upstream.settimeout(5)
            decoder = Decoder()
            self.hub.set_connected(True)
            while True:
                data = upstream.recv(256 * 1024)
                if not data:
                    raise ConnectionError("RX3 closed the stream")
                for message in decoder.feed(data):
                    self.hub.publish(message)


class AudioUpstream(threading.Thread):
    def __init__(self, hub: AudioHub, host: str, port: int) -> None:
        super().__init__(name="rx3-pcm-upstream", daemon=True)
        self.hub = hub
        self.host = host
        self.port = port

    def run(self) -> None:
        while True:
            try:
                self._receive()
            except (OSError, AudioProtocolError) as error:
                self.hub.record_error()
                LOG.warning("RX3 audio stream disconnected: %s", error)
            finally:
                self.hub.set_connected(False)
            time.sleep(1)

    def _receive(self) -> None:
        LOG.info("connecting to RX3 audio stream at %s:%d", self.host, self.port)
        with socket.create_connection((self.host, self.port), timeout=5) as upstream:
            upstream.settimeout(5)
            decoder = AudioDecoder()
            self.hub.set_connected(True)
            while True:
                data = upstream.recv(256 * 1024)
                if not data:
                    raise ConnectionError("RX3 closed the audio stream")
                for message in decoder.feed(data):
                    self.hub.publish(message)


def make_handler(hub: RelayHub, audio_hub: AudioHub | None = None):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path == "/":
                body = INDEX_HTML.encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)
                return

            if self.path == "/healthz":
                health = hub.health()
                if audio_hub is not None:
                    health["audio"] = audio_hub.health()
                body = json.dumps(health, separators=(",", ":")).encode()
                self.send_response(200 if health["ok"] else 503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)
                return

            if self.path == "/stream" and self.headers.get("Upgrade", "").lower() == "websocket":
                self._video_websocket()
                return

            if (self.path == "/audio" and audio_hub is not None and
                    self.headers.get("Upgrade", "").lower() == "websocket"):
                self._audio_websocket()
                return

            self.send_error(404)

        def _upgrade_websocket(self) -> bool:
            key = self.headers.get("Sec-WebSocket-Key")
            if not key:
                self.send_error(400, "missing WebSocket key")
                return False
            accept = base64.b64encode(hashlib.sha1(
                (key + _WEBSOCKET_GUID).encode()
            ).digest()).decode()
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()
            self.connection.settimeout(2)
            return True

        def _video_websocket(self) -> None:
            if not self._upgrade_websocket():
                return

            subscription = hub.subscribe()
            try:
                while not subscription.closed:
                    message = subscription.get(10)
                    if message is None:
                        self.connection.sendall(b"\x89\x00")
                    else:
                        self.connection.sendall(_websocket_frame(message))
            except OSError:
                pass
            finally:
                hub.unsubscribe(subscription)

        def _audio_websocket(self) -> None:
            if not self._upgrade_websocket() or audio_hub is None:
                return

            subscription = audio_hub.subscribe()
            try:
                while not subscription.closed:
                    message = subscription.get_batch(10)
                    if message is None:
                        self.connection.sendall(b"\x89\x00")
                    else:
                        self.connection.sendall(_websocket_frame(message))
            except OSError:
                pass
            finally:
                audio_hub.unsubscribe(subscription)

        def log_message(self, format: str, *args: object) -> None:
            LOG.info("%s - %s", self.client_address[0], format % args)

    return Handler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rx3-host", default="169.254.100.2")
    parser.add_argument("--rx3-port", type=int, default=7353)
    parser.add_argument("--rx3-audio-port", type=int, default=7355)
    parser.add_argument("--no-audio", action="store_true")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7353)
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    hub = RelayHub()
    Upstream(hub, args.rx3_host, args.rx3_port).start()
    audio_hub = None
    if not args.no_audio:
        audio_hub = AudioHub()
        AudioUpstream(audio_hub, args.rx3_host, args.rx3_audio_port).start()
    server = ThreadingHTTPServer((args.bind, args.port), make_handler(hub, audio_hub))
    LOG.info("browser viewer listening at http://%s:%d", args.bind, args.port)
    server.serve_forever()


if __name__ == "__main__":
    main()
