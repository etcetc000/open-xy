# Macro Osc

47 Braids oscillator models by Emilie Gillet, adapted to the SDK's explicit
lifecycle and drawing display. The [vendor license](vendor/LICENSE) and
[source provenance](vendor/VENDOR.md) remain with the port.

Four encoders: **Model**, **Timbre**, **Color** and **FM Ratio**; pressing the
fourth switches it to **FM Depth** and back. Timbre/Color retain each model's
upstream meaning. FM retunes the oscillator with a sine modulator at 0.5 to 8
times the note frequency; depth 0 is the unmodulated model. The complete
model/code/label list is in [models.hpp](models.hpp). The screen shows the
model code on four glowing 14-segment characters and knobs for Timbre, Color
and, joined on the right, FM Ratio and FM Depth; each knob always shows its
own parameter, and the fourth value reads `2.0` or `40.0` for whichever the
encoder edits. Timbre, Color and FM Depth show 0.0–100.0 without a percent
suffix. Shift + turn gives finer adjustment of continuous values.
FM Ratio interpolates between its harmonic anchors, including intermediate
ratios; the model selector stays discrete.
Output is 6.8 dB below half scale (`kOutputLevel`), so a stock volume readout
sounds about as loud as on stock instruments and 2op FM.

```sh
python sdk.py preview --engine macroosc --out local/macro-preview-001
python tools/serve_harness.py --preview local/macro-preview-001
python sdk.py check
```

| File | Owns |
| --- | --- |
| [definition.json](definition.json) | Controls, resource declarations and the display's budget |
| [core.h](core.h), [engine.cpp](engine.cpp) | Voice/shared state, lifecycle and sound |
| [display.h](display.h), [display.cpp](display.cpp), [segments.hpp](segments.hpp), [models.hpp](models.hpp) | The frame: 14-segment characters and glow drawn with nodes, dials and header values; the model table |
| [fm.h](fm.h) | FM ratio interpolation shared by sound and screen |
| [pitch.h](pitch.h), [vendor/braids/resources.cc](vendor/braids/resources.cc) | 44.1 kHz pitch and Braids tables |
| [machine.json](machine.json) | Kind, identity, builder glyph and the vendored translation units |

The SDK supplies storage, callbacks and rendering backends. Callbacks and the
vendor RNG swap require serialized access. See [engine API](../../docs/api.md).

## Port fidelity

Tables are regenerated for 44.1 kHz; Braids originally runs at 96 kHz. Delay-line
lengths are halved. Table-driven pitch/filter data use the new rate, but
hard-coded per-sample/per-block decay constants retain their upstream values,
so some decays are about 2.18 times slower and fixed filters lower. Half-rate
models run at 22.05 kHz; naive digital waveforms alias more. Pitch updates occur
at samples 0, 24 and 48 of each 64-sample callback, or every 8 samples while
FM depth is above 0. Oversampling is not implemented.

The module AD envelope, bit/rate reduction, signature waveshaper, quantizer and
VCO drift are not ported. Stock processing remains downstream on the device;
the browser has a simple audition gate.

## Native notes

Model selects `((q31 >> 16) * 47) >> 15`, clamped to 0..46. Timbre and Color
use 0..32767. FM Ratio interpolates in Q16 through 0.5, 1, 1.5, 2,
2.5, 3, 3.5, 4, 5, 6, 7 and 8; FM Depth (parameter slot 4) uses 0..32767.
The default targets a 1:1 ratio.
Changing model restrikes the oscillator. At depth 0, render produces
24 + 24 + 16 Braids samples per 64-sample block. Otherwise it renders eight
8-sample chunks, each at the note's phase increment scaled by
`1 + depth × sin(modulator)` (linear FM, between zero and twice the note at
full depth); the modulator phase advances at ratio × the note and restarts on
each note. The modulator aliases once ratio × note passes about 2.75 kHz, half
the 5.5 kHz update rate. Models that advance their envelopes once per render
call (for example the struck bell and drum) decay about 2.7 times faster while
depth is above 0. Output is int16 scaled to half-scale Q31.

Five models (SAW_COMB, PLUK, BOWD, BLOW, FLUT) lease an 8,200-byte delay line
from a shared pool of 16. When the pool is full the oldest lease is stolen and
its voice falls silent until restruck.
