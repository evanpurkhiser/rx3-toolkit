# RX3 1.19 display capture and streaming

Firmware 1.19 exposes enough of the display pipeline to stream the player screen
over the rear USB-B network connection. The lowest-risk first experiment uses
the DirectFB diagnostic program already shipped in the system image. A sustained
stream can then read the active framebuffer from a worker and use the player's
presentation function only as a notification source.

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
`dfbdump` may therefore return an 800x480 pre-scaling primary surface while
`/dev/fb0` always returns the 1280x800 physical scanout. The first live dump will
show where scaling occurs. The smaller DirectFB surface would be preferable for
streaming if it contains the complete UI.

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

## Built-in screenshot path

The production system image contains unstripped ARM DirectFB tools:

```text
/usr/bin/arm-none-linux-gnueabi-dfbdump
/usr/bin/arm-none-linux-gnueabi-dfbinfo
/usr/bin/arm-none-linux-gnueabi-dfbinspector
/usr/bin/arm-none-linux-gnueabi-dfbscreen
```

`dfbdump` joins the active DirectFB Fusion world and can dump primary layer
surfaces or every front buffer. Its supported options include:

```text
--dumplayer    dump surfaces of layer contexts
--dumpsurface  dump the front buffer of every surface
```

DirectFB's dump implementation writes PPM color data and PGM alpha data. This
provides a one-shot proof before installing another capture hook.

The 512 KiB `/tmp` mount cannot hold a screenshot. On the next live session,
write the dump to the toolkit drive from the root shell:

```sh
mkdir -p /media/usb1/sda1/RX3_SCREEN
cd /media/usb1/sda1/RX3_SCREEN
/usr/bin/arm-none-linux-gnueabi-dfbdump --dumplayer
ls -lh
```

The expected color file is named like
`dfb_layer_context_0xXXXXXXXX_0000.ppm`. If more than one layer is dumped, compare
their dimensions and contents. `--dumpsurface` is a fallback, but it may dump
many intermediate UI surfaces and should only be run once during diagnosis.

If the BusyBox `httpd` applet is enabled, the existing USB root-shell connection
can expose the directory for retrieval over `169.254.100.2`:

```sh
/bin/busybox httpd -p 8080 -h /media/usb1/sda1/RX3_SCREEN
```

Stop the server after copying the file. If `dfbdump` cannot join the application's
Fusion world, a direct `/dev/fb0` read is the next test.

The binary also contains a JPEG encoder: `write_jpeg` at `0x00177f6c` calls
`encodeFile` at `0x00238b6c`, which consumes RGB24 rows and uses quality 75.
Using it would require RGB565-to-RGB24 conversion on the RX3. Tile deltas with
host-side video encoding avoid that conversion and keep compression away from
the player process.

## Streaming design

The first streaming module should avoid DirectFB calls and renderer hooks. A
normal-priority worker can map `/dev/fb0`, read the currently visible page using
`FBIOGET_FSCREENINFO`, `FBIOGET_VSCREENINFO`, `line_length`, and `yoffset`, and
send changed tiles over TCP. It captures only while one client is connected and
defaults to 2 frames per second, with a hard limit of 10.

An event-driven revision can use two small components:

1. A guarded hook around `DS_HW_UpdateScreen` calls the original function and
   increments an atomic presentation generation. The wrapper performs no network
   I/O, allocation, compression, or framebuffer copying.
2. The worker coalesces generations and captures at its configured rate. This
   preserves event-driven idling while keeping all expensive work away from the
   UI thread.

The worker should cap capture at 10 frames per second initially. Multiple display
updates between captures collapse into one frame. A slow or disconnected client
must never backpressure the renderer; the worker drops frames and resumes from a
keyframe.

Divide the screen into 32x32 RGB565 tiles. There are 40x25, or 1,000, tiles.
Hashing one complete frame reads 2,048,000 bytes. The stream sends:

```text
HELLO       protocol version, width, height, stride, pixel format
KEYFRAME    frame sequence and all tiles
DELTA       frame sequence and changed tile records
HEARTBEAT   current sequence and counters
```

Each tile record contains its index, payload length, and RGB565 bytes. A keyframe
is sent on connection, periodically, and after a detected sequence gap. When
more than roughly 60 percent of tiles change, a full-frame record is cheaper.
Fixed bounds and network byte order keep the receiver simple. Raw tiles are
adequate for the first test; XOR plus run-length encoding is a useful next step
for dark UI regions and scrolling waveforms.

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

## Live validation order

1. Use `dfbdump --dumplayer` and confirm that its PPM matches the LCD.
2. Query framebuffer fixed and variable info, including color bitfields, stride,
   virtual height, and current `yoffset`.
3. Copy one visible `/dev/fb0` page and compare it with the DirectFB dump.
4. Read full frames at 1 fps while monitoring UI responsiveness and audio.
5. Add tile hashing and TCP transport at 5 fps, then 10 fps.
6. Add the presentation hook so captures occur only after display updates.
7. Measure bandwidth and dropped frames on browser, waveform, and performance
   screens before choosing compression.

The remaining uncertainty is whether a concurrent `/dev/fb0` read always sees a
tear-free completed presentation. The bundled `dfbdump` path reads the DirectFB
surface itself, and a capture immediately after the flip provides a natural
frame boundary. A live screenshot resolves the final question without
persistent changes to the player.
