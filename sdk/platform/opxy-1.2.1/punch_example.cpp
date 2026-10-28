// Three stock punch-in effects rebuilt through opxy/punch.hpp, each on another
// key so it can be compared with the original by ear:
//   F  (stock mute)    -> octave, as stock A#
//   C# (stock cut)     -> stutter, as stock F#
//   D# (stock cut 2)   -> step hold from the punch-in track, as stock G
#include <opxy/punch.hpp>

namespace {
using namespace opxy;
using namespace opxy::punch;

// Stock A#: drum tracks up two pad octaves, melodic tracks down an octave,
// folded back into 0..127 by octaves.
bool octave(const Context& context, Note& note) {
    i32 pitch = note.pitch + (context.drum ? 24 : -12);
    while (pitch > 127) pitch -= 12;
    while (pitch < 0) pitch += 12;
    note.pitch = static_cast<u8>(pitch);
    return true;
}

// Stock F#: silence the track except at the start of each period, a 16th
// note, halved or doubled by bend. Drums sound for an eighth of the period,
// melodic tracks for half.
void stutter(const Context& context, const Overrides& overrides) {
    u32 period = 24;
    if (context.bend > 4260) period = 12;
    else if (context.bend < -4260) period = 48;
    const u32 sounding = context.drum ? period >> 3 : period >> 1;
    if (context.ticks96 % period > sounding) overrides.set(parameter_level, 0);
}

// Stock G from the punch-in track: every step plays the step that was playing
// when the key went down.
i32 held_step[8];
bool hold(const Context& context, i32& step) {
    if (context.source != Source::group) return false;
    if (context.first) held_step[context.track] = step;
    step = held_step[context.track];
    return true;
}

const Effect octave_effect{0, nullptr, octave, nullptr};
const Effect stutter_effect{8, stutter, nullptr, nullptr};
const Effect hold_effect{10, nullptr, nullptr, hold};
}

extern "C" const Effect* const sdk_punch_effects[] = {&octave_effect, &stutter_effect, &hold_effect};
extern "C" const u32 sdk_punch_effect_count = 3;
