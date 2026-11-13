// Stock OS 1.2.1 side of note players. Stock dispatches player types 2..4
// (Arpeggio, Hold, Maestro) from a branch tree and keeps one UI controller per
// type in a five-entry table. A player of the collection is built by the stock
// factory as a Maestro (type 4) and then takes its own method table, so the
// stock step and release play its chords and the Maestro page and controller
// edit its four parameters. Its routing mode is fixed independently of those
// parameters. The hooks (player_hooks.s) ask these functions:
//   factory 8d57ee6c   build an owned type
//   load    8d588d94   keep an owned type a project stored
//   UI      8c0af13a, 8c0af43e  index the controller table with Maestro's entry
//   names   8d5bddd8, 8d5bde60  convert between an owned type and its name
#include "keyboard_octaves.hpp"
#include "players.hpp"
#include "player_activity.hpp"
#include "player_plans.hpp"
#include "player_controls.hpp"
#include "registration.hpp"

extern "C" {
void* sdk_player_build(opxy::u32 track, opxy::u32 type, opxy::u32 flag) __asm__("sdk_player_build");
opxy::u32 sdk_player_keep(opxy::u32 type) __asm__("sdk_player_keep");
opxy::u32 sdk_player_ui_type() __asm__("sdk_player_ui_type");
const char* sdk_player_name(opxy::u32 type) __asm__("sdk_player_name");
opxy::u32 sdk_player_id(const void* name) __asm__("sdk_player_id");
opxy::u32 sdk_player_substitute(opxy::u32 selected) __asm__("sdk_player_substitute");
opxy::u32 sdk_player_turn(opxy::u32 index, const opxy::i32* event) __asm__("sdk_player_turn");
extern opxy::u32 (*sdk_screen_substitute)(opxy::u32) __asm__("sdk_screen_substitute");
// Called by the encoder-click hook (synth_controls.cpp) once a player page is in place.
extern void (*sdk_player_press)(opxy::u32) __asm__("sdk_player_press");
void sdk_player_click(opxy::u32 encoder) __asm__("sdk_player_click");
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

// OS 1.2.1 Maestro object fields: chord records from +0x10c, count at +0x34c.
// A record's tone plays when its delay, decremented every step, reaches zero;
// release sends note-off for every played tone when the record's root matches.
typedef PlayerChord ChordRecord;
const u32 kRecords = 0x10c, kHeld = 0x34c, kMaxHeld = 8;
static_assert(kRecords + kMaxHeld * sizeof(ChordRecord) == kHeld, "Stock Maestro chord array layout");
// Each track's player lives in its own 64 KiB slot; stock objects end below
// 0x480, so the SDK state follows at a fixed offset.
const u32 kStateOffset = 0x800;
static_assert(kStateOffset + opxy::player::state_bytes <= 0x10000, "Player state exceeds the stock slot");
// The GUI-written input (shift row, Shift turns, clicks) follows State.
const u32 kInputOffset = kStateOffset + opxy::player::state_bytes;
static_assert(kInputOffset + sizeof(PlayerInput) <= 0x10000, "Player input exceeds the stock slot");

// Published by the GUI once per compositor frame (sdk_player_substitute):
// whether Shift is held, and the page stock selected.
volatile u32 shift_published, page_selected;

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
volatile PlayerInput* player_input(void* object) {
    return reinterpret_cast<volatile PlayerInput*>(bytes(object) + kInputOffset);
}

void* player_object(u32 track, const void* const* vtable) {
    if (track >= kPlayerSlots) return nullptr;
    void* object = *reinterpret_cast<void* const volatile*>(kPlayers + 4 * track);
    return object && *static_cast<const void* const* const*>(object) == vtable ? object : nullptr;
}

// GUI startup registers every player's page (each player has one). The hooks
// that publish Shift and the selected page each frame and count encoder clicks
// are installed whether or not the page was placed: every player's Controls
// carry Shift and clicks, and the substitute offers a page only once it is ready.
int register_player_page(const Record* record, const Screen* screen) {
    sdk_screen_substitute = sdk_player_substitute;
    sdk_player_press = sdk_player_click;
    return sdk_register_screen(record, screen);
}

opxy::player::Controls player_controls(void* object) {
    const u32 track = *reinterpret_cast<const u32*>(bytes(object) + 4);
    const i32* words = stock<const i32*(u32)>(kParameters)(track);
    opxy::player::Controls controls;
    for (u32 i = 0; i < 4; ++i) controls.value[i] = words[i];
    const i32 octave = track < kTracks ? sdk_track_octaves[track] : 0;
    controls.lowest_key = static_cast<u32>(static_cast<i32>(kLowestKey) + 12 * octave);
    controls.bpm_x10 = audio_bpm_x10();  // the audio clock's tempo word; safe on any role
    controls.shift = shift_published;
    const volatile PlayerInput* input = player_input(object);
    for (u32 i = 0; i < 4; ++i) {
        controls.shift_value[i] = input->shift_value[i];
        controls.shift_turns[i] = input->shift_turns[i];
        controls.clicks[i] = input->clicks[i];
    }
    return controls;
}

void play_plan(void* object, u32 root, const Plan& plan) {
    volatile u32& held = *reinterpret_cast<volatile u32*>(bytes(object) + kHeld);
    if (!plan.count) return;  // silent input must not alter an existing root
    player_store(reinterpret_cast<ChordRecord*>(bytes(object) + kRecords), held,
        root, plan, false, false, [object](u32 note) { stock<void(void*, u32)>(kMaestroRelease)(object, note); });
}

void update_plan(void* object, const opxy::player::Update& update) {
    if (update.mode != opxy::player::Replace && update.mode != opxy::player::Add) return;
    volatile u32& held = *reinterpret_cast<volatile u32*>(bytes(object) + kHeld);
    player_store(reinterpret_cast<ChordRecord*>(bytes(object) + kRecords), held,
        update.root, update.plan, true, update.mode == opxy::player::Add,
        [object](u32 note) { stock<void(void*, u32)>(kMaestroRelease)(object, note); });
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
        volatile PlayerInput* input = player_input(object);
        for (u32 i = 0; i < 4; ++i) {
            input->shift_value[i] = p->shift && i < p->shift->count ? p->shift->defaults[i] : 0;
            input->shift_turns[i] = 0;
            input->clicks[i] = 0;
        }
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

// Maestro's GUI turn handler supplies (controller, index, event). The turn
// shim preserves it for stock unless an owned player takes the turn. All
// owned turns bypass Maestro's post-edit hold-mode callback: parameter 3 is
// a machine control, never stock's hold-mode switch.
// event[1] is signed detents; event[0] is continuous motion. Stock's setter
// updates the live parameter and the saved pattern, including dirty state.
//
// With Shift held, a player with a shift row takes every turn that reaches
// here (index 0..3, or 4..7 should stock pass Shift's row so): the turn's
// encoder edits its shift control and adds its detents to shift_turns, in the
// input beside State. Whether stock's Maestro controller reaches this hook
// while Shift is held has not been observed on a device. Without a shift row,
// or without Shift, turns are handled as before.
u32 sdk_player_turn(u32 index, const opxy::i32* event) {
    if (index >= 8 || !event) return 0;
    const Player* p = owned(stock<u32()>(kSelectedType)());
    if (!p) return 0;
    const u32 track = selected_track();
    if (track >= kTracks) return 0;
    // The shift row first: an absent main slot does not block Shift on its encoder.
    if (p->shift && shift_held()) {
        void* object = player_object(track, p->vtable);
        if (object) {
            const u32 k = index & 3;
            volatile PlayerInput* input = player_input(object);
            input->shift_turns[k] = static_cast<opxy::i32>(static_cast<u32>(input->shift_turns[k]) +
                                                           static_cast<u32>(event[1]));
            if (k < p->shift->count && event[1])
                input->shift_value[k] = player_enum_turn(input->shift_value[k], p->shift->choices[k], event[1]);
            return 1;
        }
    }
    if (index >= 4) return 0;  // steps[] has four entries
    if (p->steps[index] == 1) return 1;  // an absent slot: the encoder turns nothing
    const u32 inherited[4] = {128, 8, 0, 4}; // stock's doubled buckets
    const u32 choices = p->steps[index] >= 2 ? p->steps[index] : inherited[index];
    if (!choices) {
        stock<void(u32, u32, const opxy::i32*)>(0x8d586538)(track, index, event);
    } else if (event[1]) {
        const opxy::i32 value = stock<const opxy::i32*(u32)>(kParameters)(track)[index];
        stock<void(u32, u32, opxy::i32)>(0x8d586474)(track, index,
            player_enum_turn(value, choices, event[1]));
    }
    return 1;
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
    // The compositor calls once per GUI frame, even when the player page is
    // hidden. Sequenced players on unselected tracks need fresh octaves too,
    // and every player the Shift state (Controls.shift).
    refresh_keyboard_octaves();
    shift_published = shift_held() ? 1 : 0;
    page_selected = selected;
    if (selected != kMaestroPage) return 0;
    const u32 track = selected_track();
    for (u32 i = 0; i < sdk_player_count; ++i) {
        const Player* p = sdk_players[i].player();
        if (p && p->page && p->page_ready && player_object(track, p->vtable) && p->page_ready() == 1)
            return static_cast<u32>(reinterpret_cast<Address>(p->page));
    }
    return 0;
}

// The encoder-click hook (synth_controls.cpp): a click of encoder 0..3 (stock
// records only clicks released without a turn and without Shift), counted for
// the selected track's player while the last frame selected the Maestro page,
// which its own page replaces. Stock still receives the click.
void sdk_player_click(u32 encoder) {
    if (encoder >= 4 || page_selected != kMaestroPage) return;
    const Player* p = owned(stock<u32()>(kSelectedType)());
    const u32 track = selected_track();
    if (!p || track >= kTracks) return;
    if (void* object = player_object(track, p->vtable)) {
        volatile PlayerInput* input = player_input(object);
        input->clicks[encoder] = static_cast<opxy::u8>(input->clicks[encoder] + 1);
    }
}
