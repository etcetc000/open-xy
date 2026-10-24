// Note players: a player is a stock Maestro object whose method table
// replaces the type, note-on and note-off methods (players.cpp). Stock's
// clocked step plays the chord records note-on appends, and releases them.
#pragma once
#include <opxy/player.hpp>

namespace opxy { namespace platform {
// A linked player. In a browser kit an unselected player's lookup resolves to
// a function returning null.
struct Player {
    u32 type;
    const char* name;
    const void* const* vtable;
    const char* page;        // absolute symbol: its own page ID, or null without one
    int (*page_ready)();     // 1 while that page is in its slot
};
struct Record;
struct Screen;
struct PlayerEntry {
    const Player* (*player)();
};

// The SDK state area inside a stock player slot, zeroed when the player is created.
void* player_state(void* object);
// The track's player object when it has this method table, else null.
void* player_object(u32 track, const void* const* vtable);
// Registers a player's own page (sdk_register_screen) and, once one is in
// place, lets the compositor paint it for the Maestro page.
int register_player_page(const Record* record, const Screen* screen);
// The track's four page parameters.
opxy::player::Controls player_controls(void* object);
// Appends one chord record that stock plays from the next step and releases
// with the key `root`; drops the plan when the object already holds eight.
void play_plan(void* object, u32 root, const opxy::player::Plan& plan);
// Stock Maestro methods a player keeps.
const u32 kMaestroVtable = 0x8409c3d8;
const u32 kMaestroRelease = 0x8d57c7f4;  // +0x18 (object, note)
}}

extern "C" {
extern const opxy::platform::PlayerEntry sdk_players[] __asm__("sdk_players");
extern const opxy::u32 sdk_player_count __asm__("sdk_player_count");
}
