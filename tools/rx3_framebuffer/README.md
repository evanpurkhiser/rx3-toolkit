# RX3 framebuffer receiver

The receiver reconstructs the RGB565 screen from the framebuffer-stream
module's dirty rectangles. It reports the delivered frame rate, wire bandwidth,
and rectangles per frame.

Open an `ffplay` window on the receiving machine:

```sh
python3 -m tools.rx3_framebuffer.cli
```

Run a browser viewer on Evan's server:

```sh
python3 -m tools.rx3_framebuffer.cli --no-display --web-port 7352
```

The server binds only to `127.0.0.1`. Tailnet nginx publishes it at
<https://7352.prk.network/>. The page uses a native WebSocket and canvas and
downloads no dependencies. A viewer that joins an active stream receives a
complete host-generated keyframe before subsequent dirty rectangles.
Modern browsers decode LZ4 deltas with a small bounded JavaScript decoder, then
apply only their changed RGB565 runs to the canvas backing image. The zlib
fallback uses `DecompressionStream`.

Compare codec sizes against a captured RX3 framebuffer and any sequential
`rx3-frame-*.raw` files beside it:

```sh
python3 -m tools.rx3_framebuffer.benchmark_codec /tmp/rx3-fb0.raw
```

The RX3 endpoint defaults to `169.254.100.2:7351`. Override it with `--host`
and `--port` when diagnosing another interface or a recorded producer.

Capture the current reconstructed display as a dependency-free PNG:

```sh
python3 -m tools.rx3_framebuffer.snapshot /tmp/rx3-screen.png
```

The command waits for a complete framebuffer keyframe, applies another 250 ms
of dirty rectangles by default, and writes the visible 1280×800 RGB image.
