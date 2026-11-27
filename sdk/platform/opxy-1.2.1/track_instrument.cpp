// A note player's track instrument (resources.trackInstrument, SDK 1.25): the kind of
// instrument each instrument track holds, as Controls.instrument gives it. Linked only
// with the track_instrument service, which a player declaring the resource selects
// (tooling/native.py); every other build links neither this unit nor its hook, and
// compiles players.cpp and every player's own units as they are without it.
//
// The GUI publishes each track's kind once per compositor frame, at the frame's first
// slot (sdk_instrument_advance, track_instrument_hooks.s, enters here in place of
// sdk_screen_advance), so note callbacks never call stock's project getters. A player
// declaring the resource compiles its own units with OPXY_PLAYER_INSTRUMENT: its calls
// to player_controls then reach sdk_instrument_controls (players.hpp), which adds the
// kind to the controls players.cpp builds for every player.
#define OPXY_PLAYER_INSTRUMENT 1
#include <opxy/player.hpp>
#include "screen_order.hpp"
#include "stock.hpp"

namespace opxy { namespace platform {
// Controls as players.cpp builds and returns them, without instrument: its first
// 112 bytes. Returned the same way (in memory the caller provides), whatever the type.
struct PlainControls { u32 words[28]; };
// players.cpp's player_controls, declared here with the type it returns in a build that
// compiles players.cpp (a function's symbol leaves its return type out). This unit does
// not include players.hpp, whose declaration a declaring player's own units see.
PlainControls player_controls(void* object);
namespace {
static_assert(sizeof(PlainControls) == __builtin_offsetof(opxy::player::Controls, instrument),
              "Controls before instrument");
static_assert(sizeof(opxy::player::Controls) == sizeof(PlainControls) + 4, "Controls with instrument");

// Stock's drum predicate (1 for its drum sampler, engine 3, or its drum synth, engine 4)
// on the track's engine (stock's getter). The drums service hooks that predicate's entry
// to alias an SDK kit to 4 (drum_hooks.s), so in a build with kits it answers for them
// too; without that service there are none.
const u32 kDrumPredicate = 0x8d5b79f0;
volatile u8 instrument_published[kTracks];

u32 track_instrument(u32 track) {
    return stock<u32(u32)>(kDrumPredicate)(track_engine(track)) ? opxy::player::DrumInstrument
                                                                 : opxy::player::OtherInstrument;
}
}
}}

extern "C" {
opxy::u32 sdk_screen_next(opxy::platform::ScreenCursor* cursor, opxy::u32 selected) __asm__("sdk_screen_next");
opxy::u32 sdk_instrument_next(opxy::platform::ScreenCursor* cursor, opxy::u32 selected) __asm__("sdk_instrument_next");
opxy::player::Controls sdk_instrument_controls(void* object) __asm__("sdk_instrument_controls");
}

// The track's controls (players.cpp) with its kind of instrument: OtherInstrument on a
// track outside the eight instrument tracks.
opxy::player::Controls sdk_instrument_controls(void* object) {
    using namespace opxy::platform;
    const PlainControls plain = player_controls(object);
    opxy::player::Controls controls;
    __builtin_memcpy(&controls, &plain, sizeof plain);
    const opxy::u32 track = *reinterpret_cast<const opxy::u32*>(static_cast<const char*>(object) + 4);
    controls.instrument = track < kTracks ? instrument_published[track] : opxy::player::OtherInstrument;
    return controls;
}

// Each frame's first slot publishes every track's kind, then the compositor goes on as
// without the service (screen_order.cpp.in).
opxy::u32 sdk_instrument_next(opxy::platform::ScreenCursor* cursor, opxy::u32 selected) {
    using namespace opxy::platform;
    if (cursor->page == 0)
        for (opxy::u32 track = 0; track < kTracks; ++track)
            instrument_published[track] = static_cast<opxy::u8>(track_instrument(track));
    return sdk_screen_next(cursor, selected);
}
