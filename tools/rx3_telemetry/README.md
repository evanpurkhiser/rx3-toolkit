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

The mappings for raw play state `1/2/3` to playing/cued/paused are established
by the accessor's control flow. Media slot, track type, pitch units, and the use
of track number as track ID remain provisional until the first hardware capture.

## Feeding `MixstatusProcessor`

The bridge can consume the JSON stream without bringing a Pro DJ Link network
online:

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

Metadata events are keyed by deck and generation. A production adapter keeps
the latest four fields for each deck and attaches them when `nowPlaying` fires,
instead of asking Pro DJ Link's database service for the track.
