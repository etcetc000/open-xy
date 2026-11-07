# Machine UX

How a machine looks and behaves on the OP-XY, so every machine plays like part
of one instrument. Catalog review checks machines against these conventions.
[Macro Osc](https://github.com/etcetc000/open-xy-core-machines/tree/main/machines/macroosc)
is the reference for each of them.

## Principles

- **Part of one instrument.** A machine should feel as if the OP-XY made it:
  its type, its greys, its motion and its page conventions. The sections below
  say how.
- **Every control reads.** Turning a control visibly changes the screen, and
  the change says what the control does to the sound. A player learns a
  machine by turning it, without a manual.
- **Seen as much as heard.** A control's visual change is in proportion to
  how much it changes the sound: one with a big audible effect moves the
  picture a lot, a subtle one moves it less, but it stays easy to see while
  it turns. Exaggerating a quiet control misleads the player about what
  matters; hiding it leaves them guessing.
- **One clear subject.** A screen shows one object or scene the sound acts on,
  not a dashboard of meters.
- **Calm by default.** An idle screen is still; motion answers a note, a press
  or a turn.
- **Cheap enough to stack.** A voice or a screen runs beside many others, up
  to 24 voices across eight tracks, so budget for that: the
  [screen cost](#screens) and the [recommended CPU](profiling.md#add-the-catalog-field).
- **Musical first.** Defaults and presets sound good straight away. An extreme
  setting can be wild, but the settings people land on are usable.
- **Honest numbers.** CPU, levels and anything in the catalog come from the
  device, not from estimates.

## Controls

- **Clockwise goes up.** Turning an encoder clockwise makes its shown number
  rise. Name a control for what rises: Bits counts bits, Rate counts kHz.
- **Defaults are clean.** A new sound starts at the machine's plainest useful
  setting. A control that colours or degrades the sound (crush, drift,
  modulation depth) defaults to off.
- **Real units in the header.** A [header value](display.md#frame), or the
  machine's own header text when it draws one, shows what the musician thinks in: `2.0` for a ratio, `16` for bits, `11k` for a rate,
  a model's short code. Amounts are 0 to 100 without a percent sign. Keep it
  to about five characters.
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
  same saved parameter. Do not add filler parameters to reach eight. A drum
  kit's eight controls page the same way, always: a press switches the row
  between the key's first four controls and its last four.
- **Use the shared header.** New synths with five to eight controls use
  [`opxy/page_header.hpp`](display.md#two-page-header), already wired up by
  `sdk.py new` when `--controls` is 5, 6, 7 or 8. Choose the actual parameter
  count; the default is four controls with the stock header. Macro Osc remains
  the two-page visual reference.
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

## Aux tracks

A machine that [takes over an aux track](api.md#aux-tracks) is chosen and
played there as an engine is on tracks 1-8, so the same keys mean the same
things everywhere.

- **Shift + M1 chooses what runs on the track**, as it chooses the engine on
  tracks 1-8; on the send tracks it opens their effect list.
- **Shift + track key opens presets**, in two columns as on tracks 1-8:
  folders, then presets. Ship a preset named `init` with the machine's
  defaults; the builders add one for the stock role, so it stays listed.
- **The replacement owns its interface.** M1 returns to its page; M2/M3
  are its actions, without inherited stock pages to hide. Leave an unused
  action inactive. Enable `trackControl.sharedLfo` explicitly to use the
  shared M4 LFO page; otherwise M4 is another machine action.
- **Mute is the bypass.** Muting the machine's track (aux + track key, or the
  mixer) switches its effect off, as it does stock's brain; show it on the
  page (`Input.muted`) rather than adding a bypass of your own.

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

## Screens

How a screen looks is the machine's own. These hold whatever it draws.

- **Judge at 1:1.** The screen is 480 × 220 px; look at each frame at that
  size, not enlarged. Avoid parts under about 3 to 4 px, lines or hatching
  closer than about 3 px, text under about 8 px, and a difference shown only by
  a slightly dimmer grey. Prefer fewer, larger, higher-contrast parts.
- **Measure captions.** The device draws text in Univers Next Pro Light, the
  font the [screen template](../sdk/platform/opxy-1.2.1/screen.svg) names.
  Check every caption a control can show by its width in that font, not by
  counting characters: a 20 px header value has about 53 px before the next
  column, and `5.5mm` needs 64.
- **One subject, and every control reads.** Draw the object or scene the sound
  acts on. At 0, 50 and 100, each control's effect is visible at a glance; a
  tiny mark or a faint change of grey doesn't count.
- **Scale motion to the sound.** Judge each control's audible effect across
  its range, by ear or by the change in level or band energy, and scale its
  visual travel to match, keeping a generous minimum so the quietest control
  is still easy to see moving. Too subtle reads as broken. A shade fades
  through the greys rather than jumping, a moving part eases through its poses
  rather than snapping, and no control is only an on/off change in the drawing.
- **Keep the redraw small.** The device redraws only what changed, and that
  redraw is the cost; host time per frame doesn't show it. Aim for about 20
  changed nodes a frame on average while notes sound, with a low peak, at
  every control setting including the heaviest, and none at rest. Moving fills
  are expensive, so use them where they earn it, and check a heavy one on the
  device. A path whose `key` includes time
  redraws the whole path every frame; change the key only when its points
  change ([frame](display.md#frame)). To measure, run `display()` on the host
  and count the nodes that differ from the previous frame.
- **Check on the device.** The preview draws some outlines the device draws
  differently ([drawing on the device](display.md#drawing-on-the-device)).

## Before you submit

Before a [catalog submission](catalog.md#submit-a-machine), check:

- A contact sheet of every control at 0, 50 and 100, at 1:1, with text in the
  device's font.
- Every caption measured; outlines checked across a sweep of every control and
  of every pair of controls, since a control can switch on the drawing that
  faults; the redraw measured at the heaviest setting.
- Photos of the screen on a device: at rest, playing, and at the heaviest
  settings.
- CPU from the [hardware readout](profiling.md), for the tagged commit.
- Presets checked by ear, and [clean defaults](#controls).

## Color design guidance

These are optional suggestions for authors who want colorful machines. Adapt
the palette and visual identity to the user's intent. Palette size, visual
metaphors and contrast targets here are design advice, with no new validation
or catalog acceptance criteria. The control conventions above and the
[SVG contract](display.md#artwork) still apply.

The [OP-1 reference](https://teenage.engineering/guides/op-1/original/reference)
offers examples of distinctive engine visuals with recurring control colors.
Its [layout guide](https://teenage.engineering/guides/op-1/original/layout)
connects colored graphics to the corresponding encoders. For OP-XY, consider
that relationship between controls and artwork while retaining its grayscale
encoders, established header and positional conventions.

- **Give color a role.** A starting palette of two to four accent hues often
  works well for oscillators, parameter groups or signal paths. More colors
  can be useful when they convey additional information. Consider sketching a
  small role table: role, color, intended gray tone and another identifying cue.
- **Keep roles consistent.** The same oscillator or parameter group can keep
  its hue across pages, presets and animation states. Names and placement help
  connect the graphic to its encoder. Brightness, outlines or markers can show
  activity and selection while preserving the role's identity; follow the
  existing [motion conventions](#motion).
- **Keep the frame quiet.** Consider pure black backgrounds, neutral readable
  labels and subdued guides, with accents concentrated on meaningful graphics.
  This also keeps decorative tints from consuming the automatic converter's
  hue-separation budget. Preserve existing black backgrounds when designing a
  grayscale counterpart.
- **Choose a useful visual metaphor.** Connected nodes can explain routing,
  curves can explain waveform shaping, and boundaries can explain sample
  regions. Original artwork can give each machine its own character while
  helping the musician understand what changes when a control moves.
- **Plan grayscale alongside color.** Aim to preserve distinctions carried by
  hue before matching the source's brightness. Different hues with similar
  luminance may need deliberately different gray tones. Labels, shapes,
  spatial groups and outlines can reinforce those distinctions. A black gap
  around a selection outline can keep it visible beside bright fills.
- **Budget tones for the complete palette.** Count distinct hues and meaningful
  shades throughout the SVG and every animation palette, including off-screen
  donors; repeated colors and aliases do not need extra tones. With few roles,
  consider widely separated grays across the usable range. Reserve within-role
  shading for information it actually conveys. Dense palettes may benefit from
  fewer redundant shades or additional labels and shapes.
- **Review the rendered result.** Compare color and grayscale at the native
  480 × 220 size, including control extremes, notes and releases, page changes,
  selection and reduced motion. Check the closest tones against one another
  and their actual neighboring backgrounds. Useful review targets are at least
  [4.5:1 for small text](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html)
  and [3:1 for essential graphical marks](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html).
  These are practical targets borrowed from web accessibility guidance; visual
  review on the device remains useful, especially for thin strokes and small
  labels.
- **Review again when the palette changes.** Adding a color or animation donor
  can change the automatic tonal allocation. Consider offering a
  [developer-authored grayscale SVG](display.md#grayscale-artwork) when more
  control is useful. Review that version when supplied, or the automatic
  fallback otherwise, while keeping the same sound and controls in both modes.

## Saved sounds

Projects and presets store all eight parameter slots, and older sounds can
hold 0 in a slot the machine did not declare when they were saved. When a
released machine gains a parameter, make 0 sound as the machine did before, or say in its README which older sounds change and
how to restore them. Existing parameters keep their order and mapping
([definition](api.md#definition)).
