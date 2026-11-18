# Sample Studio

The `sample_studio` capability lets an extension put an editor inside stock's
sample library. The extension supplies an editor, which takes the library's
keys, encoders and screen on the main board, and a player, which runs beside the
master bus on the audio board. The SDK owns every firmware site and gives both
sides services through [`opxy/sample_studio.hpp`](../sdk/include/opxy/sample_studio.hpp).
The extension holds no firmware address.

[Sample Studio](https://github.com/mmockett/OP-XY-SampleStudio) is the
extension built on it: trim, slice, retime and process, played live and saved
beside the original.

## The extension

`extension.json` declares the capability. It takes no fields yet:

```json
{
  "id": "sample_studio",
  "name": "Sample Studio",
  "description": "...",
  "claims": [],
  "actions": [],
  "sample_studio": {}
}
```

`extension.cpp` exports the two tables. The folder also holds the artwork the
editor draws, `sample_studio.svg`, which the build installs as
`content/display/sample_studio.svg`.

```cpp
#include <opxy/extension.hpp>
#include <opxy/sample_studio.hpp>

namespace opxy { namespace extensions { namespace sample_studio {
extern constexpr Hooks hooks = Hooks();
extern const studio::Editor editor = {press, release, turn, frame};
extern const studio::Player player = {receive, mixer_begin, voice, track_gain, track_level, master};
}}}
```

One extension per build declares it, and not beside a master processor: both
use the master EQ call and the B3 receiver. The build also needs a sample-family
machine or a `drumline_page` extension, for the samples service.

## The editor

| Callback | When |
| --- | --- |
| `press(key, shift)` | A key in the library: M1 to M4, the encoder clicks, Sample. |
| `release(key)` | Its release. |
| `turn(encoder, detents)` | An encoder turn, encoders 0 to 3. |
| `frame(elapsed)` | Every library frame. |

Each returns whether it took the input. What it doesn't take goes to stock's
library, unchanged. `frame` returns true when it drew the frame. It can call
`stock_frame()` first to draw over stock's library, or return false to leave
the frame to stock.

The editor draws on a retained surface over the library. `surface_ready(ids, ...)`
binds the artwork's node, text and palette IDs, and `surface_present(frame)`
shows a `studio::Surface` frame: up to 32 mutable paths of 64 points, 96 nodes,
32 texts and 8 palette entries. Only the bound slots are drawn, and the rest may
stay empty. A frame whose bound slots are out of range is not drawn at all.

Its services:

- the library: whether it is on top or busy, its highlighted file, the
  audition, listing the folder again, and stock's preset browser;
- the track: the selected track and key, its sample engine, note starts and
  held notes, Shift, the project tempo;
- a key's sample, as stock's region entry: path, length, region, level,
  direction and tuning; putting a file on a key as stock's library does; sending
  the table to the voices;
- stock's sample cache: a cached sample borrowed by its key, and its PCM;
- the card, on the worker: open, read, write, rename, remove, make a folder;
- stock's background worker (`post`), its heap and its popup;
- `send(channel, value)`: a message to the player, channels 0 to 14.

## The player

| Callback | When |
| --- | --- |
| `receive(channel, value)` | A message the editor sent. |
| `mixer_begin()` | Each block, before the send effects run. |
| `voice(stereo, track)` | Each voice's finished block, before its track's sum. |
| `track_gain(track, gain)` | The gain stock gives a track this block. |
| `track_level(track, level)` | A track's dry level this block. |
| `master(left, right)` | The master bus after its EQ, before the compressor. |

None of these may allocate, block or touch a file: post that work. The player's
services are the sample pool, with its capacity and the headroom stock keeps for
its own loads; pinning a cached sample while it plays; and the two send effects'
input buses for this block.

Both sides have the clock, the cycle counter, a store barrier, the device log
and `post`.

## Firmware it uses

All OS 1.2.1, in [`profiles/opxy-1.2.1.json`](../profiles/opxy-1.2.1.json):

- service `sample_studio`: five entry branches in the library controller
  (press, Shift press, release, turn) and the browser screen's frame
  (`sample_studio_hooks.s`, `sample_studio_host.cpp`);
- service `master_bus`: the master EQ call and the audio board's B3 receiver.
  IDs 138 to 152 go to the player and never to stock; every other ID goes to
  stock's setter (`sample_studio_audio.cpp`);
- service `drum_sends`: the first mixer call of each block and the voice hook,
  through weak calls, so a build without the extension is unchanged.

Without a `sample_studio` extension, none of it is linked.
