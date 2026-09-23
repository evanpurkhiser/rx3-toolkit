# RX3 telemetry host reader

Run the complete protocol path without hardware:

```sh
python3 -m tools.rx3_telemetry.cli simulate
```

On a Mac with the RX3 connected through rear USB-B:

```sh
python3 -m pip install hidapi
python3 -m tools.rx3_telemetry.cli listen
```

The reader emits newline-delimited JSON. State messages contain a provisional
`prolinkState` object shaped like `prolink-connect`'s `CDJStatus.State`, so an
adapter can pass that member directly to `MixstatusProcessor.handleState()`.

The firmware's standalone play-mode enum is exposed as `rawPlayState` and a
`mode-N` playback label. Correlate those values with playing, paused, and cued
states during the next hardware capture before using the provisional
`prolinkState.playState` value. Media slot, track type, pitch units, and the use
of the track-load identifier as track ID also remain provisional.

## Feeding `MixstatusProcessor`

Once the raw mode mapping is confirmed, the bridge can consume the JSON stream
without bringing a Pro DJ Link network online:

```ts
import {spawn} from 'node:child_process';
import readline from 'node:readline';
import {MixstatusProcessor} from 'prolink-connect';

const source = spawn('python3', [
  '-m',
  'tools.rx3_telemetry.cli',
  'listen',
]);
const processor = new MixstatusProcessor({useOnAirStatus: true});

readline.createInterface({input: source.stdout}).on('line', line => {
  const event = JSON.parse(line);
  if (event.type === 'state') {
    processor.handleState(event.prolinkState);
  }
});

processor.on('nowPlaying', state => {
  console.log('now playing on RX3 deck', state.deviceId);
});
```

Metadata events are keyed by deck and generation. The current firmware module
populates the title and sends empty artist, album, and key fields. A production
adapter keeps the latest fields for each deck and attaches them when
`nowPlaying` fires instead of asking Pro DJ Link's database service for the
track.
