// Punch-in effects stock doesn't have, built on opxy/punch.hpp. Each plays on
// its key on page 2 of the punch-in track (shift turns it on and off). Low
// octave (the drum side):
//   F   volume (silent -> own), drums F# lowpass, drums
//   G   snare roll (drum tracks)  G#  hipass, drums
//   A   sidechain pump (melodic)  A#  kill (releases on the next bar)
//   C   ratchet                   C#  master squash
//   D   echo                      D#  master clip
//   E   master gain               B   accent (only the beats play, boosted; sparser with intensity)
// High octave:
//   F   volume (silent -> own), melodic F# lowpass, melodic
//   G   gate (1/8 at rest to 1/32) G#  hipass, melodic
//   A   master filter sweep       A#  octave up (melodic tracks)
//   B   wah
//   C   FX I send boost           C#  FX II send boost
//   D, D#, E free
// Bend and tilt never pick a variation: punch::intensity turns their size,
// either way, into how much (the minimum, 20 %, at rest, up to 100 %).
// Effects with an animation draw it from punch_example/*.png; the rest keep
// stock's.
#include <opxy/punch.hpp>
#include "punch_example/animations.hpp"

namespace {
using namespace opxy;
using namespace opxy::punch;

const u32 kBar = 16 * ticks_per_step;
const i32 kFull = 0x7fffffff;

// Ratchet: each pattern note plays twice within its step at rest; bend or
// tilt either way climbs in equal zones through 3, 4 and 6 to 8. Repeats fade
// gently.
const u32 kRatchetCounts[] = {2, 3, 4, 6, 8};
const u32 kRatchetZones = sizeof kRatchetCounts / sizeof kRatchetCounts[0];
bool ratchet(const Context& context, Note& note) {
    if (note.generated) return true;
    const u32 amount = intensity(context.bend);
    u32 zone = 0;
    for (u32 top = intensity_low; zone < kRatchetZones - 1 && amount > top;
         top += (intensity_high - intensity_low) / (kRatchetZones - 1)) ++zone;
    const u32 count = kRatchetCounts[zone], gap = ticks_per_step / count;
    for (u32 i = 1; i < count; ++i) {
        const u32 velocity = note.velocity * (2 * count - i) / (2 * count);
        play(context, note.pitch, static_cast<u8>(velocity > 1 ? velocity : 1), gap > 1 ? gap / 2 : 1, i * gap);
    }
    return true;
}

// Echo: fading repeats a dotted eighth apart. At rest three, each at 60 % of
// the one before; the intensity sets one repeat at 40 % (20 %) up to five at
// 80 % (100 %).
bool echo(const Context& context, Note& note) {
    if (note.generated) return true;
    const u32 amount = intensity(context.bend), steps = 3;
    const u32 repeats = static_cast<u32>(by_intensity(amount, 1, 3, 5));
    const u32 feedback = static_cast<u32>(by_intensity(amount, 40, 60, 80));
    u32 velocity = note.velocity;
    for (u32 i = 1; i <= repeats; ++i) {
        velocity = velocity * feedback / 100;
        if (velocity) play(context, note.pitch, static_cast<u8>(velocity), ticks_per_step / 2, i * steps * ticks_per_step);
    }
    return true;
}

// Sidechain pump: on melodic tracks the level drops to silence on every beat
// and swells back to kPumpTop on a curve (slow first, fast at the end). The
// intensity is the aggressiveness, the swell's length: half a beat at rest,
// three quarters halfway and fifteen sixteenths at full. Outside the swell the
// track keeps its own level; drum tracks are left alone.
const i32 kPumpTop = 0x60000000;   // 75 %: an override can't read the track's own level
void pump(const Context& context, const Overrides& overrides) {
    if (context.drum) return;
    const i32 floor = 0;
    const i32 swell = by_intensity(intensity(context.bend), 2 * ticks_per_step, 3 * ticks_per_step,
                                   15 * ticks_per_step / 4);
    const i32 at = static_cast<i32>(context.ticks96 % (4 * ticks_per_step));
    if (at >= swell) return;
    const i32 curve = at * at * 1024 / (swell * swell);   // 0..1024, squared
    overrides.set(parameter_level, floor + (kPumpTop - floor) / 1024 * curve);
}

// Master filter sweep: MIX page 2 turns into a pure filter, gain and clip at
// zero and mix at full, while its tone sweeps its whole travel up and back.
// Bend or tilt, either way, sets the speed in equal zones: one cycle (up and
// back) in 12 bars at rest, then 8 and 4 bars, and from there each straight
// length followed by its triplet (two thirds of it): 4, 8/3, 2, 4/3, 1, 2/3,
// 1/2, 1/3, 1/4, 1/6, 1/8 and 1/12 of a bar, and 1/16 at full.
// The sweep keeps its place when the speed changes (no jump), and starts on
// the bar grid. The master section is shared, so only the effect's first
// track sets it.
// Cycle lengths in ticks (a bar is 384), by zone.
const u32 kSweepTicks[] = {4608, 3072, 1536, 1024, 768, 512, 384, 256, 192, 128, 96, 64, 48, 32, 24};
const u32 kSweepZones = sizeof kSweepTicks / sizeof kSweepTicks[0];
struct Sweep { u32 last, phase; u8 zone; };            // phase: 0..2^16 per cycle
Sweep sweeping;
u32 sweep_zone(i32 amount) {
    u32 zone = 0;
    for (i32 top = static_cast<i32>(intensity_low); zone < kSweepZones - 1 && amount > top;
         top += static_cast<i32>(intensity_high - intensity_low) / static_cast<i32>(kSweepZones - 1)) ++zone;
    return zone;
}
void sweep(const Context& context, const Overrides&) {
    if (!context.lead) return;
    const i32 amount = static_cast<i32>(intensity(context.bend));
    if (context.first) {
        sweeping.zone = static_cast<u8>(sweep_zone(amount));
        const u32 cycle = kSweepTicks[sweeping.zone];
        sweeping.phase = (context.ticks96 % cycle) * 0x10000 / cycle;
        sweeping.last = context.ticks96;
    } else {
        const u32 low = sweep_zone(amount - 12), high = sweep_zone(amount + 12);
        if (low == high) sweeping.zone = static_cast<u8>(low);
    }
    const u32 cycle = kSweepTicks[sweeping.zone];
    const u32 delta = context.ticks96 > sweeping.last ? context.ticks96 - sweeping.last : 0;
    sweeping.last = context.ticks96;
    sweeping.phase = (sweeping.phase + delta * 0x10000 / cycle) & 0xffff;
    const i32 rise = static_cast<i32>(sweeping.phase < 0x8000 ? sweeping.phase : 0x10000 - sweeping.phase);  // 0..0x8000
    master.set(Master::dist_gain, 0);
    master.set(Master::dist_clip, 0);
    master.set(Master::dist_mix, kFull);
    master.set(Master::dist_tone, kFull / 0x8000 * rise);
}

// Master squash: the master compressor (MIX page 3, third knob) while held, at
// 80 % at rest, full at 100 % intensity and 40 % at 20 %, and the master level
// (fourth knob) raised from the project's own by 30 % of its travel at rest
// (15 % at 20 %, 45 % at 100 %; never past full). The master section is
// shared, so only the effect's first track sets it.
void squash(const Context& context, const Overrides&) {
    if (!context.lead) return;
    const u32 amount = intensity(context.bend);
    master.set(Master::compressor, by_intensity(amount, 0x33333333, 0x66666666, kFull));
    const i32 own = master.project(Master::level), lift = by_intensity(amount, 0x13333333, 0x26666666, 0x39999999);
    master.set(Master::level, own > kFull - lift ? kFull : own + lift);
}

// Strength 0..800 for the filters, strongest on the press, falling with page
// 2's encoder travel (filter_strength eases it). Only the encoder rolls a filter back: tilt
// (64 x the accelerometer, past the dead zone at a few degrees) would open it
// on the press.
i32 rollback(const Context& context) {
    return static_cast<i32>(intensity_high - intensity(context.knob));   // 800 at rest .. 0 at full
}
// The cutoff's strength 0..800: the encoder's travel, eased by the square of
// the travel left. A straight line stayed dark through its first half and then
// opened in a few notches; the cube overcorrected (a drastic first notch, then
// little). The square sits between them.
i32 filter_strength(const Context& context) {
    const i32 left = rollback(context);
    return left * left / 800;
}

// The filters' screens: one still frame per encoder position (the strips hold
// 17, the press to open), so they move only when the encoder does.
const u32 kFilterFrames = 17;
Animation lowpass_view{lowpass_dots_frames, 0, 1}, hipass_view{hipass_dots_frames, 0, 1};
void show_position(Animation& view, const u8* const* frames, const Context& context) {
    view.frames = frames + static_cast<u32>(800 - rollback(context)) * (kFilterFrames - 1) / 800;
}

// Send boost: while B is held the track's FX I send knob is raised, while A#
// is held its FX II send; hold both for both. The bend sets how far: 75 % at
// rest, full at the top; the dry sound stays. With it the send effect itself
// grows: FX I's third parameter (a delay's feedback, say) gently, up to 30 %
// of the way to its top, and FX II's first (a reverb's length) up to 60 %.
// On release each returns to the track's own knob and the effects to theirs.
// On a padMix kit only keys that follow the track's send are boosted: a key
// with its own amount keeps it. Aux 5, the live input (a riser), follows too.
void boost(const Context& context, const Overrides& overrides, u32 send) {
    const i32 amount = static_cast<i32>(intensity(context.bend) - intensity_low);   // 0 .. 800
    const i32 sent = intensity_line(0x60000000, kFull, amount, 800);
    overrides.set(send, sent);
    if (!context.lead) return;
    aux.set(send, sent);   // the live input (a riser) too
    const bool first = send == parameter_send_fx1;
    const u32 track = first ? track_fx1 : track_fx2, parameter = first ? 2 : 0;
    const i32 own = tracks.own(track, parameter), room = kFull - own;
    const i32 top = own + (first ? room / 10 * 3 : room / 10 * 6);
    tracks.set(track, parameter, intensity_line(own, top, amount, 800));
}
void boost_fx1(const Context& context, const Overrides& overrides) { boost(context, overrides, parameter_send_fx1); }
void boost_fx2(const Context& context, const Overrides& overrides) { boost(context, overrides, parameter_send_fx2); }

// Volume: the targeted tracks go silent at rest and come back as the bend or
// tilt (either way) or page 2's encoder grows, in even steps, to their own
// level at full. The low F acts on drum tracks only, the high F on melodic
// ones. Each track's own level is read when the key goes down; on release
// the track's knob takes over again.
struct Volume { i32 own; };
Volume volumes[8];
void volume(const Context& context, const Overrides& overrides) {
    Volume& level = volumes[context.track];
    if (context.first) level.own = overrides.live(parameter_level);
    const i32 amount = static_cast<i32>(intensity(context.bend) - intensity_low);   // 0 .. 800
    overrides.set(parameter_level, intensity_line(0, level.own, amount, static_cast<i32>(intensity_high - intensity_low)));
}
void volume_drums(const Context& context, const Overrides& overrides) { if (context.drum) volume(context, overrides); }
void volume_melodic(const Context& context, const Overrides& overrides) { if (!context.drum) volume(context, overrides); }

// Filters: while held the track's filter (M3) becomes a z lowpass (F#) or z
// hipass (G#) with strong resonance; the low octave's keys act on drum
// tracks, the high octave's on melodic ones. Strongest on the press (the
// lowpass dark, the hipass thin); page 2's encoder rolls the cutoff back
// toward open, evenly (rollback); tilt leaves them alone. Key tracking (M3's
// fourth knob) is held at 0 so high notes can't open the filter. On release
// the track's own filter type, on/off, cutoff, resonance and key tracking
// come back.
const i32 kFilterResonance = 0x44000000;
const u32 kKeyTracking = 19;   // M3 page: cutoff 16, resonance 17, envelope 18, key tracking 19
void lowpass(const Context& context, const Overrides& overrides) {
    overrides.filter(filter_z_lowpass);
    overrides.set(parameter_cutoff, intensity_line(0x60000000, 0x0a000000, filter_strength(context), 800));
    overrides.set(parameter_resonance, kFilterResonance);
    overrides.set(kKeyTracking, 0);
}
void highpass(const Context& context, const Overrides& overrides) {
    overrides.filter(filter_z_hipass);
    overrides.set(parameter_cutoff, intensity_line(0x02000000, 0x66000000, filter_strength(context), 800));
    overrides.set(parameter_resonance, kFilterResonance);
    overrides.set(kKeyTracking, 0);
}
// Lowpass and hipass held together (F# and G# of one octave): the track's
// filter becomes stock's dynamic filter (type 18, a bandpass; words 16..19:
// low cut frequency, compression, drive, high cut frequency; both higher means
// higher on the device). With both cuts at one frequency the two slopes meet in
// a narrow band there. Page 2's encoder moves that frequency from the middle
// (untouched) down to the left or up to the right (Context::turn), both cuts
// together. Compression and drive sit at a moderate setting. The lowpass key
// runs it; the hipass key stands aside while its partner is held.
const i32 kBandMiddle = 0x48000000, kBandTravel = 0x30000000;   // 0.56, +-0.375
const i32 kBandCompression = 0x40000000, kBandDrive = 0x20000000;
void bandpass(const Context& context, const Overrides& overrides) {
    const i32 at = kBandMiddle + kBandTravel / 1000 * context.turn;   // right (+) raises the band
    overrides.filter(filter_dynamic);
    overrides.set(16, at);
    overrides.set(17, kBandCompression);
    overrides.set(18, kBandDrive);
    overrides.set(19, at);
}
// Both filter keys' screens show the band at the encoder's place, the middle frame at rest.
void show_band(const Context& context) {
    const i32 turn = context.turn < -1000 ? -1000 : context.turn > 1000 ? 1000 : context.turn;
    const u8* const* frame = bandpass_dots_frames + static_cast<u32>(turn + 1000) * (kFilterFrames - 1) / 2000;
    lowpass_view.frames = frame, hipass_view.frames = frame;
}
const u32 kLowLowpass = 1, kLowHipass = 3, kHighLowpass = 13, kHighHipass = 15;
void lowpass_drums(const Context& c, const Overrides& o) {
    show_position(lowpass_view, lowpass_dots_frames, c);
    if (key_held(kLowHipass)) show_band(c);
    if (c.drum) key_held(kLowHipass) ? bandpass(c, o) : lowpass(c, o);
}
void highpass_drums(const Context& c, const Overrides& o) {
    key_held(kLowLowpass) ? show_band(c) : show_position(hipass_view, hipass_dots_frames, c);
    if (c.drum && !key_held(kLowLowpass)) highpass(c, o);
}
void lowpass_melodic(const Context& c, const Overrides& o) {
    show_position(lowpass_view, lowpass_dots_frames, c);
    if (key_held(kHighHipass)) show_band(c);
    if (!c.drum) key_held(kHighHipass) ? bandpass(c, o) : lowpass(c, o);
}
void highpass_melodic(const Context& c, const Overrides& o) {
    key_held(kHighLowpass) ? show_band(c) : show_position(hipass_view, hipass_dots_frames, c);
    if (!c.drum && !key_held(kHighLowpass)) highpass(c, o);
}

// Swing (wah): a cycle on the sequencer's grid whose speed the
// intensity sets, in equal zones over its table of cycle lengths (ticks, 96 a
// quarter). The swing keeps its place when the speed changes.
struct Swing { u32 last, phase; };   // phase: 0..2^32 a cycle, so it never drifts off the grid
// The swing's phase now, 0..2^16 a cycle.
u32 swing_phase(Swing& s, const Context& context, const u32* ticks, u32 zones) {
    const u32 amount = intensity(context.bend);
    u32 zone = 0;
    for (u32 top = intensity_low; zone < zones - 1 && amount > top;
         top += (intensity_high - intensity_low) / (zones - 1)) ++zone;
    const u32 cycle = ticks[zone], step = 0xffffffffu / cycle + 1;   // 2^32 / cycle, rounded up
    if (context.first) s.phase = (context.ticks96 % cycle) * step, s.last = context.ticks96;
    const u32 delta = context.ticks96 > s.last ? context.ticks96 - s.last : 0;
    s.last = context.ticks96;
    s.phase += delta * step;
    return s.phase >> 16;
}
// 0 at phase 0 up to 0x8000 halfway and back, on a smooth (smoothstep) curve.
u32 swing_shape(u32 phase) {
    phase &= 0xffff;
    const u32 t = phase < 0x8000 ? phase : 0x10000 - phase;
    return ((t * t) >> 15) * (3 * 0x8000 - 2 * t) >> 15;
}

// Wah: on every track, drum and melodic, the track filter becomes a resonant z
// lowpass whose cutoff swings up and back (a swing): one cycle a bar at rest, then a half, a
// quarter, a sixth, an eighth and a twelfth, and a sixteenth at full. On
// release the track's own filter comes back.
const u32 kWahTicks[] = {384, 192, 96, 64, 48, 32, 24};
const i32 kWahLow = 0x14000000, kWahHigh = 0x5c000000, kWahResonance = 0x5c000000;
Swing wahs[8];
void wah(const Context& context, const Overrides& overrides) {
    const u32 shape = swing_shape(swing_phase(wahs[context.track], context, kWahTicks, sizeof kWahTicks / sizeof kWahTicks[0]));
    overrides.filter(filter_z_lowpass);
    overrides.set(parameter_cutoff, kWahLow + ((kWahHigh - kWahLow) >> 15) * static_cast<i32>(shape));
    overrides.set(parameter_resonance, kWahResonance);
}

// ---- META bank (high octave) ----

// Gate: the track is cut to silence for the second half of every slice, on
// the sequencer's grid; the first half keeps its own level. The intensity is
// the speed, in equal zones: eighths at rest, then twelfths, sixteenths and
// twenty-fourths, and 32nds at full.
const u32 kGatePeriods[] = {48, 32, 24, 16, 12};   // ticks: 1/8, 1/12, 1/16, 1/24, 1/32
const u32 kGateZones = sizeof kGatePeriods / sizeof kGatePeriods[0];
void gate(const Context& context, const Overrides& overrides) {
    const u32 amount = intensity(context.bend);
    u32 zone = 0;
    for (u32 top = intensity_low; zone < kGateZones - 1 && amount > top;
         top += (intensity_high - intensity_low) / (kGateZones - 1)) ++zone;
    const u32 period = kGatePeriods[zone];
    if (context.ticks96 % period >= period / 2) overrides.set(parameter_level, 0);
}

// Rolls: on drum tracks a pad repeats on the grid while held, from the
// press, its velocity rising from half to full over two bars. Page 2's encoder
// (not tilt or bend) sets the rate, a doubling a notch: quarters untouched,
// then eighths, sixteenths, 32nds and 64ths one notch each (a notch is 4 % of
// the encoder's travel; turning fast skips notches). A roll stays silent until
// the pattern plays one of its pads, then rolls the first one it plays and
// sticks to it until the key is released: the snare roll (low G) takes G, G#,
// A or A#. The first snare hit plays; every later snare hit from the sequencer
// is dropped during the roll, so only the roll sounds. Other pads leave a roll
// alone; the pattern keeps playing underneath; melodic tracks are left alone.
const u8 kSnareLow = 55, kSnareHigh = 58;
struct Roll { u32 start, last; u8 pitch, velocity; };
Roll rolls[8];
const i32 kRollNotch = 4 * intensity_span / 100;   // a detent in knob units (480)
u32 roll_period(const Context& context) {
    i32 notches = context.knob > intensity_dead_zone ? (context.knob - intensity_dead_zone + kRollNotch / 8) / kRollNotch : 0;
    if (notches > 4) notches = 4;
    return 4 * ticks_per_step >> notches;   // 96, 48, 24, 12, 6
}
u8 roll_velocity(u8 velocity, u32 elapsed) {
    const u32 rise = 2 * kBar, at = elapsed < rise ? elapsed : rise;
    const u32 scaled = velocity * (rise + at) / (2 * rise);
    return static_cast<u8>(scaled < 1 ? 1 : scaled > 127 ? 127 : scaled);
}
void snare_tick(const Context& context, const Overrides&) {
    if (!context.drum) return;
    Roll& roll = rolls[context.track];
    if (context.first) roll = Roll{context.ticks96, context.ticks96, 0, 0};   // silent until a pad plays
    const u32 now = context.ticks96, period = roll_period(context);
    const bool crossed = now / period != roll.last / period || now < roll.last;
    roll.last = now;
    if (crossed && roll.pitch) play(context, roll.pitch, roll_velocity(roll.velocity, now - roll.start), period / 2);
}
bool snare_note(const Context& context, Note& note) {
    if (!context.drum || note.generated || note.pitch < kSnareLow || note.pitch > kSnareHigh) return true;
    Roll& roll = rolls[context.track];
    if (roll.pitch) return false;   // the roll is on: the sequencer's snares are dropped
    roll.pitch = note.pitch, roll.velocity = note.velocity;
    return true;
}

// Kill: drum tracks keep only their kick pads (kKickLow..kKickHigh), melodic
// tracks only the lowest note starting on each tick (notes arriving after a
// lower one; a lower one arriving later still plays); everything else is
// dropped. From 90 % of the intensity every note goes. On release the effect
// holds until the next bar starts.
const u8 kKickLow = 53, kKickHigh = 54;
struct Lowest { u32 at; u8 pitch; };
Lowest lowest[8];
bool kill(const Context& context, Note& note) {
    if (intensity(context.bend) >= 900) return false;
    if (context.drum) return note.pitch >= kKickLow && note.pitch <= kKickHigh;
    Lowest& low = lowest[context.track];
    const bool keep = context.first || low.at != context.ticks96 || note.pitch <= low.pitch;
    if (keep) low.at = context.ticks96, low.pitch = note.pitch;
    return keep;
}

// Master clip and master gain: MIX page 2 (the master's M3) while held. Clip
// (second knob) goes from 50 % at rest to full at 100 % intensity, the mix
// (fourth knob) from 20 % to 50 % (full would be too loud). Gain (first knob)
// starts hotter, 75 % at rest with the mix at 35 %, up to full and 50 %. The
// other knobs keep the project's own. The master section is shared, so only
// the effect's first track sets it.
const i32 kMixHigh = 0x40000000;   // 50 %
void master_drive(const Context& context, Master knob, i32 knob_low, i32 mix_low) {
    if (!context.lead) return;
    const i32 amount = static_cast<i32>(intensity(context.bend) - intensity_low);   // 0 .. 800
    const i32 span = static_cast<i32>(intensity_high - intensity_low);
    master.set(knob, intensity_line(knob_low, kFull, amount, span));
    master.set(Master::dist_mix, intensity_line(mix_low, kMixHigh, amount, span));
}
void clip(const Context& context, const Overrides&) { master_drive(context, Master::dist_clip, kFull / 2, 0x19999999); }
void gain(const Context& context, const Overrides&) { master_drive(context, Master::dist_gain, 0x60000000, 0x2cccccc8); }

// Accent: only the notes on the beat play, boosted by +40 velocity; the rest
// are dropped. The intensity sets how sparse the beats are, in four equal
// zones: every quarter note at rest, then every half note (beats 1 and 3),
// every bar and every two bars.
bool accent(const Context& context, Note& note) {
    if (note.generated) return true;
    const u32 travel = intensity(context.bend) - intensity_low;   // 0 .. 800
    const u32 period = 4 * ticks_per_step << (travel >= 800 ? 3 : travel / 200);   // 96, 192, 384, 768
    const u32 into = context.ticks96 % period;
    if (into >= ticks_per_step / 2 && into <= period - ticks_per_step / 2) return false;
    const u32 louder = note.velocity + 40u;
    note.velocity = static_cast<u8>(louder > 127 ? 127 : louder);
    return true;
}

// Octave: melodic tracks play an octave higher (two at 60 % of the intensity's
// travel and up); drum tracks are left alone. Notes that would pass 127 stay.
bool octave(const Context& context, Note& note) {
    if (context.drum) return true;
    const u32 up = intensity(context.bend) >= 680 ? 24 : 12;
    if (note.pitch + up <= 127) note.pitch = static_cast<u8>(note.pitch + up);
    return true;
}

const Effect volume_drums_effect{0, volume_drums, nullptr, nullptr, &volume_dots};
const Effect lowpass_drums_effect{1, lowpass_drums, nullptr, nullptr, &lowpass_view};
const Effect highpass_drums_effect{3, highpass_drums, nullptr, nullptr, &hipass_view};
const Effect pump_effect{4, pump, nullptr, nullptr, &pump_dots};
const Effect fx2_boost_effect{20, boost_fx2, nullptr, nullptr, &boost_fx2_dots};
const Effect fx1_boost_effect{19, boost_fx1, nullptr, nullptr, &boost_fx1_dots};
const Effect ratchet_effect{7, nullptr, ratchet, nullptr, &ratchet_dots};
const Effect squash_effect{8, squash, nullptr, nullptr, &squash_dots};
const Effect echo_effect{9, nullptr, echo, nullptr, &echo_dots};
const Effect sweep_effect{16, sweep, nullptr, nullptr, &sweep_dots};
const Effect volume_melodic_effect{12, volume_melodic, nullptr, nullptr, &volume_dots};
const Effect lowpass_melodic_effect{13, lowpass_melodic, nullptr, nullptr, &lowpass_view};
const Effect highpass_melodic_effect{15, highpass_melodic, nullptr, nullptr, &hipass_view};
const Effect gate_effect{14, gate, nullptr, nullptr, &gate_dots};
const Effect roll_effect{2, snare_tick, snare_note, nullptr, &snare_roll_dots};
const Effect kill_effect{5, nullptr, kill, nullptr, &kill_dots, release_bar};
const Effect wah_effect{18, wah, nullptr, nullptr, &wah_dots};
const Effect clip_effect{10, clip, nullptr, nullptr, &clip_dots};
const Effect gain_effect{11, gain, nullptr, nullptr, &gain_dots};
const Effect accent_effect{6, nullptr, accent, nullptr, &accent_dots};
const Effect octave_effect{17, nullptr, octave, nullptr, &octave_dots};

// Page 2's idle screen: a dot hopping across, as on stock's punch-in screen.
const Animation page2_screen{page2_dots_frames, 0, 20};
}

// Page 2's idle screen, for the platform.
extern "C" const Animation* const sdk_punch_page2_screen = &page2_screen;

extern "C" const Effect* const sdk_punch_effects[] = {
    &volume_drums_effect, &lowpass_drums_effect, &highpass_drums_effect, &pump_effect, &fx2_boost_effect, &fx1_boost_effect,
    &ratchet_effect, &squash_effect, &echo_effect, &sweep_effect, &clip_effect, &gain_effect,
    &volume_melodic_effect, &lowpass_melodic_effect, &highpass_melodic_effect, &gate_effect, &roll_effect,
    &kill_effect, &wah_effect, &accent_effect, &octave_effect};
extern "C" const u32 sdk_punch_effect_count = sizeof sdk_punch_effects / sizeof sdk_punch_effects[0];
