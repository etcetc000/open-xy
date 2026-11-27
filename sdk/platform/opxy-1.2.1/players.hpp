// Note players: a player is a stock Maestro object whose method table
// replaces the type, routing mode, note-on and note-off methods (players.cpp). Stock's
// clocked step plays the chord records note-on appends, and releases them.
#pragma once
#include <opxy/player.hpp>

namespace opxy { namespace platform {
// A player's shift row (SDK 1.16): its enums' choice counts and default words.
// Entry i sits under page i / 4, encoder i % 4, as in a synth's Shift row.
struct PlayerShift {
    u32 count;               // 1..4 controls, 1..8 on a paged player
    u8 choices[8];           // 2..255 each, 0 beyond count
    i32 defaults[8];         // each default choice's centre word
};
// GUI-written input beside a player's State (players.cpp): the shift row's
// words, Shift+turn detent sums and encoder click counts. Words, written only
// by the GUI and read on any role.
struct PlayerInput {
    i32 shift_value[8];
    i32 shift_turns[8];
    u8 clicks[4];
};
// A linked player. In a browser kit an unselected player's lookup resolves to
// a function returning null.
struct Player {
    const char* type_id;     // absolute symbol: the player's stock type, supplied at link time
    const char* name;
    const void* const* vtable;
    const char* page;        // absolute symbol: its own page ID, or null without one
    int (*page_ready)();     // 1 while that page is in its slot
    u8 steps[8];            // enum choice counts; one is an absent slot; zero keeps stock editing
    bool paged;              // a press switches the page between the first four and the last four
    bool clicks;             // "press": "click": encoder 4's press is only a click, never stock's toggle
    const PlayerShift* shift;  // its shift row, or null
    u32 type() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(type_id)); }
};
struct Record;
struct Screen;
struct PlayerEntry {
    const Player* (*player)();
};

// The SDK state area inside a stock player slot, zeroed when the player is created.
void* player_state(void* object);
// The GUI-written input area after it, set to the shift row's defaults then.
volatile PlayerInput* player_input(void* object);
// The track's player object when it has this method table, else null.
void* player_object(u32 track, const void* const* vtable);
// Registers a player's own page (sdk_register_screen) and, once one is in
// place, lets the compositor paint it for the Maestro page. Installs the
// per-frame Shift publication and routes the press either way: the press
// switches a paged player's page only while that page is in place, and is
// counted in an unpaged player's clicks.
int register_player_page(const Record* record, const Screen* screen);
// The first parameter the track's page shows: 4 on a paged player's second page, else 0.
u32 player_row(u32 track);
// The track's eight parameters.
#if OPXY_PLAYER_INSTRUMENT
// A player declaring resources.trackInstrument compiles its own units with
// OPXY_PLAYER_INSTRUMENT (tooling/native.py): its controls come from the track
// instrument's unit, which adds Controls.instrument to players.cpp's.
opxy::player::Controls sdk_instrument_controls(void* object) __asm__("sdk_instrument_controls");
static inline opxy::player::Controls player_controls(void* object) { return sdk_instrument_controls(object); }
#else
opxy::player::Controls player_controls(void* object);
#endif
// Appends one chord record that stock plays from the next step and releases
// with the key `root`; drops the plan when the object already holds eight.
void play_plan(void* object, u32 root, const opxy::player::Plan& plan);
// Changes an existing root only; a stale update cannot revive a released key.
void update_plan(void* object, const opxy::player::Update& update);
// GUI snapshot of notes stock has emitted and still holds.
void player_output(void* object, opxy::player::Display& display);
// Stock Maestro methods a player keeps.
const u32 kMaestroVtable = 0x8409f6d0;
const u32 kMaestroRelease = 0x8d57d744;  // +0x18 (object, note)
const u32 kMaestroReset = 0x8d57d9a2;    // +0x1c (object)
const u32 kMaestroAllOff = 0x8d57d414;   // +0x28 (object)
const u32 kMaestroStep = 0x8d57d8cc;     // +0x34 (object), sequencer role
}}

extern "C" {
// The table startup builds from the selected machines' records (machine_tables.cpp).
extern const opxy::platform::PlayerEntry* sdk_players __asm__("sdk_players");
extern opxy::u32 sdk_player_count __asm__("sdk_player_count");
}
