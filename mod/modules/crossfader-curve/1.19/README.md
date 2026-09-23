# USB crossfader curve prototype

This module replaces one firmware 1.19 crossfader lookup table in `rbp` memory.
It never writes the `rbp` file. A restart or power cycle restores the stock
table, and the other two selectable crossfader curves remain unchanged.

Place a file named `rx3-crossfader.json` at the root of the same USB drive as
the toolkit payload. The object may be pretty-printed and its fields may appear
in any order, but it must contain exactly these three fields:

```json
{"version":1,"target":"mid","midpoint_db":-6.0}
```

`target` selects the firmware table replaced behind one hardware selector
position: `mid` is the stock smooth/equal-power position, `sharp` is the
near-instant cut position, and `mid-half` is the remaining transition position.
`midpoint_db` is each deck's gain at the physical midpoint and must be between
`-48` and `-3.0103` dB. The upper limit prevents a symmetric center power
boost. `-6.0206` produces a linear-amplitude response. The generated response
is a symmetric power curve with exact zero and full-scale endpoints.

The preload checks the complete stock table before changing memory. This
prototype supports only the verified firmware 1.19 `rbp` whose SHA-1 is
`cf309238491e73cdbdc1f08a09f7a3177e079068`. Missing configuration leaves a
stock session untouched. Invalid configuration aborts preparation before an
`rbp` restart or table change.

Select `crossfader-curve` when building the runtime. Configuration changes take
effect after the module restarts `rbp`; removing the configuration disables a
previously active preload on the next insertion.
