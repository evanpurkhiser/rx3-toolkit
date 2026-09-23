# USB crossfader curve prototype

This module replaces one firmware 1.19 crossfader lookup table in `rbp` memory.
It never writes the `rbp` file. A restart or power cycle restores the stock
table, and the other two selectable crossfader curves remain unchanged.

Place a file named `rx3-crossfader.json` at the root of the same USB drive as
the toolkit payload. The object may be pretty-printed and its fields may appear
in any order, but it must contain exactly these three fields:

```json
{
  "version": 1,
  "target": "mid",
  "points": [[0, 0], [0.5, 0.5], [1, 1]]
}
```

`target` selects the firmware table replaced behind one hardware selector
position: `mid` is the stock smooth position, `sharp` is the near-instant cut
position, and `mid-half` is the remaining transition position. `points`
contains 2 through 32 `[position, gain]` pairs. Position 0 is the muted far side
and position 1 is the full-gain near side. Both coordinates are linear values
from 0 through 1. The list must begin with `[0, 0]`, end with `[1, 1]`, use
strictly increasing positions, and never decrease in gain. The module linearly
interpolates gain between points into the firmware's 1,024-entry table. Decimal
coordinates may use at most nine digits after the decimal point so the USB
validator and preload consume exactly the same values.

Each deck remains capped at unity gain. A curve that keeps both sides loud near
the center can increase their summed level, just as the stock sharp curve does.

## Stock firmware responses

The first chart follows the raw firmware table direction, from audible to
muted; the JSON points use the opposite, user-facing direction from muted to
full gain.

![The MID, MID HALF, and SHARP single-deck lookup tables](stock-crossfader-curves.svg)

![Mirrored Deck A and Deck B gain and combined power for each stock table](stock-crossfader-overlap.svg)

The preload checks the complete stock table before changing memory. This
prototype supports only the verified firmware 1.19 `rbp` whose SHA-1 is
`cf309238491e73cdbdc1f08a09f7a3177e079068`. Missing configuration leaves a
stock session untouched. Invalid configuration aborts preparation before an
`rbp` restart or table change.

Select `crossfader-curve` when building the runtime. Configuration changes take
effect after the module restarts `rbp`; removing the configuration disables a
previously active preload on the next insertion.
