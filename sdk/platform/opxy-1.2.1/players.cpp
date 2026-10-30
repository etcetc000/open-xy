// Stock OS 1.2.1 side of note players. Stock dispatches player types 2..4
// (Arpeggio, Hold, Maestro) from a branch tree and keeps one UI controller per
// type in a five-entry table. A player of the collection is built by the stock
// factory as a Maestro (type 4) and then takes its own method table, so the
// stock step and release play its chords and the Maestro page and controller
// edit its four parameters. The hooks (player_hooks.s) ask these functions:
//   factory 8d57ee6c   build an owned type
//   load    8d588d94   keep an owned type a project stored
//   UI      8c0af13a, 8c0af43e  index the controller table with Maestro's entry
//   names   8d5bddd8, 8d5bde60  convert between an owned type and its name
#include "keyboard_octaves.hpp"
#include "players.hpp"
#include "player_activity.hpp"
#include "registration.hpp"

extern "C" {
void* sdk_player_build(opxy::u32 track, opxy::u32 type, opxy::u32 flag) __asm__("sdk_player_build");
opxy::u32 sdk_player_keep(opxy::u32 type) __asm__("sdk_player_keep");
opxy::u32 sdk_player_ui_type() __asm__("sdk_player_ui_type");
const char* sdk_player_name(opxy::u32 type) __asm__("sdk_player_name");
opxy::u32 sdk_player_id(const void* name) __asm__("sdk_player_id");
opxy::u32 sdk_player_substitute(opxy::u32 selected) __asm__("sdk_player_substitute");
extern opxy::u32 (*sdk_screen_substitute)(opxy::u32) __asm__("sdk_screen_substitute");
}

namespace opxy { namespace platform {
namespace {
using registry::StockString;
using opxy::player::Plan;

const u32 kFactory = 0x8d57ee6c;       // (track, type, flag) -> object or null
const u32 kSelectedType = 0x8d57efb8;  // the selected track's live or stored type
const u32 kParameters = 0x8d5863d8;    // (track) -> the track's eight parameter words
const u32 kArpeggio = 2, kMaestro = 4, kStockTypes = 5;
const u32 kPlayers = 0x84efc768;       // each track's player object, or null
const u32 kPlayerSlots = 16;           // one per track
const u32 kMaestroPage = 46;           // the stock Maestro page (chord.svg)
const u32 kContinue = 0xffffffffu;

// Maestro object fields: chord records from +0x1dc, their count at +0x41c.
// A record's tone plays when its delay, decremented every step, reaches zero;
// release sends note-off for every played tone when the record's root matches.
typedef PlayerChord ChordRecord;
const u32 kRecords = 0x1dc, kHeld = 0x41c, kMaxHeld = 8;
// Each track's player lives in its own 64 KiB slot; stock objects end below
// 0x480, so the SDK state follows at a fixed offset.
const u32 kStateOffset = 0x800;
static_assert(kStateOffset + opxy::player::state_bytes <= 0x10000, "Player state exceeds the stock slot");

char* bytes(void* object) { return static_cast<char*>(object); }

const Player* owned(u32 type) {
    for (u32 i = 0; i < sdk_player_count; ++i) {
        const Player* p = sdk_players[i].player();
        if (p && p->type() == type) return p;
    }
    return nullptr;
}

bool equal(const StockString* string, const char* name) {
    u32 length = 0;
    while (name[length]) ++length;
    if (string->length != length || string->capacity < length) return false;
    const char* text = string->text;
    if (string->capacity >= 16) __builtin_memcpy(&text, string->text, sizeof text);
    if (!text) return false;
    for (u32 i = 0; i < length; ++i)
        if (text[i] != name[i]) return false;
    return true;
}
}

void* player_state(void* object) { return bytes(object) + kStateOffset; }

void* player_object(u32 track, const void* const* vtable) {
    if (track >= kPlayerSlots) return nullptr;
    void* object = *reinterpret_cast<void* const volatile*>(kPlayers + 4 * track);
    return object && *static_cast<const void* const* const*>(object) == vtable ? object : nullptr;
}

int register_player_page(const Record* record, const Screen* screen) {
    const int result = sdk_register_screen(record, screen);
    if (result == 1) sdk_screen_substitute = sdk_player_substitute;
    return result;
}

opxy::player::Controls player_controls(void* object) {
    const u32 track = *reinterpret_cast<const u32*>(bytes(object) + 4);
    const i32* words = stock<const i32*(u32)>(kParameters)(track);
    opxy::player::Controls controls;
    for (u32 i = 0; i < 4; ++i) controls.value[i] = words[i];
    const i32 octave = track < kTracks ? sdk_track_octaves[track] : 0;
    controls.lowest_key = static_cast<u32>(static_cast<i32>(kLowestKey) + 12 * octave);
    return controls;
}

void play_plan(void* object, u32 root, const Plan& plan) {
    volatile u32& held = *reinterpret_cast<volatile u32*>(bytes(object) + kHeld);
    if (!plan.count || plan.count > opxy::player::max_tones || held >= kMaxHeld) return;
    ChordRecord& record = reinterpret_cast<ChordRecord*>(bytes(object) + kRecords)[held];
    for (u32 i = 0; i < plan.count; ++i) {
        record.tones[i].delay = plan.tones[i].delay;
        record.tones[i].pitch = plan.tones[i].pitch;
        record.tones[i].velocity = plan.tones[i].velocity;
        record.tones[i].unused[0] = record.tones[i].unused[1] = 0;
    }
    record.count = plan.count;
    record.root = root;
    held = held + 1;  // publish the filled record
}

void player_output(void* object, opxy::player::Display& display) {
    const u32 held = *reinterpret_cast<const volatile u32*>(bytes(object) + kHeld);
    player_activity(reinterpret_cast<const volatile PlayerChord*>(bytes(object) + kRecords), held, display.played);
    display.lowest_key = player_controls(object).lowest_key;
}
}}

using namespace opxy::platform;
using opxy::u32;

// An owned type becomes a stock Maestro without the selection flag, which
// would reset the track's recorded Maestro chord, then takes its own methods.
void* sdk_player_build(u32 track, u32 type, u32) {
    const Player* p = owned(type);
    if (!p) return reinterpret_cast<void*>(kContinue);
    void* object = stock<void*(u32, u32, u32)>(kFactory)(track, kMaestro, 0);
    if (object) {
        __builtin_memset(player_state(object), 0, opxy::player::state_bytes);
        *static_cast<const void* const**>(object) = p->vtable;
    }
    return object;
}

// Stock replaces a loaded type of 1 or at least 5 with Arpeggio.
u32 sdk_player_keep(u32 type) { return owned(type) != nullptr; }

// An owned type uses Maestro's controller. Any other type outside the stock
// table uses Arpeggio's, as a loaded project would.
u32 sdk_player_ui_type() {
    const u32 type = stock<u32()>(kSelectedType)();
    if (type < kStockTypes) return type;
    return owned(type) ? kMaestro : kArpeggio;
}

const char* sdk_player_name(u32 type) {
    const Player* p = owned(type);
    return p ? p->name : nullptr;
}

u32 sdk_player_id(const void* name) {
    for (u32 i = 0; i < sdk_player_count; ++i) {
        const Player* p = sdk_players[i].player();
        if (p && equal(static_cast<const registry::StockString*>(name), p->name)) return p->type();
    }
    return kContinue;
}

// The compositor's substitute for the selected page: the own page of the
// selected track's player while the stock Maestro page is selected, else 0.
u32 sdk_player_substitute(u32 selected) {
    if (selected != kMaestroPage) return 0;
    const u32 track = selected_track();
    for (u32 i = 0; i < sdk_player_count; ++i) {
        const Player* p = sdk_players[i].player();
        if (p && p->page && p->page_ready && player_object(track, p->vtable) && p->page_ready() == 1)
            return static_cast<u32>(reinterpret_cast<Address>(p->page));
    }
    return 0;
}
