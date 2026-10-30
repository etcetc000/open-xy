// Note players: a player is a stock Maestro object whose method table
// replaces the type, routing mode, note-on and note-off methods (players.cpp). Stock's
// clocked step plays the chord records note-on appends, and releases them.
#pragma once
#include <opxy/player.hpp>

namespace opxy { namespace platform {
// A linked player. In a browser kit an unselected player's lookup resolves to
// a function returning null.
struct Player {
    const char* type_id;     // absolute symbol: the player's stock type, supplied at link time
    const char* name;
    const void* const* vtable;
    const char* page;        // absolute symbol: its own page ID, or null without one
    int (*page_ready)();     // 1 while that page is in its slot
    u32 type() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(type_id)); }
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
// Changes an existing root only; a stale update cannot revive a released key.
void update_plan(void* object, const opxy::player::Update& update);
// GUI snapshot of notes stock has emitted and still holds.
void player_output(void* object, opxy::player::Display& display);
// Stock Maestro methods a player keeps.
const u32 kMaestroVtable = 0x8409f6d0;
const u32 kMaestroRelease = 0x8d57d744;  // +0x18 (object, note)
const u32 kMaestroReset = 0x8d57d9a2;    // +0x1c (object)
const u32 kMaestroAllOff = 0x8d57d414;   // +0x28 (object)
}}

extern "C" {
extern const opxy::platform::PlayerEntry sdk_players[] __asm__("sdk_players");
extern const opxy::u32 sdk_player_count __asm__("sdk_player_count");
}
