# Master parameters from punch-in effects (design note)

Status: research, not an interface. Base: `origin/staging` at 3190282. The punch-in
API this extends is draft PR #215 (`origin/sdk/punch-in-effects`). Nothing here
has been run on a device.

Sources are labelled:

- **[repo]**: docs, profile or SDK source in this checkout.
- **[disasm]**: static reading of the stock OS 1.2.1 image (hash checked against
  `profiles/opxy-1.2.1.json`). The confidence is given each time. No firmware
  bytes are copied here; only addresses and behaviour are.

## 1. The master section in OS 1.2.1

The MIX controller keeps its page index at controller word `+0x88` [repo:
abi-1.2.1.md "Master-processor capability"]. The encoder entry `8c093888` calls
the page handler `8c09358c`, which switches on that word [disasm, high]. The
pages:

| Page | Artwork (string ref) | Project words | GUI→audio message, sender | Audio engine words (`84c696fc` +) |
| --- | --- | --- | --- | --- |
| 0 | `mixer.svg`, faders | per track (mixer record) | 0xa1 / 0xc7 per track | n/a |
| 1 EQ | `eq.svg` (`8d537050`), low/mid/high flaps | `+0x68..+0x74` | **0xb3**, `8d4dc84c(id, value)` | `+0x54..+0x60` |
| 2 "dist" | `master_dist.svg` (`8d544d68`), `textleftknob`, `textrightknob` | `+0x78..+0x84` | **0xc6**, `8d4dc890(id, value)` | `+0x64..+0x70` |
| 3 master | `master.svg` (`8d545010`), `master_level`, `glue_level`, `glue_current` | `+0x88..+0x94` | **0xa2**, `8d4dc808(id, value)` | `+0x44..+0x50` |

"Project words" are offsets from the active project returned by `8d584210`
[repo: the getter used by `sdk_master_project`].

**Evidence.**

- *Edit path* [disasm, high]. On pages 1–3 the page handler reads the project
  word `+{0x68,0x78,0x88} + 4·encoder`, steps it with `8d4d6cc8` [repo], marks
  the project dirty with `8d585ee8` [repo] and calls the page's sender with
  `(encoder, value)`. On page 3 the step is one percent (`n·21474836 + 10737418`).
  On page 1, encoder 4 moves all three band words together.
- *Publication* [disasm, high]. After the getter call at `8d598a3c` [repo: the
  `sdk_master_project` site], stock sends the four A2 words, then the four B3
  words, then the four C6 words.
- *Audio dispatcher* [disasm, high]. `8d5b1260` reads the message type byte at
  +2 and looks it up in a 50-entry table at `840ab380` (types 0x98–0xc9).
  - 0xb3 → `8d5b144e` → `8d50eb8c` [repo: the "unbounded parameter setter"]. It
    stores `[engine + 0x54 + 4·id]`, and for ids 0–2 it recomputes the EQ through
    `8d4ff6f4` and `8d4ff640`, the same routine the block calls just before EQ
    (`11a02c10`) at `8d511b6c`.
  - 0xc6 → `8d5b14ee` → `8d50e5e4`. It stores the raw value at
    `[engine + 0x64 + 4·id]` with no bound on id.
  - 0xa2 → `8d5b1332` → `8d50ea92`. It takes ids 0–3 through the table at
    `8404cbfc`. Ids 0 and 1 go to `+0x44`/`+0x48` with a two-segment curve. Id 2
    goes to `+0x4c` as `0.6v + 0.4v²`. Id 3 goes to `+0x50` raw.
- *Who reads them* [disasm, medium-high]. After EQ, the block calls `8d502828`
  (state object `84c69a30`) with a pointer to `engine + 0x64`, the four C6 words.
  It smooths each word with a per-parameter ramp. Then `8d50efd8` reads A2 id 2
  through getter `8d50e5cc(engine, id)` (`[engine + 0x44 + 4·id]`) at `8d50effe`
  and runs the compressor (profiler labels `FILTER`, `COMPRESSOR`, `LIMITER` at
  `8409f394`). Last, `8d511ed0` reads A2 id 3 as master level (cubic gain).

**Mapping the requested effects.**

- **Effect A** ("filter mix 100%, both distortion params off, tone sweeps") uses
  the four page-2 C6 words. They are the only master stage with four parameters
  that are neither EQ bands nor level or compressor. Which word is mix, tone and
  each distortion parameter is **not confirmed**. The labels live in the stock
  artwork on the device, not in the OS image. *Medium* for "C6 = these four",
  *unknown* for their order. In 1.2.1 the "master filter" is this page. Page 1 is
  EQ unless an SDK master processor replaces it [repo: master-filter.md].
- **Effect B** ("compressor 100%") uses A2 id 2: project `+0x90`, audio `+0x4c`.
  The compressor stage reads it, and the page shows it as `glue_level`.
  *Medium-high.* A2 ids 0 and 1 are unlabelled. A2 id 3 is master level (*high*).

## 2. Storage and transport

- The project words above are saved with the project. Stock publishes them on
  activation [disasm, high]. They are GUI-side state.
- The GUI and audio sides run on separate Blackfin chips joined by an SPI bridge
  (firmware strings `spi_bridge_bfm/bfs`, `cpu_is_master`). Each sender builds a
  message (type, id byte, value word) and queues it. The audio side applies it
  in the dispatcher above. Nothing on the audio side is written back to the
  project.
- The SDK already intercepts B3 at `8d5b145e` [repo]. Ids 128–137 carry
  processor transactions. Ids below 128 still reach `8d50eb8c`, so EQ ids 0–3
  are unaffected. With a processor selected, stock EQ does not run, so EQ
  overrides have no effect.
- An SDK master processor sees only its own eight slots through `prepare`
  [repo]. It never sees the stock dist or compressor words.

## 3. Options for temporary overrides

**(b′) Transient sends (recommended).** While an effect holds a master
parameter, send the override value to the audio side through the stock sender,
and *never* write the project word or call the dirty marker. On release, send
the project's current value.

- Save, undo, project dirty state and the knob display all keep the project
  value. Only the sound changes, which is how stock's per-track overrides behave.
- A knob edit or project publication while held sends the project value and
  clobbers the override. The SDK re-asserts the override on the next tick:
  compare the project word and its last-seen value each tick, or simply resend
  every tick while held.
- Uses no new patch sites. The two other senders (`8d4dc808`, `8d4dc890`) are
  [disasm] and need adding to the ABI doc with this evidence. The B3 sender is
  already used by the SDK [repo].
- **Unverified.** First, that the senders may be called from the sequencer
  thread. The stock sequencer already sends parameter messages, and the MIDI CC
  path calls the B3 sender from `8d58e9d4`, but the queue's locking has not been
  read. Second, the bridge traffic a per-tick resend costs. Third, whether
  audio-side smoothing hides steps in a 96-PPQN sweep (`8d502828` ramps each
  word, which is promising).

**(a) Override in the audio read path.** Hook the dist-stage call at `8d511cb4`
and the getter calls at `8d50effe` and `8d511ed0`, and substitute override
values held on the audio side. A new virtual-ID transaction would deliver them,
like B3 128–137. This is the cleanest semantically: stock state is never sent
a fake value. But it needs new profile patch sites, audio-side adapters and
release verification, with the same unknowns about parameter identity. It is
worth doing only if (b′) shows thread-safety or traffic problems.

**(b) Write and restore through the stock edit path.** Write project words,
mark them dirty and send. A save while held would store the override. Undo and
the UI would show it, and a crash would lose the original. **Rejected.**

**(c) Modulation input for SDK master processors only.** Pass punch modulation
to `prepare`. This leaves the stock dist and compressor alone, so it cannot do
effect A or B on stock. It remains a separate, compatible addition: the
processor could also take a fixed "punch" slot.

## 4. Proposed API (an SDK PR on top of #215)

This widens the existing `tick` callback and its event. It adds no new hook
beside it.

```cpp
namespace opxy { namespace punch {
enum class Master : u8 {
    eq_low, eq_mid, eq_high, eq_extra,  // page 1, B3 ids 0..3
    stage_a, stage_b, stage_c, stage_d, // page 2, C6 ids 0..3 (names after device check)
    master_a, master_b, compressor, level, // page 3, A2 ids 0..3
};
// Lasts while set on every tick, like Overrides; the SDK sends the project's
// value on the first tick nobody sets it. Value: normalized Q31, 0..0x7fffffff.
struct MasterOverrides { void set(Master parameter, i32 value) const; };
}}
// Effect::tick gains a third argument, or Effect gains
// `void (*master)(const Context&, const MasterOverrides&)`, called once per tick
// for the first targeted track only, so several tracks don't fight.
```

How the SDK would implement it [proposal]:

- During a tick, collect the requested value per parameter. If several effects
  set the same parameter, the last one wins.
- At the next mask hook (`sdk_punch_mask`, once per tick):
  - send each set value that changed, or whose project word changed since it
    was last sent;
  - for each value that was set and now is not, send the project word.
- Never write project memory.
- Parameter names stay generic until a device check confirms the labels. The
  enum values are the stable contract.

The capability belongs in PR #215's extension direction (punch-in effects as an
opt-in extension), not in a machine.

## 5. Needs disassembly or a device

1. The order and labels of the four C6 words and of A2 ids 0 and 1 (device: turn
   each knob and watch which word changes, or read the stock artwork's text
   nodes on the device).
2. Whether the senders are safe to call from the sequencer: read the queue push
   that the senders call.
3. The audible result and bridge load of per-tick resends (hardware).
4. What B3 id 3 is: CC 90 sets it, and no MIX encoder edits it directly.

## 6. Follow-up: thread safety and knob order [disasm]

- *Senders* (high). `8d4dc808`, `8d4dc84c` and `8d4dc890` differ only in the
  type byte (0xa2, 0xb3, 0xc6). Each builds a header on its stack (id byte,
  value word, routing 5/0/0xff/0xff) and calls the bridge push `11a00ec0` with
  the GUI→audio queue `841460b4`. The push rejects payloads over 249 bytes,
  copies the message and calls `11a00cbc`, which bumps a per-channel sequence
  byte without a lock and sends through the transport's method `+0x28`. The
  receive side (`11a00d64`) guards its busy flag with CLI/STI; the send side
  has no guard, so it relies on single-context callers.
- *Context* (medium-high). The GUI main loop `8c0babcc` polls the bridge and
  other subsystems, then calls the controller update `8c0baacc`, whose
  sequencer update `8d5986b8` runs the tick loop `8d598040` (the mask builder,
  once per elapsed tick) and then `8d59518c`: the per-update track pass
  `8d58ce60` (tracks 0..7, unconditionally) and `8d58c474`, which itself sends
  0xc3 messages through `8d4dc534` and the same push. Encoder edits reach
  `8c09358c` through the MIX controller's method table (`803fd0c`); the GUI
  tick that dispatches them runs in the same main loop [repo: abi-1.2.1.md],
  though the dispatch path itself was not traced (medium). Calling the
  senders from the punch hooks is therefore what stock does already, and a
  knob edit cannot interrupt one.
- *Knob order* (high for ids, device for labels). On page 2, `8c09358c` masks
  the encoder index to 0..3, steps project word `+0x78 + 4·id` with
  `8d4d6cc8` and calls `8d4dc890(id, value)`: no permutation. With the device
  labels left to right that is gain, clip, tone, mix = ids 0..3. No dirty
  marker call appears on this path.
- *Cadence*. The mask builder can run several times per update (once per
  elapsed tick) and the per-update pass once, so the SDK applies master values
  after each complete run of passes over tracks 0..7, not at the mask hook.
