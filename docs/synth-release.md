# Optional synth release callback

Declare `"sdk": "1.8"` in `machine.json` and add `"releaseCallback": 1` inside a synth definition's `resources` to receive
the stock voice-release event:

```cpp
void release(Voice& voice, Shared& shared, opxy::u32 owner);
```

This callback has the same stable owner ID and state as `start` and `render`.
It runs on the audio thread and must be bounded and allocation-free. Omitted
or integer zero does not require the callback and preserves existing behavior.
Booleans and values outside 0/1 are rejected. Sample engines and effects do not
accept this resource.

On OP-XY 1.2.1, stock release dispatch at `0x8d51a928` calls engine vtable slot 7.
The adapter forwards that event only to the handle's own voice. If a note is
waiting for a stolen voice to finish fading, release is delivered immediately
after that note starts. It never releases the previous owner.
Stock envelopes, filters and mixing remain active. This is an event callback,
not a replacement envelope or an instruction to destroy the voice.

Same-owner retrigger still calls `start`. Voice stealing calls `destroy` for
the previous owner, then zeroes and initializes the new owner's storage and
calls `start`. Neither stealing nor destruction invents a release event.
The adapter forwards every actual stock release invocation; stock decides
whether a note transition releases or retriggers a voice.

Host/WASM adapters export `opxy_release`. Browser note-off invokes it once per
active gate closure before the existing audition release envelope. Already
rendered samples in a partial block finish normally. Duplicate or stale
note-offs cannot release another note. Retrigger resets the audition gate;
tail completion and panic continue to destroy voices. Older ABI 1 WASM modules
without the release export remain supported.

This extension adds no stock hook and no fields to the machine's voice state.
Host checks do not establish hardware timing or stock mixer/envelope behavior.
