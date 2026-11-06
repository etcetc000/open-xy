# Machine UX

How a machine looks and behaves on the OP-XY, so every machine plays like part
of one instrument. Catalog review checks machines against these conventions.
[Macro Osc](https://github.com/etcetc000/open-xy-core-machines/tree/main/machines/macroosc)
is the reference for each of them, apart from its captions, which predate
[counting 00 to 99](#controls).

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
  rise. Name a control for what rises: more Bits is more bits, more Rate is faster.
- **Defaults are clean.** A new sound starts at the machine's plainest useful
  setting. A control that colours or degrades the sound (crush, drift,
  modulation depth) defaults to off.
- **Mirror stock's controls.** These are standards to try to follow, not fixed rules,
  and [stock breaks some of them too](#where-stock-breaks-these-standards). A control should read and move like the closest
  stock one, so a player's habits carry over. Stock's [synth engines](#stock-synth-engines) and
  [send effects](#stock-send-effects) show what to match. They apply to synths from SDK 1.11 and to
  new or updated send effects, not to sample engines; a machine already in the catalog keeps what
  it shows until its next [update](catalog.md#submit-a-machine).
- **Look to the engine and effect pages.** Stock's envelope (M2), filter (M3) and LFO (M4) pages use
  their own graphics, so they aren't a model for a machine's controls, and these standards don't
  apply to changing those pages.
- **A control stock shares, follow stock.** Where other stock engines or effects have the same or
  similar control, such as a ratio, use its range and behaviour.
- **Anything else, start with 00 to 99.** Stock's designers often chose a plain 00 to 99 over
  a more direct mapping: Axis's ratio shows 00 to 99, not a ratio. A number is cut off rather
  than rounded (`05`, and `99` at full travel), with a centred control's middle at 50.
  Generated captions and the stock FX page do this, and [`header_number`](display.md#effect-page)
  does it for your own text. Send effects lean on this even harder than synths, as
  [stock's do](#stock-send-effects).
- **Put useful values on the quarters.** On a continuous control, such as an engine's own filter, place what
  players aim for at 00, 25, 50, 75 or 99: unison, an octave, a neutral setting, a round time.
  Declare five [`anchors`](api.md#definition) and map them with `opxy::q31::anchored`, so each is
  exact on its number. One click moves one number, so a player can land on each. Put a split in behaviour at 50, as Axis's ratio does. This is about
  useful values: a default doesn't need to sit on a quarter.
- **Avoid decimals.** If a value would need one, use 00 to 99 or something else, as Axis's ratio
  does. Prism's ratios use whole numbers, such as `1:3`.
- **Use something else where it's clearly more logical.** For a few named settings or a stepping
  ratio, use an `enum`, which shows its name, such as `1:3`, [measured to fit](#screens). A stepped
  control whose steps land exactly on its values can show them the same way: an `enum` labelled
  `-1st`, `0st`, `+1st`. A continuous control shows 00 to 99. Any machine can also write its own
  text with [`Frame.header`](display.md#frame).
- **A choice takes several clicks.** An enum turns as Prism's ratio does: one click moves its
  hidden 00 to 99 by one, and its `n` choices share that travel evenly, so each takes about
  `100 / n` clicks: choice `k`, counting from 0, starts at `100 × k / n`. Prism's ten ratios take
  10 clicks each, and 25 semitones take 4, with `0st` on 48 to 51 and so centred. Two choices take
  50 each, as an on/off would. From SDK 1.11 a synth or send effect won't build with more than 100
  choices, since some would then fall between two clicks. Keep to about 50 or fewer, so each has
  at least two.
- **Cuts are fine.** A control can follow a curve or cut to a new range partway, as Axis's ratio
  and Organ's tone do. Keep the number rising as you turn, and put a cut on a quarter where you
  can, so players can find it.
- **Names are lower case and fit.** Stock's names are all lower case, such as `lo cut` and `fm`, so
  write them that way, with no capital at the start or anywhere else. Keep a name to one or two
  words, at most eight characters and about 49 px at the header's 10 px, so it stops before its value box.
  [Measure it](#screens): `feedback` is 41 px and fits, but `mmmmmm`, at 54 px, runs into the box on a synth's page on a device, while `mmmmmr`, at 48.5 px, stops just before it. The FX page's columns are wider, but one limit covers both. From SDK 1.11 a synth or send
  effect whose names break this won't build.
- **The OP-XY already modulates.** Its envelopes, LFOs, filter and FX have
  their own pages. An engine page holds the sound's own parameters, not
  another envelope, LFO or modulation amount.
- **Fine turns are free.** Shift + turn already gives continuous controls finer
  steps; don't build another.

## Where stock breaks these standards

- **Shared controls differ.** Prism's `ratio` shows ten ratios as text, while Axis's shows 00 to 99
  (guide). Organ's `tone` and Wavetable's `table` both pick from a fixed set, but only Wavetable's
  is stepped.
- **Over eight characters.** Phaser's `frequency` has nine, at 45 px, on the FX page's wider columns.
- **Not 00 to 99.** Prism's ratio shows text, which the standards allow where it's clearly more
  logical.
- **No values at all.** Organ's screen shows no numbers or titles over its drawing.

## Stock synth engines

The stock 1.2.1 synth controls with values where their behaviour changes, and what each starts
at. Organ's default is worked out the same way, though its screen doesn't show it. Defaults are
from the stock catalog, and the rest is from the firmware, as the
[firmware ABI](abi-1.2.1.md#control-values-and-captions) details. Listening on a device, the
changes in Axis's tremolo, Dissolve's swarm and E-piano's punch are smooth, with nothing that sounds
like a switch, and Hardsync's whole multiples don't stand out. Axis's tone and shape, Simple's shape
and Wavetable's position, warp and drift turn without jumps. So only Axis's ratio, Organ's tone and
the stepped controls change in a way a player hears. A control that isn't listed has nothing significant found so far, and the ABI page says
which weren't fully decoded.

| Engine | Control | Default | Significant values |
| --- | --- | --- | --- |
| Axis | ratio | 00 | Detune rises to 50 and holds to 54, then cuts to ten steps of ascending octaves and fifths, a new one every 5 from 55 (multiples 2, 3, 4, 6, 8, 12, 16, 24, 32). The guide puts the change at 51 |
| | tremolo | 00 | Below 50 only the amount rises. Above 50 the speed rises while the amount eases back |
| Dissolve | swarm | 50 | Two stages meet at 50: one part of the mix stops rising there and another starts |
| E-piano | punch | 25 | A knee at 25: gentle below it, steeper above, though it sounds smooth |
| Hardsync | freq | 50 | The synced oscillator runs from 1 to 8 times the note, on whole multiples at 00, 14, 28, 42, 57, 71, 85 and 99 |
| Organ | tone (the guide's type) | 40 | Eight organ types, starting at 00, 12, 25, 37, 50, 62, 75 and 87, with no detents |
| Prism | shape | 50 | Morphs through four stages, turning over at 25, 50 and 75 |
| | ratio | 00 | Ten ratios, shown as text: `2:1`, `1:1`, `2:3`, `1:2`, `1:3`, `1:4`, `1:6`, `1:8`, `1:12`, `1:16`, one for each ten of travel |
| Simple | pw | 00 | An even 50% pulse at 00, narrowing to 99% at full travel |
| Wavetable | table | 00 | One of nine tables, several clicks each (seen on a device) |

## Stock send effects

Every control on stock's six send effects, and on the FX filter, shows 00 to 99, even where a unit
looks natural: lofi's `bits`, dist's `lo cut` and `hi cut`, and phaser's `frequency` all show a plain
number. The only exceptions are delay's and reverb's `size`, which show names starting at these numbers:

| Starts at | 00 | 12 | 25 | 37 | 50 | 62 | 75 | 87 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Delay size | 1/32 | 1/32 dotted | 1/16 | 1/16 dotted | 1/8 | 1/8 dotted | 1/4 | 1/2 |
| Reverb size | micro | tiny | small | mid | large | larger | huge | insane |

The straight note lengths, and small, large and huge, land on the quarters. Delay's `fine` is
bipolar, with no offset at exactly 50, and a machine's centred control should work the same way.
This comes from the firmware, not a device, and the rest of the effects' sound code wasn't decoded.

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
- **Effects use the effect page.** A send effect with eight controls that
  draws its own page starts from [`opxy/effect_page.hpp`](display.md#effect-page),
  as the machine template's [`myeffect8`](https://github.com/etcetc000/open-xy-machine-template/tree/main/machines/myeffect8) does. It is laid out as
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
  column, and the choice `square` needs 61. A 10 px name has about 49 px.
- **One subject, and every control reads.** Draw the object or scene the sound
  acts on. At 00, 50 and 99, each control's effect is visible at a glance; a
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

- A contact sheet of every control at 00, 50 and 99, at 1:1, with text in the
  device's font.
- Controls that [mirror stock](#controls): numbers read as stock's, text is for
  choices or a real unit stock uses, and each significant value lands on its number.
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
