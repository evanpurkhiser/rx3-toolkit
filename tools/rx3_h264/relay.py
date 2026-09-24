"""Relay a framed RX3 Annex-B stream to browsers using WebCodecs."""

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


LOG = logging.getLogger("rx3-h264-relay")
_WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


INDEX_HTML = r"""<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>RX3 H.264</title>
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
</style>
<main>
  <header><strong>RX3 H.264 LIVE</strong><span id="state">connecting</span>
    <span id="rate">—</span><span id="bandwidth">—</span><span id="latency">—</span></header>
  <canvas id="display" width="640" height="400"></canvas>
</main>
<script>
const state = document.querySelector('#state');
const rate = document.querySelector('#rate');
const bandwidth = document.querySelector('#bandwidth');
const latency = document.querySelector('#latency');
const canvas = document.querySelector('#display');
const context = canvas.getContext('2d', {alpha: false, desynchronized: true});
let decoder, configPrefix, frames = 0, bytes = 0, statsAt = performance.now();
let awaitingKeyframe = true;
const arrivals = new Map();

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
      context.drawImage(frame, 0, 0, canvas.width, canvas.height);
      const arrivedAt = arrivals.get(frame.timestamp);
      if (arrivedAt !== undefined) {
        latency.textContent = `${(performance.now() - arrivedAt).toFixed(0)} ms decode`;
        arrivals.delete(frame.timestamp);
      }
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
  if (decoder.decodeQueueSize > 12) { configure(); return; }
  awaitingKeyframe = false;
  arrivals.set(timestamp, performance.now());
  decoder.decode(new EncodedVideoChunk({type: keyframe ? 'key' : 'delta', timestamp, data}));

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


def make_handler(hub: RelayHub):
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
                body = json.dumps(health, separators=(",", ":")).encode()
                self.send_response(200 if health["ok"] else 503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)
                return

            if self.path == "/stream" and self.headers.get("Upgrade", "").lower() == "websocket":
                self._websocket()
                return

            self.send_error(404)

        def _websocket(self) -> None:
            key = self.headers.get("Sec-WebSocket-Key")
            if not key:
                self.send_error(400, "missing WebSocket key")
                return
            accept = base64.b64encode(hashlib.sha1(
                (key + _WEBSOCKET_GUID).encode()
            ).digest()).decode()
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()

            subscription = hub.subscribe()
            self.connection.settimeout(2)
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

        def log_message(self, format: str, *args: object) -> None:
            LOG.info("%s - %s", self.client_address[0], format % args)

    return Handler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rx3-host", default="169.254.100.2")
    parser.add_argument("--rx3-port", type=int, default=7353)
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
    server = ThreadingHTTPServer((args.bind, args.port), make_handler(hub))
    LOG.info("browser viewer listening at http://%s:%d", args.bind, args.port)
    server.serve_forever()


if __name__ == "__main__":
    main()
