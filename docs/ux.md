# Machine UX

How a machine looks and behaves on the OP-XY, so every machine plays like part
of one instrument. Catalog review checks machines against these conventions.
[Macro Osc](https://github.com/etcetc000/open-xy-core-machines/tree/main/machines/macroosc)
is the reference for each of them.

## Controls

- **Clockwise goes up.** Turning an encoder clockwise makes its shown number
  rise. Name a control for what rises: Bits counts bits, Rate counts kHz.
- **Defaults are clean.** A new sound starts at the machine's plainest useful
  setting. A control that colours or degrades the sound (crush, drift,
  modulation depth) defaults to off.
- **Count 00 to 99.** Every number a stock page shows is the control's place in
  its travel, `floor(100 × value)` as two digits, whatever the control does:
  a delay's feedback, a filter's cutoff and a synth's ratio all read 00 to 99.
  Caption a machine's own numbers the same way with
  [`header_number`](display.md#effect-page), so a value reads the same on every
  page. Text is for choices only: an `enum`'s label, a `division`'s note
  length, at most about five characters.
- **Significant values on the quarters.** Put what the musician aims for at
  00, 25, 50, 75 and 99, where the caption says so: unison, an octave, a fifth,
  a neutral or centred setting, a round ratio or time. Stock does this where it
  steps: delay's size reads `1/16` from 25, `1/8` from 50 and `1/4` from 75,
  with the dotted lengths between, and reverb's size reads `small`, `large` and
  `huge` from the same points. Its defaults sit there too: delay's fine and
  feedback, chorus's depth, feedback and stereo, the lowpass, highpass, svf
  and ladder filters' cutoff and every drumline control start at 50, epiano's
  punch at 25, dry levels at 99. Declare five [`anchors`](api.md#definition)
  for such a control and map it in C++ with `opxy::q31::anchored` and the same
  values, so each anchor is exact on its caption; a linear range whose quarters
  are already round (5 to 25 ms) needs none. Between anchors, move smoothly.
  Stock's own synth curves don't all follow this (Prism's ratio, below), so
  treat it as the convention for new machines, not a description of every
  stock engine.
- **Stepped, split and curved controls still count 00 to 99.** Stock 1.2.1
  captions every synth control with the same `%02i`, whatever it does inside:
  - *Stepped:* Wavetable's table moves one of nine tables per detent, and
    Prism's shape picks one of eight presets in equal eighths of the travel
    (`word >> 28`, so a new shape at 00, 12, 25, 37, 50, 62, 75 and 87) with
    no detents. For a handful of settings the musician names, use an `enum`,
    which steps one choice per detent and shows its label. Otherwise split
    the travel evenly, so each step starts where the caption says.
  - *Split:* Axis's ratio is a detune from 00 to 50 and ascending fifths
    above it. Put the boundary at 50 and make both halves start there.
  - *Curved:* Prism's ratio crossfades each shape's settings with quadratic
    weights, so its intervals move with the shape; Dissolve shapes a control
    through a nine-point curve. A curve is fine as long as it rises with the
    number and has no jumps the caption doesn't show.
- **Short labels.** One or two words, at most eight characters; the stock
  header writes them in lower case.
- **The OP-XY already modulates.** Its envelopes, LFOs, filter and FX have
  their own pages. An engine page holds the sound's own parameters, not
  another envelope, LFO or modulation amount.
- **Fine turns are free.** Shift + turn already gives continuous controls finer
  steps; don't build another.

## More than four controls

- **Two pages.** A synth with five to eight parameters uses
  [`"press": "page"`](api.md#alternate-controls): page 1 holds
  what is played most, page 2 the rest. A press of any encoder switches all
  four, so a press means the same thing everywhere. If a position has no
  alternate, repeat its first-page control in the second row; both edit the
  same saved parameter. Do not add filler parameters to reach eight.
- **Use the shared header.** New synths with five to eight controls use
  [`opxy/page_header.hpp`](display.md#two-page-header), already wired up by
  `sdk.py new` when `--controls` is 5, 6, 7 or 8. Choose the actual parameter
  count; the default is four controls with the stock header. Macro Osc remains
  the two-page visual reference.
- **Effects use the effect page.** A send effect with eight controls that
  draws its own page starts from [`opxy/effect_page.hpp`](display.md#effect-page),
  wired up by `sdk.py new --kind effect --controls 8`. It is laid out as
  stock's FX page rather than the synth header: both rows of controls in place
  under a badge naming the send, the edited row on a white band that slides to
  the other row on a press, the other row dimmed, and captions that read 00..99
  as that page's do.
- **Show both pages in place.** Page 1 stays on top and page 2 under it; a
  press does not swap them. The page the encoders edit is the stock header's
  size and sits on the stepped bar; the other is smaller and grey. Cover the
  stock header ([artwork](display.md#artwork)), which hides its bar too, and
  draw the header yourself: the bar's eight 60 × 20 px steps as nodes, and
  sixteen [text slots](display.md#frame) (`"sdk": "1.3"`) on the header's
  columns, titles at x `5 + 120 i` and values at `65 + 120 i`. A text slot
  keeps its authored fill, so use a bright set that follows the page being
  edited and a grey set that follows the other. This takes every text slot.
  With page 1 up, the rows are:

  | | Page 1 (edited) | Page 2 (grey) |
  | --- | --- | --- |
  | Bar | 0 to 20 px | none |
  | Title baseline, size | 10 px, 10 px | 33 px, 10 px |
  | Value baseline, size | 17.4 px, 20 px | 33 px, 13 px |
  | Fill | stock: `#CBCBCB` on the dark half, `#000000` on the light | `#5A5A5A` titles, `#707070` values |

  With page 2 up, page 1 shrinks to a grey row with both baselines at 13 px,
  and page 2 grows into the bar, moved down to 18 to 38 px: titles at 28 px
  and values at 35.4 px. Keep the rest of the artwork below about 38 px.
- **Author text at its resting size.** Scaling a text slot is decoration for
  a moment of motion: a layout must read correctly at unit scale.

## Motion

- **Motion answers the player.** Animate what a press, turn or note changed;
  an idle screen stays still.
- **Short and decelerating.** An exponential ease-out that settles within
  0.15 to 0.2 s. On a page switch each row grows or shrinks
  where it is: the edited page's values from 13 to 20 px, the other's from 20
  to 13, and the bar moves to the edited row
  ([Macro Osc's display](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/display.cpp)).
- **Reduced motion jumps.** With `reduced_motion` set, or on a track change,
  show the end state at once.
- **Grey is inactive, bright is active**: the page row, unlit segments, a dial
  not being edited.

## Saved sounds

Projects and presets store all eight parameter slots, and older sounds can
hold 0 in a slot the machine did not declare when they were saved. When a
released machine gains a parameter, make 0 sound as the machine did before, or say in its README which older sounds change and
how to restore them. Existing parameters keep their order and mapping
([definition](api.md#definition)).
