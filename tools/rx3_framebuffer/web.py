"""Dependency-free browser viewer for the RX3 framebuffer stream."""

from __future__ import annotations

import base64
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import socket
import struct
import threading
import time

from .protocol import (
    FLAG_KEYFRAME,
    FRAME,
    FRAME_PREFIX,
    HELLO,
    HELLO_PAYLOAD,
    PIXEL_RGB565_LE,
    RECT_PREFIX,
    Framebuffer,
    Message,
    StreamInfo,
    encode_message,
)


_WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

INDEX_HTML = r"""<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>RX3 display</title>
<style>
  :root { color-scheme: dark; font-family: ui-monospace, SFMono-Regular, monospace }
  * { box-sizing: border-box }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center;
         background: #080b10; color: #dbe7f7 }
  main { width: min(100vw, 1400px); padding: 12px }
  header { display: flex; gap: 18px; align-items: baseline; padding: 0 2px 10px;
           font-size: 13px; color: #91a4bc }
  strong { color: #f3f7fc; font-size: 15px }
  canvas { display: block; width: 100%; height: auto; background: #000;
           border: 1px solid #263244; border-radius: 6px; image-rendering: auto }
  .ok { color: #65e6a5 } .bad { color: #ff7c85 }
</style>
<main>
  <header><strong>RX3 LIVE</strong><span id="state">connecting</span>
    <span id="geometry">—</span><span id="rate">—</span><span id="bandwidth">—</span></header>
  <canvas id="display"></canvas>
</main>
<script>
const canvas = document.querySelector('#display');
const context = canvas.getContext('2d', {alpha: false});
const state = document.querySelector('#state');
const geometry = document.querySelector('#geometry');
const rate = document.querySelector('#rate');
const bandwidth = document.querySelector('#bandwidth');
let image, pixels, rgb565, dirty = false, frames = 0, bytes = 0, statsAt = performance.now();

function render() {
  if (dirty && image) { context.putImageData(image, 0, 0); dirty = false; }
  requestAnimationFrame(render);
}
requestAnimationFrame(render);

function decompressLz4(data, maximum) {
  const output = new Uint8Array(maximum);
  let source = 0, destination = 0;
  const readLength = initial => {
    let length = initial;
    if (length !== 15) return length;
    while (true) {
      if (source >= data.length) throw new Error('truncated LZ4 length');
      const extension = data[source++]; length += extension;
      if (extension !== 255) return length;
    }
  };
  while (source < data.length) {
    const token = data[source++], literalLength = readLength(token >> 4);
    if (source + literalLength > data.length || destination + literalLength > maximum)
      throw new Error('invalid LZ4 literals');
    output.set(data.subarray(source, source + literalLength), destination);
    source += literalLength; destination += literalLength;
    if (source === data.length) return output.subarray(0, destination);
    if (source + 2 > data.length) throw new Error('truncated LZ4 offset');
    const matchOffset = data[source] | (data[source + 1] << 8); source += 2;
    if (!matchOffset || matchOffset > destination) throw new Error('invalid LZ4 offset');
    const matchLength = readLength(token & 15) + 4;
    if (destination + matchLength > maximum) throw new Error('oversized LZ4 block');
    for (let index = 0; index < matchLength; index++) {
      output[destination] = output[destination - matchOffset]; destination++;
    }
  }
  throw new Error('LZ4 block has no final literals');
}

async function receive(buffer) {
  const view = new DataView(buffer);
  if (view.byteLength < 16 || view.getUint32(0) !== 0x52583346 || view.getUint8(4) !== 1) return;
  const type = view.getUint8(5), length = view.getUint32(12);
  if (length + 16 !== view.byteLength) return;
  bytes += view.byteLength;
  if (type === 1) {
    const width = view.getUint16(16), height = view.getUint16(18);
    canvas.width = width; canvas.height = height;
    image = context.createImageData(width, height); pixels = image.data;
    rgb565 = new Uint16Array(width * height);
    geometry.textContent = `${width}×${height} RGB565`;
    return;
  }
  if (type !== 2 || !image || length < 12) return;
  let offset = 28;
  const count = view.getUint16(24);
  for (let rectangle = 0; rectangle < count; rectangle++) {
    if (offset + 12 > view.byteLength) return;
    const x = view.getUint16(offset), y = view.getUint16(offset + 2);
    const width = view.getUint16(offset + 4), height = view.getUint16(offset + 6);
    const encodedLength = view.getUint32(offset + 8), codec = encodedLength >>> 30;
    const dataLength = encodedLength & 0x3fffffff; offset += 12;
    if (offset + dataLength > view.byteLength) return;
    let pixel = 0;
    const writePixel = (index, value) => {
      const row = Math.floor(index / width), column = index - row * width;
      const destination = ((y + row) * canvas.width + x + column) * 4;
      rgb565[(y + row) * canvas.width + x + column] = value;
      pixels[destination] = ((value >> 11) & 31) * 255 / 31;
      pixels[destination + 1] = ((value >> 5) & 63) * 255 / 63;
      pixels[destination + 2] = (value & 31) * 255 / 31;
      pixels[destination + 3] = 255;
    };
    if (codec === 0) {
      if (dataLength !== width * height * 2) return;
      let source = offset;
      while (pixel < width * height) {
        writePixel(pixel++, view.getUint8(source) | (view.getUint8(source + 1) << 8));
        source += 2;
      }
    } else if (codec === 1 || codec === 2 || codec === 3) {
      let encoded = new Uint8Array(buffer, offset, dataLength);
      if (codec === 2) {
        encoded = decompressLz4(encoded, width * height * 2);
      } else if (codec === 3) {
        if (!globalThis.DecompressionStream) return;
        const stream = new Blob([encoded]).stream().pipeThrough(
          new DecompressionStream('deflate')
        );
        encoded = new Uint8Array(await new Response(stream).arrayBuffer());
      }
      let source = 0;
      while (source < encoded.length && pixel < width * height) {
        const control = encoded[source++], run = (control & 0x7f) + 1;
        if (pixel + run > width * height) return;
        if (control & 0x80) {
          for (let index = 0; index < run; index++) {
            const row = Math.floor(pixel / width), column = pixel - row * width;
            const destination = ((y + row) * canvas.width + x + column) * 4;
            const old = rgb565[(y + row) * canvas.width + x + column];
            const delta = encoded[source] | (encoded[source + 1] << 8);
            writePixel(pixel++, old ^ delta); source += 2;
          }
        } else pixel += run;
      }
      if (pixel !== width * height || source !== encoded.length) return;
    } else return;
    offset += dataLength;
  }
  frames++; dirty = true;
  const now = performance.now(), elapsed = now - statsAt;
  if (elapsed >= 1000) {
    rate.textContent = `${(frames * 1000 / elapsed).toFixed(1)} fps`;
    bandwidth.textContent = `${(bytes * 8 / elapsed / 1000).toFixed(2)} Mbit/s`;
    frames = 0; bytes = 0; statsAt = now;
  }
}

function connect() {
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const websocket = new WebSocket(`${scheme}://${location.host}/stream`);
  websocket.binaryType = 'arraybuffer';
  websocket.onopen = () => { state.textContent = 'connected'; state.className = 'ok'; };
  let receiveQueue = Promise.resolve();
  websocket.onmessage = event => {
    receiveQueue = receiveQueue.then(() => receive(event.data)).catch(() => websocket.close());
  };
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
        header = struct.pack("!BB", 0x82, length)
    elif length <= 0xFFFF:
        header = struct.pack("!BBH", 0x82, 126, length)
    else:
        header = struct.pack("!BBQ", 0x82, 127, length)
    return header + payload


def _visible_frame(framebuffer: Framebuffer) -> bytes:
    info = framebuffer.info
    row_bytes = info.width * 2
    if info.stride == row_bytes:
        return bytes(framebuffer.pixels)
    return b"".join(
        framebuffer.pixels[row * info.stride:row * info.stride + row_bytes]
        for row in range(info.height)
    )


class ViewerHub:
    """Publish protocol messages and supply a snapshot to newly joined viewers."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._clients: set[socket.socket] = set()
        self._framebuffer: Framebuffer | None = None

    def configure(self, info: StreamInfo) -> Framebuffer:
        with self._lock:
            self._framebuffer = Framebuffer(info)
            hello = HELLO_PAYLOAD.pack(
                info.width, info.height, info.stride,
                PIXEL_RGB565_LE, info.tile_size,
            )
            self._broadcast_locked(encode_message(HELLO, 0, hello))
            return self._framebuffer

    def apply_and_publish(self, message: Message):
        with self._lock:
            if self._framebuffer is None:
                raise RuntimeError("framebuffer viewer was not configured")
            update = self._framebuffer.apply(message)
            self._broadcast_locked(encode_message(
                message.type, message.sequence, message.payload, message.flags
            ))
            return update, self._framebuffer

    def add(self, client: socket.socket) -> None:
        client.settimeout(1)
        with self._lock:
            framebuffer = self._framebuffer
            if framebuffer is not None:
                info = framebuffer.info
                hello = HELLO_PAYLOAD.pack(
                    info.width, info.height, info.stride,
                    PIXEL_RGB565_LE, info.tile_size,
                )
                if not self._send(client, encode_message(HELLO, 0, hello)):
                    return

                pixels = _visible_frame(framebuffer)
                payload = FRAME_PREFIX.pack(time.monotonic_ns(), 1, 0)
                payload += RECT_PREFIX.pack(
                    0, 0, info.width, info.height, len(pixels)
                ) + pixels
                sequence = framebuffer.last_sequence or 0
                if not self._send(client, encode_message(
                    FRAME, sequence, payload, FLAG_KEYFRAME
                )):
                    return
            client.settimeout(0.25)
            self._clients.add(client)

    def remove(self, client: socket.socket) -> None:
        with self._lock:
            self._clients.discard(client)

    def _broadcast_locked(self, payload: bytes) -> None:
        failed = [client for client in self._clients if not self._send(client, payload)]
        for client in failed:
            self._clients.discard(client)
            client.close()

    @staticmethod
    def _send(client: socket.socket, payload: bytes) -> bool:
        try:
            client.sendall(_websocket_frame(payload))
            return True
        except OSError:
            return False


class ViewerServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], hub: ViewerHub):
        self.hub = hub
        super().__init__(address, ViewerHandler)


class ViewerHandler(BaseHTTPRequestHandler):
    server: ViewerServer

    def do_GET(self) -> None:
        if self.path == "/stream" and self.headers.get("Upgrade", "").lower() == "websocket":
            self._websocket()
            return
        if self.path not in ("/", "/index.html"):
            self.send_error(404)
            return

        body = INDEX_HTML.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _websocket(self) -> None:
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            self.send_error(400, "missing WebSocket key")
            return
        accept = base64.b64encode(
            hashlib.sha1((key + _WEBSOCKET_GUID).encode()).digest()
        ).decode()
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.wfile.flush()

        client = self.connection
        self.server.hub.add(client)
        try:
            while True:
                try:
                    if not client.recv(1024):
                        break
                except TimeoutError:
                    continue
        except OSError:
            pass
        finally:
            self.server.hub.remove(client)

    def log_message(self, _format: str, *args: object) -> None:
        return


def start_server(hub: ViewerHub, port: int) -> ViewerServer:
    server = ViewerServer(("127.0.0.1", port), hub)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server
