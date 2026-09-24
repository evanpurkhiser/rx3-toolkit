# RX3 1.19 display capture and streaming

Firmware 1.19 exposes enough of the display pipeline to stream the player screen
over the rear USB-B network connection. A direct read of `/dev/fb0` on a live
unit produced the exact 1280x800 LCD image without disturbing the player. A
sustained stream can therefore read the active framebuffer from a worker and use
the player's presentation function only as a notification source.

## Confirmed display path

The active display is 1280x800 at 16 bits per pixel. The runtime exposes three
framebuffers:

| Device | Geometry | Kernel name | Role |
| --- | --- | --- | --- |
| `/dev/fb0` | 1280x800, 16 bpp | `DISP3 BG` | Main display |
| `/dev/fb1` | 240x960 virtual, 16 bpp | `DISP3 FG` | Auxiliary foreground |
| `/dev/fb2` | 1280x800, 16 bpp | `DISP3 BG - DI1` | Secondary output path |

The live `fb0` mode reports a 1280x800 virtual size, 2,560-byte stride, and
RGB bitfields `5/11,6/5,5/0,0/0`: little-endian RGB565 with one 2,048,000-byte
page. A prior process map shows `rbp` mapping exactly `0x1f4000` bytes from
`/dev/fb0`, matching that complete frame.

`rbp` opens `/dev/fb0`, `/dev/galcore`, and `/dev/mem`. Its proprietary HMI
composites windows through DirectFB 1.4 and the Vivante GAL driver:

```text
HMI controls
  -> DS_HW window surfaces
  -> DS_HW_UpdateScreen dirty-region composition
  -> primary IDirectFBSurface::Flip
  -> Vivante GAL / i.MX IPU
  -> /dev/fb0
```

The matching Linux source makes direct capture especially promising. The RX3
`mxc_ipuv3_fb` driver does not override `fb_read`, so the kernel's generic
`fb_read` implementation copies from `info->screen_base`. Reading `/dev/fb0`
therefore returns framebuffer memory rather than a synthetic or write-only
device. The equal physical and virtual dimensions rule out a hidden panned page
in the observed configuration.

The application itself confirms the direct mapping contract.
`ui_com_draw_AllClearDirectly` at `0x001920d4` opens `/dev/fb0`, queries
`FBIOGET_VSCREENINFO`, maps the reported frame size, clears it, and unmaps it.

The product initializes DirectFB with `no-hardware`, `no-cursor`, `no-vt`, and
`no-vt-switch`, among other options. This favors a coherent CPU-visible primary
surface even though the firmware also contains Vivante acceleration paths.

The HMI reports a separate logical coordinate space of 800x480 through
`NS_GetScreenWidth` at `0x00277ea8` and `NS_GetScreenHeight` at `0x00277eb0`.
`/dev/fb0` returns the 1280x800 physical scanout after that logical UI has been
scaled and composed.

The main presentation function is `DS_HW_UpdateScreen` at `0x001a6528`. It
maintains dirty rectangles in the selected 0xa4-byte layer record, blits changed
window regions into the primary surface, and invokes the primary DirectFB
surface's `Flip` method with `DSFLIP_WAITFORSYNC`. The function clears the dirty
rectangles after presentation and enforces a roughly 16 ms update interval.

The primary surface is reachable through `g_surfaceInfo` at `0x02458918`; the
DirectFB surface used by the final flip is stored at `g_surfaceInfo + 0x1c`.
These related functions are also available:

| Symbol | Address | Behavior |
| --- | ---: | --- |
| `DS_HW_Core_Surface_Lock` | `0x001a44c0` | Calls `IDirectFBSurface::Lock` for CPU access |
| `DS_HW_Core_Surface_Unlock` | `0x001a4534` | Calls `IDirectFBSurface::Unlock` |
| `DS_HW_Core_Layer_Filp` | `0x001a4bd0` | Flips the primary surface and waits for sync |
| `DS_HW_UpdateScreen` | `0x001a6528` | Composites dirty windows and presents a layer |
| `DS_HW_FilpWindow` | `0x001a8268` | Maps a changed window region into its layer |
| `DS_Task_UpdateScreen` | `0x001b1770` | Higher-level display update entry point |
| `NS_HMIManager_Flush` | `0x001d065c` | Flushes pending HMI rendering |

The spelling `Filp` comes from the firmware's symbol names.

## Safe screenshot path

The validated one-shot capture reads one physical scanout page. The 512 KiB
`/tmp` mount is too small, so write the 2,048,000-byte result to the toolkit
drive:

```sh
mkdir -p /media/usb1/sda1/RX3_SCREEN
dd if=/dev/fb0 of=/media/usb1/sda1/RX3_SCREEN/fb0.raw \
  bs=2048000 count=1
```

The result is tightly packed RGB565LE at 1280x800. This command was run against
a live player and the decoded image matched the LCD exactly.

## DirectFB diagnostic hazard

The production system image contains unstripped ARM DirectFB tools:

```text
/usr/bin/arm-none-linux-gnueabi-dfbdump
/usr/bin/arm-none-linux-gnueabi-dfbinfo
/usr/bin/arm-none-linux-gnueabi-dfbinspector
/usr/bin/arm-none-linux-gnueabi-dfbscreen
```

The tools expose options for dumping primary layer surfaces or every front
buffer:

```text
--dumplayer    dump surfaces of layer contexts
--dumpsurface  dump the front buffer of every surface
```

Do not run these tools alongside `rbp`. A live `dfbdump --dumplayer` test failed
to join the player's Fusion world, initialized a second single-application
DirectFB core, changed the framebuffer mode, and left the LCD white until the
unit was rebooted. Their presence in the production image does not make them a
safe capture interface.

If the BusyBox `httpd` applet is enabled, the existing USB root-shell connection
can expose the directory for retrieval over `169.254.100.2`:

```sh
/bin/busybox httpd -p 8080 -h /media/usb1/sda1/RX3_SCREEN
```

Stop the server after copying the file.

The binary also contains a JPEG encoder: `write_jpeg` at `0x00177f6c` calls
`encodeFile` at `0x00238b6c`, which consumes RGB24 rows and uses quality 75.
Using it would require RGB565-to-RGB24 conversion on the RX3. Tile deltas with
host-side video encoding avoid that conversion and keep compression away from
the player process.

## Streaming design

The first streaming module avoids DirectFB calls and renderer hooks. A
normal-priority worker can map `/dev/fb0`, read the currently visible page using
`FBIOGET_FSCREENINFO`, `FBIOGET_VSCREENINFO`, `line_length`, and `yoffset`, and
send changed tiles over TCP. It scans only while one client is connected and
caps its configurable rate at 30 frames per second.

An event-driven revision can use two small components:

1. A guarded hook around `DS_HW_UpdateScreen` calls the original function and
   increments an atomic presentation generation. The wrapper performs no network
   I/O, allocation, compression, or framebuffer copying.
2. The worker coalesces generations and captures at its configured rate. This
   preserves event-driven idling while keeping all expensive work away from the
   UI thread.

The worker scans at a configurable rate up to 30 frames per second. Multiple
display updates between scans collapse into one frame. A slow or disconnected
client never backpressures the renderer; bounded nonblocking writes disconnect
the receiver, which reconnects for a fresh keyframe.

The prototype divides the screen into 32x32 RGB565 tiles. There are 40x25, or
1,000, tiles. Comparing one complete frame reads 2,048,000 bytes. The stream
sends:

```text
HELLO       protocol version, width, height, stride, pixel format
KEYFRAME    frame sequence and all tiles
DELTA       frame sequence and changed tile records
```

Each rectangle contains its bounds, codec, payload length, and pixel data. A
keyframe is sent on connection. Later frames encode the full-screen XOR as
alternating skip and literal runs, then compress that stream as a raw LZ4 block.
The firmware's zlib and a per-tile XOR codec are fallbacks. TCP preserves
ordering; a host that detects a sequence gap waits for a fresh connection and
keyframe. Fixed bounds and network byte order keep the receiver simple.

## Bandwidth

A complete 1280x800 RGB565 frame is 2,048,000 bytes:

| Mode | Payload rate |
| --- | ---: |
| 1 fps raw | 2.05 MB/s, 16.4 Mb/s |
| 5 fps raw | 10.24 MB/s, 81.9 Mb/s |
| 10 fps raw | 20.48 MB/s, 163.8 Mb/s |

Full-frame 10 fps is therefore unsuitable for a nominal 100 Mb/s link. Tile
deltas should be substantially smaller for menus and browser pages. Moving
waveforms are the demanding case and must be measured on hardware. The companion
reconstructs the RGB565 frame, converts it to RGBA, and supplies a browser canvas
or OBS source. Video encoding belongs on the companion.

If the DirectFB primary is the complete 800x480 logical UI, each RGB565 frame is
768,000 bytes. Its raw rates are 3.84 MB/s at 5 fps, 7.68 MB/s at 10 fps, and
11.52 MB/s at 15 fps. Locking that surface briefly after presentation may offer
a much smaller stream, but the direct framebuffer worker is the safer first
implementation.

## Live prototype

The `framebuffer-stream` runtime module implements the direct framebuffer design
on firmware 1.19. It maps `/dev/fb0` read-only, compares RGB565 pixels on a
detached worker, and listens on `169.254.100.2:7351`. The scan rate is
configurable from 1 through 30 Hz with `RX3_FB_FPS` and defaults to 30 Hz. The
worker blocks while no receiver is connected, and a slow receiver is
disconnected after a bounded send deadline.

The host receiver reconstructs the screen and can relay the same rectangle
messages to web browsers over WebSocket:

```sh
python3 -m tools.rx3_framebuffer.cli --no-display --web-port 7352
```

The browser converts RGB565 tiles to RGBA and paints a canvas. This keeps image
conversion and presentation off the player and gives each late viewer a complete
host-generated snapshot before live deltas. It also avoids encoding and decoding
a conventional video stream for the first proof of concept.

Live validation reproduced the full performance screen through tailnet HTTPS.
The initial raw-tile implementation delivered 1.8 to 1.9 changed frames per
second at 6.6 to 7.0 Mbit/s. Direct protocol profiling found 480 to 513 KiB per
moving update and matching source and arrival intervals around 545 ms. The host
was not the bottleneck.

Seven sequential live captures provided repeatable codec input:

| Encoding | Typical update |
| --- | ---: |
| Raw 32x32 dirty tiles | 492–511 KiB |
| Per-tile XOR skip/literal RLE | 269–276 KiB |
| Full-frame XOR with zlib level 1 | 41–44 KiB |
| Full-frame XOR-RLE with zlib level 1 | 26.8–28.9 KiB |
| Full-frame XOR-RLE with the module's LZ4 encoder | 34.0–36.2 KiB |

The firmware's zlib compressed one 2,048,000-byte XOR frame to 43,611 bytes in
339 ms on the RX3, making direct deflate too expensive. RLE reduces its input,
but a self-contained LZ4 block encoder gives similar wire size with predictable
CPU cost and no additional runtime library. The live module therefore tries LZ4
first, zlib second, and per-tile RLE last. A static player screen produced tiny
updates around 0.01 to 0.02 Mbit/s after this change. A moving-waveform live rate
still needs measurement after the player is reloaded following the module
restart.

## Live validation order

1. Query framebuffer fixed and variable info, including color bitfields, stride,
   virtual height, and current `yoffset`.
2. Copy one visible `/dev/fb0` page and decode it as 1280x800 RGB565LE.
3. Read full frames at 1 fps while monitoring UI responsiveness and audio.
4. Add tile comparison and TCP transport at 5 fps, then increase toward 30 fps.
5. Add the presentation hook so captures occur only after display updates.
6. Measure bandwidth and dropped frames on browser, waveform, and performance
   screens before choosing compression.

The remaining uncertainty is whether a concurrent `/dev/fb0` scan always sees a
tear-free completed presentation. A later notification hook immediately after
the flip would provide a natural frame boundary without calling into DirectFB
from the capture worker.
