# Linear Arranger — User Guide

**MVP-027 · OP-XY OS 1.2.1 · Experimental firmware**

Build an arrangement across eight tracks, placing clips anywhere on the timeline.
This guide starts with the everyday workflow, then covers independent clip
versions, detailed editing, piano roll, bounce and project backups.

## 1. The essentials

### Enable Linear for a project

Open **Project Settings → General → arranger mode** and choose **Linear**.
Press **Arrange** to open the timeline. This choice is saved with the project.
Switching back to Song retains the arrangement. Leaving the timeline does not
turn off Linear playback. Native patterns remain on their tracks in Song mode,
including their notes and instrument settings; switching modes does not generate
Song scenes from the timeline. Switching back to Linear restores its placement.

### Understand the screen

- **Header:** `Linear by Veff` identifies the timeline.
- **Tracks:** keys **1–8** select a track and highlight its lane. Entering Linear
  follows the currently selected instrument; AUX tracks have no timeline lane.
- **Edit cursor:** the position where you add clips or start playback.
- **Playhead:** the current playback position, separate from the edit cursor.
- **Selection:** the clip under the cursor on the selected track is selected
  automatically. In a gap, no clip is selected.
- **Bar lines:** faint dashed lines help you place clips. Zoomed-out views show
  every second or fourth bar.
- **Feedback:** actions and errors appear at the right for about three seconds.
  The footer shows the basic controls; holding Shift shows the 1–8 action and
  loop shortcuts, and holding Stop shows its editing shortcuts. Bounce retains
  its progress display. Raw tick/debug counters are no longer shown.

Clips can start between grid points. Gaps are silent. Clips cannot overlap on
the same track; a conflicting edit is rejected without moving other clips.

### Know what a clip contains

A clip is a timeline instance of a **source pattern**. Letters **A–P** identify
sources within each track.

**Two clips marked A on the same track share their notes.** Editing either A
changes both. Their timeline positions, lengths and offsets remain independent.
A new source contains 64 steps; its initial timeline clip exposes 16 steps.
E4 can reveal more of the source, up to its native length (four bars at normal
16th-note timing), and shorten it to one native step. Changing the source timing
in the stock editor scales its timeline instances automatically, stopping at the
next clip on the same track. Slower timing can therefore span more than four bars.
A change to the source's internal step count also limits the visible duration.
With a clip selected in Linear, hold **Bar** to change its native track scale;
this edits the highlighted clip's source, even if Song selects another pattern.
All timeline instances sharing that source follow its timing.

To change only one instance, select it and press **Shift + 2 — Make Unique**
before editing its notes. Source letters are local to each track: A on track 1
is not the same source as A on track 2.

### Mute one clip

Select a clip, then hold **Stop** and click **E1**, or choose **Toggle Mute Clip**
in the actions menu. The clip turns gray and stops producing sequenced notes;
its label and selection outline remain visible. Repeat to unmute. This changes
only the selected timeline instance, not other clips sharing its source. Mute
is saved with the project and supports undo/redo. The Stop chord does not move
the playhead when released.

### Read the shortcuts

**E1–E4** are the four encoders. **Turn** means rotate; **click** means press the
encoder. Hold **Shift** or **Stop** while performing the other part of a chord.
Plain **1–8** always means the track keys, except inside piano roll.

### Find an action without memorizing its chord

On the timeline, **press Arrange briefly** to open the actions menu. Press it
again to cancel and return to Linear. Holding Arrange still opens the same menu;
releasing it leaves the menu open. Turn **E1** to browse, turn **E2** to adjust
an available value, and click **E1 or E2** once to execute the selected action.
This menu gesture is available from the timeline, not from piano roll or the
stock note editor. Arrange in the stock note editor still returns to Linear.

The menu has 24 actions and displays the existing shortcut beside each action
that has one:

- Split, Make Unique, Undo and Redo: **Shift + 1–4**.
- Loop selected clip, loop on/off, loop start and loop end: **Shift + 5–8**.
- Trim left, trim right at cursor, Slip contents and Move to track.
- Edit notes, Piano roll, Bounce to Sampler and Bounce to Drum Sampler.
- Add clip, Paste, Duplicate, Cut and Delete.
- Add existing clip and Import Song at cursor.
- Toggle Mute Clip: **Stop + click E1**.

Unavailable actions are dimmed and show the reason. Existing Shift/Stop button shortcuts
remain usable: they close the menu and perform their usual action.

A click on **E1 or E2** executes the highlighted action immediately. For **Slip
contents**, **Move to track**, **Add existing clip** and **Import Song**, turn
**E2** to choose the value inside the menu before clicking. **Trim left** trims
to the cursor; the cursor must be inside the clip. There is no second confirmation
on the timeline. Press **Arrange** to close the menu without applying a choice.
The direct encoder shortcuts still edit immediately.

**Shift + Arrange** also opens this menu in a Linear project; it does not enter
Song mode. Use the project setting to switch modes deliberately.

**Add existing clip:** choose a source A–P on the selected track with E2, then
click E1 or E2 once to place another instance at the cursor. It shares the source's notes
and instrument; it does not allocate another pattern.

**Import Song at cursor:** choose a Song with E2 and click E1 or E2 to insert its eight
instrument lanes. Existing clips are retained; a collision, unsupported scene
state or insufficient capacity rejects the entire import. Repeated scenes retain
their duration. AUX/send automation is not copied. The native Song is unchanged.
Stop playback and recording before importing or adding an existing source.
Import requires all eight instrument tracks unmuted in the current native scene;
it rejects source timings 1/9 and 1/32, which the current phase model cannot
represent exactly.

The two bounce entries start bounce immediately with the selected sampler type
when the destination below is free. To replace an occupied destination, use the
regular **Shift + Sample** bounce controls and their replacement confirmation.

## 2. Make your first phrase

1. Choose a track with **1–8** and move the cursor with **E1**.
2. With transport stopped and recording disarmed, press **+** to create an empty clip. Its new source has 64 steps at 1× speed; the visible clip starts at 16 steps (one bar).
3. Turn **E4** to set the clip length; the minimum is one native step; the maximum follows the source length and timing, capped at the next clip.
4. Click **E4** to open the stock keyboard/step editor and enter notes.
5. Release held editing keys, then press **Arrange** to return to the timeline.
6. Press **Play** to hear the arrangement from the edit cursor.
7. Press **Shift + 5** to loop the selected clip.
8. Press **Stop + +** to place a shared duplicate immediately after it.

For live recording, select the track and hold **Rec** while pressing **Play**.
Release both buttons: recording remains armed across loop wraps, and notes go
into the source of the clip currently playing on that track. Press **Rec** to
leave recording, **Stop** to stop, or **Play** without Rec to leave recording
and restart from the cursor. Gaps do not receive notes.
When recording from the stock note editor, the playing source must match the
source being edited. **Shift + Rec is unavailable** in this version.

## 3. Navigate, play and loop

### Move around the timeline

| Control | Action |
| --- | --- |
| E1 | Move the edit cursor |
| Click E1 | Toggle GRID / FINE |
| Shift + E1 | Jump between clip starts and ends on the selected track |
| E2 | Zoom around the edit cursor |
| 1–8 | Select a track |

**GRID** snaps cursor movement to sequencer-step boundaries. **FINE** moves one
tick at a time; one tick is 1/1920 of a quarter note. Zoom responds immediately
in small increments, with eight steps per doubling.

### Control playback

| Control | Action |
| --- | --- |
| Play | Start or restart from the edit cursor |
| Shift + Play | Resume from the playback position |
| Stop | Stop and return the playhead to the edit cursor |
| Second consecutive Stop | Return to the start of the entire arrangement |
| Rec + Play | Record into the playing clip on the selected track |

### Set a loop

| Control | Action |
| --- | --- |
| Shift + 5 | Set A/B to the selected clip's boundaries and enable looping |
| Shift + 6 | Toggle looping without losing A/B |
| Shift + 7 | Set loop start A at the cursor |
| Shift + 8 | Set loop end B at the cursor |

For a loop spanning several clips, move to its start with E1, press Shift + 7,
move to its end, press Shift + 8, then enable looping with Shift + 6 if needed.

## 4. Everyday clip editing

| Control | Action |
| --- | --- |
| + | Add an empty clip at the cursor |
| − | Delete the selected clip, keeping the clipboard unchanged |
| Shift + − | Cut the clip and move the cursor to its former start |
| Shift + + | Paste and advance the cursor to the pasted clip's end |
| Stop + + | Duplicate the clip immediately after itself |
| E3 | Move the whole clip; the cursor follows |
| Shift + E3 | Move the clip with fine adjustment |
| E4 | Change length, down to one grid step |
| Shift + E4 | Fine length adjustment |
| Shift + 3 / 4 | Undo / redo a timeline edit |

Beginning a length adjustment moves the cursor and playhead to the clip's start.

### Build several repetitions quickly

Select a clip, press **Shift + −**, then **Shift + +** to restore it at the same
position. The cursor advances to its end. Keep pressing **Shift + +** to append
more instances: pasting retains the clipboard.

On the same track, these instances share their source. Use **Shift + 2** on any
one of them to make a variation without changing the others.

### What undo includes

Timeline undo retains **16 edits**, including clip operations and loop changes.
Consecutive turns of the same encoder on the same clip form one edit; navigation
or another action ends that group. Undo restores timeline references, selected
track and cursor. It does not restore edits to a source's notes or instrument.

History is session-only. Loading a project or completing a bounce clears it.
New source slots remain allocated after undo so redo and other references remain
valid; undo does not reclaim those slots.

## 5. Edit notes

### Stock keyboard / step editor

Click **E4** on a selected clip to open its source in the stock editor. Press
**Arrange** to return after releasing held editing keys.

### Piano roll

Press **Shift + click E4** to open the selected source in piano roll. It shows
eight pitches at a time and scrolls as the pitch cursor moves beyond them.
It opens around the first stored note, including its time and pitch, and shows
the total note count. `OUTSIDE VIEW` means the source has notes outside the
current viewport. The heading is **PIANO ROLL**; the note under the time/pitch
cursor is selected automatically.

**Stop playback and disarm REC before changing notes.** You can browse during
playback, but leave piano roll to record. Piano roll edits the source pattern,
including notes shared by other instances; use Make Unique first if necessary.

| Control | Action |
| --- | --- |
| E1 | Move the time cursor |
| Click E1 | Toggle GRID / FINE |
| E2 | Zoom time around the cursor, down to one sequencer step |
| E3 | Transpose the selected note; in a gap, move the pitch cursor |
| E4 | Change selected note length using GRID / FINE |
| 1–8 | Select one of the eight visible pitches |
| Shift + E1 | Move the selected note in time, one tick per step |
| Shift + E3 | Move the pitch cursor and scroll without transposing |
| Shift + E4 | Change velocity, from 1 to 127 |
| + | Add a note at the cursor |
| − | Delete the selected note |
| Shift + 3 / 4 | Undo / redo a note edit |
| Arrange | Return to the timeline |

Note history is separate from timeline history and retains **eight edits**.
Consecutive changes to the same note/control are grouped. Reopening piano roll
resets this history. Editing and selection operate on one note at a time.

## 6. Advanced clip editing

### Split a phrase

Place the cursor inside a clip and press **Shift + 1**. This creates two adjacent
clips sharing the original source, preserving the phrase's timing and source
phase even at different pattern speeds.

Splitting at an existing edge or without a free clip slot is rejected. To change
only one half's notes, select that half and use **Shift + 2** afterward. Make
Unique copies the whole source pattern, not just the visible slice.

### Trim an edge or shift the contents

| Control | Action |
| --- | --- |
| Stop + − | Trim the right edge to the cursor |
| Stop + E4 | Move the left edge, retaining the right edge and source phase |
| Shift + Stop + E4 | Fine left-edge adjustment |
| Shift + Stop + E3 | Shift source contents inside the clip, keeping both edges fixed |

Left trim changes how much of the phrase is exposed. Shifting the contents
changes which part of the source plays inside the existing time range. E4 can
shorten a clip below one bar, including to one quarter of a bar. Fine length
edits allow one tick; switching back to the grid does not enlarge an already
shorter clip. A clip can never have zero length.

### Move or paste onto another track

Use **Stop + E3** to move a clip to the previous or next track. Alternatively:

1. Cut with **Shift + −**.
2. Select the destination track with **1–8**.
3. Position the cursor with **E1**.
4. Paste with **Shift + +**.

Cross-track moves and pastes copy an **independent source**, including its notes
and instrument settings. They require a free source slot and unlocked source
and destination instruments. Collisions, locks and full destinations reject the
operation without changing the original.

## 7. Bounce a clip to audio

Bounce performs audible, real-time internal resampling and places the result on
the **track below**, at the same timeline position. The source clip stays intact.

1. Disarm REC and select a clip on tracks **1–7**.
2. Press **Shift + Sample** to open the bounce controls.
3. Turn **E1** to choose **Sampler** or **Drum Sampler**.
4. Turn **E2** to choose a free destination or replacement of overlapping clips.
5. Click **E4** to start. Replacement requires a second click to confirm.
6. Listen and watch the progress. Press **Stop** to cancel.

Completion assigns the sample and creates its trigger clip. Other editing is
blocked while capture, saving and assignment finish. A locked destination
instrument or full source pool prevents bounce.

The sample is saved in the stock **User** sample directory under a stock-generated
name, without an added date. The length limit is slightly below **20 seconds**.
This is not a sample-exact or dry stem export: leading silence and a capture tail
can remain, and output effects, normalization and edge fades are retained.
Bounce preserves leading and trailing rests in the requested region; the stock
sample recorder’s automatic silence trimming still applies to ordinary recordings.
An audio file can remain in User if later assignment fails or is cancelled.
The save indicator closes after pending bounce work has finished, including on
failure or cancellation. A failure reason remains in the footer until the next
bounce: capture/transport, short capture, sample write, file verification or
destination assignment. Report that reason when a bounce reaches the end but fails.

## 8. Save, back up and restore

The project uses stock autosave. Linear mode and arrangement data live beside
the stock project in a companion file:

| File | Contains |
| --- | --- |
| `song.xy` | Stock project data |
| `song.xy.oxyl` | Linear mode and arrangement |
| `song.oxy`, when present | Custom-machine mappings |

**Back up and restore these matching files together.** MTP exposes the `.oxyl`
companion. If you rename a project, preserve the shared basename. Copying only
`.xy` does not preserve its arrangement. Also retain any samples the project
uses, including bounce files; the companion is not an audio archive.

Missing or mismatched arrangement data opens the project in Song mode. Restoring
a project clears the clipboard and timeline history.

Current builds write companion **version 3**, preserving per-clip mute and
fractional source phase. They read versions 1 and 2, whose clips start unmuted.
Older firmware cannot read version 3: keep a pre-upgrade backup of the `.xy`
and matching `.xy.oxyl` before saving with this build if you may downgrade.

Sidecar writes use a temporary file and recheck the stock project's bytes before
renaming it. A valid matching temporary companion can be recovered if the main
one is missing or invalid. **The project files are still not an atomic pair
against power loss during saving.**

## 9. Limits and troubleshooting

| Situation | What to check |
| --- | --- |
| No clip is selected | Choose the correct track and place the cursor inside a clip |
| Editing one clip changes another | They share a source; use Shift + 2 first |
| Move, paste or split is rejected | Check overlap, source/clip capacity and instrument locks |
| Piano roll will not change notes | Stop transport and disarm REC |
| Recording produces no notes in a gap | Recording requires a playing clip on the selected track |
| Bounce will not start | Check REC, clip length, destination track, locks and source capacity |
| Project opens in Song mode | Restore the matching `.xy.oxyl` alongside `.xy` |

Capacity is **256 clips**, **16 sources per track**, and **120 notes per source**.
Deleting clips or undoing source creation does not remove or renumber sources.
The screen draws up to **64 visible clips or notes** and reports overflow.

For installation, use the [build and install guide](build.md#5-install). MVP-010
requires its matching firmware, catalog and **new
`content/display/open-xy-arranger.svg`**. Updating only `os.ldr` is insufficient.

The menu and new editing features still require hardware acceptance. Host tests and exact-image
reconstruction do not establish device timing, recovery, or protection against
power loss. The separately downloaded upstream beta is not yet integrated with
this arranger build.
