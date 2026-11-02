// Four punch-in effects stock doesn't have, built on opxy/punch.hpp:
//   G  (stock repeat)        -> tape stop, drums and melodic
//   C  (stock kick fill)     -> ratchet, drums and melodic
//   D  (stock hi-hat fill)   -> echo, drums and melodic
//   A  (stock repeat 1/2)    -> sidechain pump, melodic only; drums keep stock A
// Each shows its own flip-dot animation, drawn in punch_example/*.png.
#include <opxy/punch.hpp>
#include "punch_example/animations.hpp"

namespace {
using namespace opxy;
using namespace opxy::punch;

const i32 kBendUp = 4260, kBendDown = -4260;   // stock F#'s thresholds
const u32 kBar = 16 * ticks_per_step;

// Tape stop: over one bar the tape slows to a halt. Steps fall further behind
// the playhead until they stop, melodic notes sink up to an octave and every
// note gets quieter; after the bar the track is silent.
u32 tape_start[8];
i32 tape_step[8];
u32 tape_elapsed(const Context& context) {
    if (context.first) tape_start[context.track] = context.ticks96;
    const u32 elapsed = context.ticks96 - tape_start[context.track];
    return elapsed < kBar ? elapsed : kBar;
}
void tape_tick(const Context& context, const Overrides& overrides) {
    if (tape_elapsed(context) >= kBar) overrides.set(parameter_level, 0);
}
bool tape_note(const Context& context, Note& note) {
    const u32 elapsed = tape_elapsed(context);
    if (!context.drum) {
        const i32 pitch = note.pitch - static_cast<i32>(12 * elapsed / kBar);
        note.pitch = static_cast<u8>(pitch < 0 ? 0 : pitch);
    }
    note.velocity = static_cast<u8>(note.velocity * (kBar - elapsed) / kBar);
    return note.velocity != 0;
}
bool tape_steps(const Context& context, i32& step) {
    const u32 elapsed = tape_elapsed(context);
    // In e ticks the tape covers e - e^2 / 2L: full speed slowing to a stop at L.
    const u32 behind = (elapsed * elapsed / (2 * kBar)) / ticks_per_step;
    const i32 slowed = step - static_cast<i32>(behind);
    // Across the pattern's end, and once stopped, stay on the last step.
    if (context.first) tape_step[context.track] = step;
    else if (elapsed < kBar && slowed >= 0) tape_step[context.track] = slowed;
    step = tape_step[context.track];
    return true;
}

// Ratchet: each pattern note plays two, three or four times within its step;
// bend up for four, down for three.
bool ratchet(const Context& context, Note& note) {
    if (note.generated) return true;
    const u32 count = context.bend > kBendUp ? 4 : context.bend < kBendDown ? 3 : 2;
    const u32 gap = ticks_per_step / count;
    for (u32 i = 1; i < count; ++i) {
        const i32 velocity = note.velocity - 12 * static_cast<i32>(i);
        play(context, note.pitch, static_cast<u8>(velocity > 1 ? velocity : 1), gap / 2, i * gap);
    }
    return true;
}

// Echo: three fading repeats a dotted eighth apart; bend up for eighths, down
// for quarters.
bool echo(const Context& context, Note& note) {
    if (note.generated) return true;
    const u32 steps = context.bend > kBendUp ? 2 : context.bend < kBendDown ? 4 : 3;
    u32 velocity = note.velocity;
    for (u32 i = 1; i <= 3; ++i) {
        velocity = velocity * 6 / 10;
        if (velocity) play(context, note.pitch, static_cast<u8>(velocity), ticks_per_step / 2, i * steps * ticks_per_step);
    }
    return true;
}

// Sidechain pump: the level drops on every beat and climbs back over half a
// beat in steps; bend up pumps on eighths. Outside the dip the track keeps
// its own level.
void pump(const Context& context, const Overrides& overrides) {
    static const i32 dip[] = {0, 0x06666666, 0x0f5c28f6, 0x1999999a, 0x26666666, 0x33333333};  // 0 .. 0.4
    const u32 beat = context.bend > kBendUp ? 2 * ticks_per_step : 4 * ticks_per_step;
    const u32 segment = (context.ticks96 % beat) * 12 / beat;
    if (segment < sizeof dip / sizeof dip[0]) overrides.set(parameter_level, dip[segment]);
}

const Effect tape_effect{{2, 2}, tape_tick, tape_note, tape_steps, {&tape_stop_dots, &tape_stop_dots}};
const Effect ratchet_effect{{7, 7}, nullptr, ratchet, nullptr, {&ratchet_dots, &ratchet_dots}};
const Effect echo_effect{{9, 9}, nullptr, echo, nullptr, {&echo_dots, &echo_dots}};
const Effect pump_effect{{no_key, 4}, pump, nullptr, nullptr, {nullptr, &pump_dots}};
}

extern "C" const Effect* const sdk_punch_effects[] = {&tape_effect, &ratchet_effect, &echo_effect, &pump_effect};
extern "C" const u32 sdk_punch_effect_count = 4;
