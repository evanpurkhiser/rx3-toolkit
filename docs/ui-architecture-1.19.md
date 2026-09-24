<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 firmware 1.19 UI architecture

This document records the display stack in the firmware 1.19 `rbp` executable
and evaluates practical paths for adding live text, controls, and image frames.
Addresses apply only to the verified `rbp` SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`.

## Summary

The RX3 uses a generated, proprietary HMI framework statically linked into
`rbp`. It is not an HTML application and does not use Qt, GTK, SDL, Cairo, or
the JUCE GUI classes. JUCE is statically present for core utilities, threads,
timers, audio, MIDI, files, sockets, and messages.

The proprietary UI ultimately renders through DirectFB 1.4 and the Vivante
graphics driver:

```text
generated Obj_WS_* / Obj_CTRL_* property trees
                  │
                  ▼
        NS_* HMI and glyph framework
                  │
                  ▼
          NS_PALRender_* renderer
                  │
                  ▼
       DS_GR_* / DS_Task_* abstraction
                  │
                  ▼
         DS_HW_* DirectFB backend
                  │
                  ▼
 DirectFB 1.4 → Vivante GAL/g2d → framebuffer
```

Adding custom UI is feasible. The toolkit's performance core already draws
custom tabs, text, controls, and RGB565 images through the native renderer and
routes touch through the stock input path. A live count or compact chat panel
is a small extension of proven behavior. A complete custom page needs more
layout and navigation work, but does not require replacing the UI framework.

## Binary and library evidence

`rbp` is a fixed-address ARM EABI5 executable with its static symbol table and
RTTI intact. Its dynamic graphics dependencies are:

| Library | Role |
| --- | --- |
| `libdirectfb-1.4.so.0` | Surfaces, windows, layers, fonts, and presentation |
| `libfusion-1.4.so.0` | DirectFB shared state and IPC |
| `libdirect-1.4.so.0` | DirectFB support layer |
| `libg2d.so.0.8` | Vivante 2D blitting |
| `libjpeg.so.8` | JPEG encoding and decoding |
| `libGAL-fb.so` and DirectFB GAL plugin | Vivante framebuffer acceleration |

The executable contains approximately 807 `NS_*` functions, 166 `DS_*`
drawing/window/layer functions, 2,000 JUCE symbols, 1,715 `uif::` symbols, and
5,504 `ui::` symbols. The JUCE set has no `Component`, `Graphics`,
`LookAndFeel`, or JUCE window hierarchy. The product UI belongs to the `NS_*`,
`ui::`, and `uif::` layers.

The statically linked JUCE reports version 1.51.16. Its GUI module was not
linked; adding modern JUCE widgets would require bringing an entire graphics
backend rather than instantiating dormant components already in `rbp`.

An embedded source path names the DirectFB adapter:

```text
Source/Allinone/Nexus/GUI/suntec/glib/plugins/driver/DS_HW_Glib3_DFB.c
```

This `glib` is the product's graphics library rather than GNOME GLib.

Production graphics initialization occurs in `init_Resource` at `0x001a34a0`.
It configures DirectFB without cursor, VT switching, or keyboard input and then
creates the product-owned layers and windows. `ui_com_init` at `0x001921ec`
initializes the image and font tables, creates the 1280×800 layer, and enters
the HMI initialization path.

## Generated UI model

The normal interface is generated into static object/property tables. The
binary exports 2,483 `Obj_*` symbols: 2,422 controls and 61 named window or
winscape nodes. The top-level factory registers `WS_START` and `WS_BROWSER`.
Most of the product interface is a collection of subtrees under `WS_BROWSER`,
including deck, browse list, source, utility, keyboard, pads, PC control, and
video-control windows.

The framework provides conventional HMI operations despite being proprietary:

| Capability | Representative API |
| --- | --- |
| Find the active screen | `NS_ComponentManager_GetActiveWinscape` |
| Switch screens | `NS_ComponentManager_SetActiveWinscapeByID` |
| Find a generated object | `ui_com_draw_GetObjectByID` |
| Replace text | `ui_com_draw_SetTextStringByBuffer` |
| Refresh an object or screen | `ui_com_draw_RefreshObject`, `ui_com_draw_RefreshWinscape` |
| Create text/image/rectangle glyphs | `NS_GlyphText_*`, `NS_GlyphImage_*`, `NS_GlyphRect_*` |
| Route touch | `NS_HMIManager_OnTouchPanel`, `NS_EventManager_OnTouchPanel` |
| Render and present | `NS_HMIManager_Flush`, `DS_HW_UpdateScreen` |

The `gui_task` loop at `0x000d3aa0` calls `ui_com_draw` at `0x0018e75c` for
normal display work. This is a useful synchronization boundary for features
that need one bounded update per native UI cycle.

The 40 MiB GUI volume supplies a 40.8 MiB packed `imagedata.dat`, proprietary
bitmap fonts, two TrueType fonts, and small language-specific binary tables.
Layout and control properties are compiled into `rbp`; there is no editable
XML, QML, HTML, or similar runtime layout document.

## Existing custom-rendering path

The performance core is an implementation proof for custom UI. It hooks these
guarded firmware functions:

| Function | Address | Current use |
| --- | ---: | --- |
| `NS_PALRender_DrawText` | `0x001d23e4` | Clone native text glyphs and draw custom labels |
| `NS_PALRender_DrawImage` | `0x001d3284` | Replace selected subtrees and draw custom images |
| `TouchPanelHandler::solveCoordToKey` | `0x002dc104` | Route custom touch targets |
| `NS_HMIManager_RefreshGlyph` | `0x001d07b0` | Request redraw through the native UI path |

The text hook copies a fully attached 84-byte `NS_GlyphText` object from the
screen being drawn. The clone retains its native window, parent, font, palette,
and clipping state. The hook changes its bounds, UTF-16 string pointer and
length, alignment, and other public fields before calling the original
renderer.

Custom bitmaps use private IDs above the firmware's stock image count. The core
allocates a larger image table, relocates the stock records, appends RGB565
records, and publishes the finished table with one pointer swap. Existing
resources and cached DirectFB surfaces remain untouched.

This mechanism currently draws the KEY and STEMS tabs and their controls. A
new visual feature must join this central render broker or generalize it.
Installing a second inline hook on the same renderer entry points would create
an ordering and trampoline conflict.

## Feature feasibility

| Feature | Assessment | Practical implementation |
| --- | --- | --- |
| Viewer count or status badge | High confidence | One cloned native text glyph, updated only when the value changes |
| Twitch chat text | High confidence | A bounded set of cloned text rows with native clipping and font handling |
| Chat emotes | High confidence | Rasterize rows or emotes on the companion and send RGB565 tiles |
| Full custom page | Feasible | Take over one existing `WS_BROWSER` subtree while a custom tab is selected |
| Low-resolution preview | Feasible with validation | Companion-decoded RGB565 or JPEG frames, double buffered and drawn on the UI thread |
| Native H.264/video playback | Poor fit | The firmware lacks a general video decoder and the kernel VPU driver is disabled |

The compiled `CTRL_VIDEO` subtree is a remote control for Rekordbox Video. It
contains camera, slideshow, image/text, AV-sync, favorite, and transition-FX
controls. It does not decode or display a local video stream. It may be a useful
host subtree after confirming its behavior on a live unit.

The firmware contains no FFmpeg, GStreamer, `libavcodec`, TLS, WebSocket, or
browser engine. Twitch authentication, HTTPS, WebSocket handling, JSON parsing,
emote layout, and video decoding should run on a companion computer or Wi-Fi
device. The RX3 should receive a small purpose-built local protocol.

## Recommended runtime design

```text
Twitch / video source
        │
        ▼
companion service
  HTTPS, OAuth, WebSocket, JSON, video decode, text/emote layout
        │
        │ framed TCP or UDP over the local RX3 link
        ▼
RX3 receiver worker
  validate lengths, fill inactive state/frame buffer
        │
        │ atomic generation or pointer swap
        ▼
existing UI render hook
  read complete snapshot, clone native glyphs, draw, return
```

All `NS_*` and `DS_*` rendering calls must execute on the UI/render path. The
network worker may receive and validate messages, decode the small protocol,
and fill inactive buffers. It then publishes a complete snapshot atomically.
The draw hook performs bounded reads and native drawing only.

The draw hook must not perform socket reads, file I/O, allocation, JSON parsing,
JPEG decoding, environment lookups, or blocking synchronization. Earlier
experiments showed that calling invalidation from the wrong thread can stall UI
startup, and even repeated environment lookups in the hot draw path can prevent
DirectFB from starting.

A compact protocol needs only a few message types:

- replace viewer-count/status text;
- replace a numbered chat row with UTF-16 text and style flags;
- clear or scroll chat rows;
- publish a complete RGB565 frame into the inactive video buffer;
- show, hide, or select the custom panel.

Length-prefixed binary messages are preferable to JSON inside `rbp`. They are
easy to validate, require no parser, and can be copied into fixed-size buffers.

## Video-preview budget

A 320×180 RGB565 frame is 115,200 bytes. Two frame buffers require 230,400
bytes. Raw transport requires approximately:

| Rate | Payload bandwidth |
| ---: | ---: |
| 5 fps | 0.58 MB/s |
| 10 fps | 1.15 MB/s |
| 15 fps | 1.73 MB/s |
| 30 fps | 3.46 MB/s |

Those numbers fit comfortably within the nominal 100 Mb/s rear USB network and
the unit's 1 GiB RAM. Audio deadline and UI-render headroom still require live
measurement. Begin at 320×180 and 5–10 fps with frames preconverted by the
companion.

Two native paths merit a live prototype:

1. create or refresh a private image surface with `DS_HW_CreateImage`,
   `DS_HW_RefreshImage`, and `DS_HW_DrawImage`;
2. use the `DS_GR_DrawPrivate`/`DS_HW_DrawPrivate` callback path to lock the
   destination, copy the current RGB565 buffer, and mark its damaged area.

`DS_HW_DrawPicture` is a no-op in this DirectFB backend and is not a candidate.
A changing color-bar and frame-counter test should establish cache behavior,
tearing, sustainable cadence, UI latency, and audio safety before accepting
real video frames.

The binary also contains `DecodeScalingJpeg` at `0x00176208` and `resize` at
`0x00176160`; both use the Vivante `g2d_blit` path. Sending small JPEG frames
could reduce transport bandwidth, but it adds decoder load and an unproven
thread-safety requirement. Raw preconverted RGB565 is the better first test.

A separate DirectFB process may be able to join the DirectFB Fusion world and
create a translucent window. Its z-order, ownership, and interaction with the
product's update cycle have not been tested. The in-process render broker is
more predictable because it already inherits the correct layer, window,
palette, clipping, and presentation behavior.

## Implementation sequence

1. Add a static viewer count and four sample chat rows through the existing
   render broker.
2. Feed bounded text state from a companion over the direct USB network.
3. Add a custom tab with show/hide and chat scrolling through the existing
   touch router.
4. Take over an existing browser subtree for a full-page layout.
5. Draw a changing 320×180 RGB565 test pattern at 1 fps, then 5 and 10 fps.
6. Measure `rbp` CPU use, UI latency, event drops, and audio stability before
   enabling a decoded preview stream.

The first three steps reuse mechanisms that already work in the toolkit. The
dynamic image refresh is the only essential rendering primitive that still
needs a live proof.
