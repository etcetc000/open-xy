# Device test: punch-in second bank (page 2) and META bank

Build: any build of this branch (stock OS 1.2.1, with a machine such as
`probesynth`). Experimental: no hardware test, CPU measurement or release
verification of this branch has been done yet.

**Layout (page 2 of the punch-in track):**
low: F volume drums (silent at rest -> own level), F# lowpass drums, G snare roll, G# hipass drums, A pump,
A# kill, B accent, C ratchet, C# squash, D echo, D# master clip, E master gain;
high: F volume melodic, F# lowpass melodic, G gate, G# hipass melodic, A filter sweep, A# octave up,
B wah, C FX I boost, C# FX II boost; D, D#, E free (stock's). Low F# + G# together: bandpass. Filters (F#/G#) are strongest on the press and roll back toward open with tilt/encoder.

**Intensity rule (every page-2 effect):** at rest the minimum; bend or tilt
either way raises it to full; holding an encoder and turning it raises it too.
Variations are on their own keys; bend never switches them. Note which tilt
direction raises intensity: ____

## Install (docs/build.md, step 5)

1. Hold COM while switching on to open te-boot; press track 3 ("3. dev menu"),
   then track 2 ("2. mtp file transfer (mac)") and wait for "mtp mode active".
   Quit Field Kit, OpenMTP and the browser builder.
2. `python sdk.py device push "<build>/candidate"` (backs up, reads back, rolls back on failure).
3. Press track 2 ("2. exit"), then track 5 ("5. write os to boot partition"),
   keeping power on. On "firmware written ok!", switch off and on.
4. The boot screen shows `OPEN <version>-1.2.1`.

## Recovery

If it fails to boot, hangs or misbehaves: hold COM while switching on, open
te-boot and use "upload firmware" to reinstall the official OS 1.2.1 from
teenage.engineering. The pushed content files can stay (the push kept backups
under the private folder).

## Setup

A project with drum tracks (1-4) and melodic tracks (5-8), all with steps and
some FX I / FX II send, playing. Select the punch-in track for bank-2 tests.

## 0. Page 2

| # | Test | Expect | Pass | Fail |
| --- | --- | --- | --- | --- |
| 0.1 | Punch-in track, press and release shift | Screen shows PAGE 2; keys now play the custom effects | [ ] | [ ] |
| 0.2 | Press shift again | PAGE 2 gone; keys play stock again; any custom effect still held stops | [ ] | [ ] |
| 0.3 | Shift on another track | Stock's shift behaviour; page 2 unchanged | [ ] | [ ] |
| 0.5 | Page 2, hold any encoder and turn it while holding a key (e.g. D# sweep) | The effect's intensity rises; about half a turn reaches full. Turns feel ok (too slow/fast): ____ | [ ] | [ ] |
| 0.6 | Let go of the encoder | Intensity jumps back to rest (or to the tilt's, if larger); no click action fires | [ ] | [ ] |
| 0.6b | Page 2, turn an encoder without holding it, then let go | Intensity rises and stays; a click (press and release, no turn) returns it to 0; hold and turn from there raises it further and letting go returns to the turned value | [ ] | [ ] |
| 0.7 | Page 1, hold an encoder and turn | Stock behaviour, unchanged | [ ] | [ ] |
| 0.4 | After using F, release everything | Screen shows PAGE 2 (page 2) or stock's idle (page 1), not the velocity animation | [ ] | [ ] |

## 1. Stock punch-ins unchanged

| # | Test | Expect | Pass | Fail |
| --- | --- | --- | --- | --- |
| 1.1 | Punch-in track, each plain key, drum (low) octave | Stock's effect on drum tracks only, stock animation | [ ] | [ ] |
| 1.2 | Punch-in track, each plain key, melodic (high) octave | Stock's effect on melodic tracks only | [ ] | [ ] |
| 1.3 | Track 1 (drum) selected, shift + every key | Stock's effect on track 1 only, including F, G, A, C, D (repeat, fills) | [ ] | [ ] |
| 1.4 | Track 6 (melodic) selected, shift + every key | Stock's effect on track 6 only | [ ] | [ ] |
| 1.5 | Plain keys on instrument tracks, with and without recording | Notes play and record as stock | [ ] | [ ] |

## 2. Second bank: punch-in track, shift + key (either octave, all tracks)

| # | Key | Listen for | Pass | Fail |
| --- | --- | --- | --- | --- |
| 2.0 | Low octave only | Bank-2 effects below come from the **low** punch-in octave (101..112); the high octave plays the META bank (section 4) | [ ] | [ ] |
| 2.1 | Low F volume boost (drums) | Drum tracks' level from their own (unchanged at rest) up to full with tilt/bend/encoder, in even steps; melodic tracks untouched; knobs take over on release | [ ] | [ ] |
| 2.3 | A pump | Melodic tracks duck to silence on every beat and swell back: over half a beat at rest, longer (up to 15/16 beat) with tilt/encoder; drum tracks unaffected | [ ] | [ ] |
| 2.4 | A# FX II boost | FX II send at 50 % at rest, rising with tilt/encoder to 100 %; the dry sound always stays; Aux 5 (riser) follows; back to the knobs on release. Clicks at press/release: ____ | [ ] | [ ] |
| 2.5 | B FX I boost | Same for FX I; A# + B together boosts both | [ ] | [ ] |
| 2.6 | C ratchet | Each note 2x in its step at rest; tilt/encoder steps through 3x, 4x, 6x to 8x at full | [ ] | [ ] |
| 2.7 | C# master squash | Master compressor at 80 % at rest, full bent up, 40 % down; project value back on release | [ ] | [ ] |
| 2.8 | D echo | Dotted-eighth repeats: three at 60 % each at rest; bent up up to five at 80 %, down a single one at 40 % | [ ] | [ ] |
| 2.9 | High A filter sweep (lava-lamp screen) | Full tone travel; one cycle in 12 bars at rest, faster with tilt/encoder through 8, 4, 8/3, 2, 4/3, 1, 2/3, 1/2, 1/3, 1/4, 1/6, 1/8, 1/12 bar to 1/16 at full. Triplet steps audible: ____. Gain/clip/tone/mix return on release | [ ] | [ ] |
| 2.10 | Low D# clip, low E gain (master M3) | Clip (D#) or gain (E) at 50 % at rest up to 100 % at full intensity, the master mix knob 20 % up to 50 %; other M3 knobs keep the project's; release restores all. Not too loud at the top: ____ | [ ] | [ ] |
| 2.15 | Low B accent | Only the quarter-beat notes play, the rest are dropped; beats +40 velocity; the intensity thins the beats: every quarter at rest, then every half note, bar, two bars; screen: tall and short bars | [ ] | [ ] |
| 2.16 | High A# octave | Melodic tracks an octave up (two octaves from 60 % travel); drums unchanged; screen: the wave doubling | [ ] | [ ] |
| 2.14 | Low F# / G# (drums) and high F# / G# (melodic) | While held: the track filter becomes z lowpass (F#) or z hipass (G#) with a little resonance; tilt/encoder closes the lowpass / opens the hipass; on release the track's own filter (type, on/off, cutoff, resonance) is back. Clicks when switching; notes already sounding switch at once or only the next note: ____ | [ ] | [ ] |
| 4.13 | High B wah | Melodic tracks get a resonant lowpass swinging in time: once a bar at rest, faster with tilt/encoder up to a 16th; own filter back on release | [ ] | [ ] |
| 2.11 | G on page 2 | Stock's own G effect (free keys stay stock) | [ ] | [ ] |
| 2.12 | Any low-octave bank-2 key | Works on drum and melodic tracks alike (pump: melodic only) | [ ] | [ ] |
| 2.13 | Flip-dot screen while holding F, G, A, C, D, E | That effect's animation; A#, B, C#, D# keep stock's. Note if stock's idle shows instead: ____ | [ ] | [ ] |

## 3. Latching and edge cases

| # | Test | Expect | Pass | Fail |
| --- | --- | --- | --- | --- |
| 3.1 | Shift + E, release shift, keep holding E | Bar loop keeps running | [ ] | [ ] |
| 3.2 | ... then release E | Bar loop ends | [ ] | [ ] |
| 3.3 | Hold plain E (stock), press shift, release E | Stock's E ends normally, no bank-2 effect | [ ] | [ ] |
| 3.4 | Shift + C held, then plain D held (shift up) | Ratchet (all tracks) and stock D together; each ends with its key | [ ] | [ ] |
| 3.5 | Shift + G held, switch to track 3, release G | Tape stop ends (no stuck effect) | [ ] | [ ] |
| 3.6 | Shift + A held, change keyboard octave, release A | Pump ends; report if it sticks: ____ | [ ] | [ ] |
| 3.7 | Recording on, shift + each bank-2 key on the punch-in track | Effects play live; nothing is recorded into the punch-in pattern | [ ] | [ ] |
| 3.8 | Recording on, plain punch-in keys | Recorded as stock | [ ] | [ ] |
| 3.9 | Hold a step button, shift + key on the punch-in track | Note what stock does: ____ | [ ] | [ ] |
| 3.10 | Stop and start playback while holding a bank-2 key | No hang; effect follows the transport | [ ] | [ ] |
| 3.11 | 10 minutes of mixed use | No crash, no stuck effect, no audio dropouts | [ ] | [ ] |

## 4. META bank: punch-in track, shift + key in the high octave (113..124)

| # | Key | Listen for | Pass | Fail |
| --- | --- | --- | --- | --- |
| 4.0 | High F volume boost (melodic) | As 2.1 for melodic tracks only | [ ] | [ ] |
| 4.3 | G gate | Level cut to silence for the second half of every slice, locked to tempo: eighths at rest, faster with tilt/encoder through 1/12, 1/16, 1/24 to 32nds at full | [ ] | [ ] |
| 4.5 | Low G snare roll | Drum tracks roll the snare pad G from the press (G#, A or A# if the pattern plays that snare): eighths, sixteenths, 32nds, getting louder; each stage 4 bars at rest, faster with tilt/encoder. Other pads don't change it; melodic tracks unaffected | [ ] | [ ] |
| 4.6 | A# kill | Drum tracks keep only kick pads (assumed pitches 53/54, first two drum keys: check ____); melodic tracks keep the lowest note of each chord. Bent down: others only quieter; bent far up (90 %): everything silent | [ ] | [ ] |
| 4.7 | A# kill release | Release mid-bar: kill holds until the next bar starts, then everything returns on the downbeat. Release right on a bar: holds to the following one. Press again before the bar: keeps killing until the new release's bar | [ ] | [ ] |
| 4.8 | Low A# kill | Drums keep kicks, melodic the lowest note; holds to the next bar after release | [ ] | [ ] |
| 4.10 | High D, D#, E with shift | Stock's own effects (free) | [ ] | [ ] |
| 4.11 | Flip-dot while holding | Gates: pump animation; roll: ratchet; throws and kill: stock's | [ ] | [ ] |
| 4.12 | Kill held, stop transport, release, start | Kill ends at the first bar after playback starts (or at once if the position restarted) | [ ] | [ ] |

Tester: ______  Date: ______  Result: ______
