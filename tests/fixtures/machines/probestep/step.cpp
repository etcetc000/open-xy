#include <opxy/step.hpp>

namespace probestep {
// Every callback, for the SDK's adapter checks: settings 2 and 3 replay the step
// 2 or 4 times and mute the replays; 6 ratchets 3 trigs; 7 jumps to step 0;
// 0, 1, 5 and 8 move the note; 9 sets velocity 13 and doubles the length.
struct Track {
    opxy::u32 passes;
    opxy::u32 entered;
};

void reset(Track& track) { track.entered = 0; }

bool condition(Track& track) { return track.passes == 0; }

void enter(Track& track, const opxy::StepContext&, opxy::u32) { ++track.entered; }

opxy::u32 trigs(Track&, opxy::u32 setting) { return setting == 6 ? 3 : 0; }

bool mute(Track& track, const opxy::StepContext&, opxy::u32 setting) {
    return (setting == 2 || setting == 3) && track.passes > 0;
}

void apply(Track&, const opxy::StepContext&, opxy::StepNote& note, opxy::u32 setting) {
    static const opxy::i32 kIntervals[opxy::step_settings] = {-12, -1, 0, 0, 0, 1, 0, 0, 12, 0};
    note.pitch += kIntervals[setting];
    if (setting == 9) {
        note.velocity = 13;
        if (note.length > 0) note.length *= 2;
    }
}

bool next(Track& track, const opxy::StepContext& at, opxy::u32 setting, opxy::StepNext& next) {
    if (setting == 2 || setting == 3) {
        if (++track.passes < (setting == 2 ? 2u : 4u)) {
            next.step = at.step;
            next.counts = false;
            return true;
        }
        track.passes = 0;
        return false;
    }
    if (setting == 7) {
        next.step = 0;
        return true;
    }
    return false;
}
}

namespace probestep {
// On a condition key: play every second visit, counted per track.
bool plays(Track& track, const opxy::StepContext&, opxy::u32) { return (++track.entered & 1) == 0; }
}
