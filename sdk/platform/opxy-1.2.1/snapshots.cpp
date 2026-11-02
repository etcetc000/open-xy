// The SDK's snapshots (opxy/snapshots.hpp): part of the control service.
//
// Taking one reads, for each instrument track, its engine (8d58781c) and the 58
// base values stock keeps for the track's current pattern at track data + 0x3864
// (the block 8d587708 returns), the same words the encoder edit's setter writes
// and a saved project keeps. It writes nothing of stock's. Overlays live only in
// the live parameters (g_project + 0x4080), so a snapshot never holds one.
//
// Snapshots are taken on the controller path (an extension's queued control) and
// read on the GUI tick that follows it in the same main loop. They are saved and
// loaded with the rest of the control service's data (track_control_project.cpp):
// encoded here, written and read there.
#include <opxy/snapshots.hpp>
#include "snapshots.hpp"
#include "track_control.hpp"
#include "stock.hpp"

namespace {
using opxy::i32;
using opxy::u32;
using opxy::u8;
using namespace opxy::platform;
namespace control = opxy::control;
namespace snapshots = opxy::snapshots;

snapshots::Snapshot store[snapshots::slots];
u32 generation;

// The menu: open while its key is held; the selection stays between openings.
struct Menu { bool open, used, strip_now; u32 selected; i32 held_ms, knob; u32 active, flash, flash_slot; i32 flash_ms; };
Menu menu = {false, false, false, 0, 0, 0, opxy::platform::kNoSlot, 0, 0, 0};

void flash(u32 what, u32 slot) {
    menu.flash = what;
    menu.flash_slot = slot;
    menu.flash_ms = opxy::platform::kMenuFlashMs;
}
// Whether the tracks hold `slot` now: every track still holding the slot's
// engine holds its values, and at least one does.
bool holds(u32 slot) {
    const snapshots::Snapshot& s = store[slot];
    if (!s.taken) return false;
    bool any = false;
    for (u32 t = 0; t < control::tracks; ++t) {
        if (!track_engine(t) || s.engines[t] != track_engine(t)) continue;
        any = true;
        const u32 block = stock<u32(u32)>(0x8d587708)(t) + 0x3864;
        for (u32 p = 0; p < control::track_parameters; ++p)
            if (static_cast<i32>(word(block + p * 4)) != s.values[t][p]) return false;
    }
    return any;
}
const u32 kProject = 0x86bd027c;

i32 base(u32 track, u32 index) {
    return static_cast<i32>(word(stock<u32(u32)>(0x8d587708)(track) + 0x3864 + index * 4));
}

// A value's encoding, at most five bytes, 32-bit arithmetic only: Q16-aligned
// words (stock's controls mostly are) keep their upper half, any other word all
// of it. The first byte holds that choice (bit 0: 1 for the whole word), the
// payload's low six bits (1-6) and a continuation bit (7); each further byte
// seven more bits, low first, and a continuation bit.
u32 put_value(u8* out, u32 at, u32 capacity, u32 word) {
    const bool whole = (word & 0xffff) != 0;
    u32 payload = whole ? word : word >> 16;
    if (at >= capacity) return capacity + 1;
    u8 first = static_cast<u8>((whole ? 1 : 0) | (payload & 0x3f) << 1);
    payload >>= 6;
    out[at++] = static_cast<u8>(first | (payload ? 0x80 : 0));
    while (payload) {
        if (at >= capacity) return capacity + 1;
        const u8 low = static_cast<u8>(payload & 0x7f);
        payload >>= 7;
        out[at++] = static_cast<u8>(low | (payload ? 0x80 : 0));
    }
    return at;
}
bool get_value(const u8* data, u32 bytes, u32& at, u32& word) {
    if (at >= bytes) return false;
    u8 b = data[at++];
    const bool whole = b & 1;
    u32 payload = b >> 1 & 0x3f;
    for (u32 shift = 6; b & 0x80; shift += 7) {
        if (at >= bytes || shift > 27) return false;
        b = data[at++];
        // The fifth byte may hold only the word's top five bits.
        if (shift == 27 && (b & 0x60)) return false;
        payload |= static_cast<u32>(b & 0x7f) << shift;
    }
    if (!whole && payload > 0xffff) return false;
    word = whole ? payload : payload << 16;
    return true;
}
}

const void* sdk_snapshot(u32 slot) {
    return slot < snapshots::slots && store[slot].taken ? &store[slot] : nullptr;
}
u32 sdk_snapshot_engine(u32 track) { return track < control::tracks ? track_engine(track) : 0; }
u32 sdk_snapshot_generation() { return generation; }
bool sdk_snapshot_take(u32 slot) { return opxy::platform::snapshot_take(slot); }

bool opxy::platform::snapshot_take(u32 slot) {
    if (slot >= snapshots::slots) return false;
    snapshots::Snapshot& s = store[slot];
    for (u32 t = 0; t < control::tracks; ++t) {
        s.engines[t] = track_engine(t);
        for (u32 p = 0; p < control::track_parameters; ++p) s.values[t][p] = base(t, p);
    }
    s.taken = 1;
    ++generation;
    return true;
}

bool opxy::platform::snapshot_load(u32 slot) {
    if (slot >= snapshots::slots || !store[slot].taken) return false;
    const snapshots::Snapshot& s = store[slot];
    for (u32 t = 0; t < control::tracks; ++t) {
        if (!track_engine(t) || s.engines[t] != track_engine(t)) continue;
        const u32 block = stock<u32(u32)>(0x8d587708)(t);
        for (u32 p = 0; p < control::track_parameters; ++p)
            if (base(t, p) != s.values[t][p]) {
                // The stored value first: stock's setter stores it only on synth
                // tracks. Then the setter an encoder edit ends in, which publishes.
                *at<i32>(block + 0x3864 + p * 4) = s.values[t][p];
                stock<void(u32, u32, u32, i32)>(0x8d5acf28)(kProject, t, p, s.values[t][p]);
                // Beneath an overlay, the loaded value is what its release restores.
                control_stock_set(t, p, s.values[t][p]);
            }
    }
    return true;
}

u32 opxy::platform::snapshot_fits(u32 slot) {
    if (slot >= snapshots::slots || !store[slot].taken) return 0;
    u32 fits = 0;
    for (u32 t = 0; t < control::tracks; ++t) fits += track_engine(t) && store[slot].engines[t] == track_engine(t);
    return fits;
}

opxy::platform::MenuView opxy::platform::snapshot_menu_view() {
    MenuView view = {menu.open, menu.open && (menu.strip_now || menu.held_ms >= kMenuStripMs), menu.selected, 0, 0,
                     kNoSlot, menu.active, menu.flash_ms > 0 ? menu.flash : kFlashNone, menu.flash_slot};
    for (u32 i = 0; i < snapshots::slots; ++i) view.taken |= store[i].taken ? 1u << i : 0;
    view.fits = snapshot_fits(menu.selected);
    // The slot last saved or loaded first, so two equal slots show the one in use.
    if (menu.active < snapshots::slots && holds(menu.active)) view.current = menu.active;
    for (u32 i = 0; view.current == kNoSlot && i < snapshots::slots; ++i)
        if (holds(i)) view.current = i;
    return view;
}

bool opxy::platform::snapshot_menu_open() { return menu.open; }

void opxy::platform::snapshot_menu_frame(i32 elapsed_ms) {
    if (!menu.open || elapsed_ms <= 0) return;
    if (menu.held_ms < kMenuStripMs) menu.held_ms += elapsed_ms;
    if (menu.flash_ms > 0) menu.flash_ms -= elapsed_ms;
}

u32 sdk_snapshot_menu(u32 op, i32 arg) {
    switch (op) {
    case kMenuOpen:
        // arg 1: opened by a gesture that leaves it open, so the bottom row shows at once.
        menu.open = true; menu.used = false; menu.strip_now = arg != 0; menu.held_ms = 0;
        menu.knob = 0; menu.flash_ms = 0;
        return 1;
    case kMenuClose: {
        const bool used = menu.used || menu.held_ms >= kMenuStripMs;
        menu.open = false;
        return used;
    }
    case kMenuHeld:
        return menu.open && (menu.used || menu.held_ms >= kMenuStripMs);
    case kMenuSelect:
        if (menu.open && arg >= 0 && static_cast<u32>(arg) < snapshots::slots) { menu.selected = static_cast<u32>(arg); menu.used = true; }
        return menu.open;
    case kMenuStep:
        // Raw encoder counts: one step per detent, the remainder kept; turning
        // back starts over.
        if (menu.open && arg) {
            if ((arg > 0) != (menu.knob > 0) && menu.knob) menu.knob = 0;
            menu.knob += arg;
            while (menu.knob >= kMenuKnobCounts || menu.knob <= -kMenuKnobCounts) {
                const bool up = menu.knob > 0;
                menu.knob += up ? -kMenuKnobCounts : kMenuKnobCounts;
                menu.selected = (menu.selected + (up ? 1 : snapshots::slots - 1)) % snapshots::slots;
            }
            menu.used = true;
        }
        return menu.open;
    case kMenuAction:
        // The bottom row acts once it shows, as on the bar menu.
        if (!menu.open || !(menu.strip_now || menu.held_ms >= kMenuStripMs)) return 0;
        menu.used = true;
        if (arg == kMenuSave && snapshot_take(menu.selected)) {
            menu.active = menu.selected;
            flash(kFlashSaved, menu.selected);
        }
        // A slot no track can take back loads nothing, and says so (fits 0/8).
        if (arg == kMenuLoad && snapshot_fits(menu.selected) && snapshot_load(menu.selected)) {
            menu.active = menu.selected;
            flash(kFlashLoaded, menu.selected);
        }
        if (arg == kMenuClear && store[menu.selected].taken) {
            store[menu.selected].taken = 0;
            ++generation;
            if (menu.active == menu.selected) menu.active = kNoSlot;
            flash(kFlashCleared, menu.selected);
        }
        if (arg == kMenuClearAll) {
            snapshots_clear();
            flash(kFlashClearedAll, kNoSlot);
        }
        return 1;
    }
    return 0;
}

void opxy::platform::snapshots_clear() {
    menu.active = kNoSlot;
    for (u32 i = 0; i < snapshots::slots; ++i) store[i].taken = 0;
    ++generation;
}

// Version 1: a version byte, two bytes of taken slots (little-endian, bit n for
// slot n), then for
// each taken slot in order its eight engines (little-endian words) and its
// tracks' values in track order. Returns the byte count, or 0 when no slot is
// taken or the encoding does not fit.
u32 opxy::platform::snapshots_encode(u8* out, u32 capacity) {
    u32 taken = 0;
    for (u32 i = 0; i < snapshots::slots; ++i) taken |= store[i].taken ? 1u << i : 0;
    if (!taken || capacity < 3) return 0;
    out[0] = 1;
    out[1] = static_cast<u8>(taken);
    out[2] = static_cast<u8>(taken >> 8);
    u32 at = 3;
    for (u32 i = 0; i < snapshots::slots; ++i) {
        if (!store[i].taken) continue;
        const snapshots::Snapshot& s = store[i];
        for (u32 t = 0; t < control::tracks; ++t)
            for (u32 b = 0; b < 4; ++b) {
                if (at >= capacity) return 0;
                out[at++] = static_cast<u8>(s.engines[t] >> (8 * b));
            }
        for (u32 t = 0; t < control::tracks; ++t)
            for (u32 p = 0; p < control::track_parameters; ++p) {
                at = put_value(out, at, capacity, static_cast<u32>(s.values[t][p]));
                if (at > capacity) return 0;
            }
    }
    return at;
}

// Replaces every slot: with what `data` holds, or with nothing when it is empty
// or malformed (a project without snapshots, or one this version can't read).
void opxy::platform::snapshots_decode(const u8* data, u32 bytes) {
    static snapshots::Snapshot read[snapshots::slots];
    bool ok = bytes >= 3 && data[0] == 1 && !(data[2] >> (snapshots::slots - 8));
    const u32 taken = ok ? data[1] | data[2] << 8 : 0;
    u32 at = 3;
    for (u32 i = 0; ok && i < snapshots::slots; ++i) {
        read[i].taken = 0;
        if (!(taken & (1u << i))) continue;
        snapshots::Snapshot& s = read[i];
        for (u32 t = 0; ok && t < control::tracks; ++t) {
            if (at + 4 > bytes) { ok = false; break; }
            s.engines[t] = data[at] | data[at + 1] << 8 | data[at + 2] << 16 | static_cast<u32>(data[at + 3]) << 24;
            at += 4;
        }
        for (u32 t = 0; ok && t < control::tracks; ++t)
            for (u32 p = 0; ok && p < control::track_parameters; ++p) {
                u32 word;
                if (!get_value(data, bytes, at, word)) { ok = false; break; }
                s.values[t][p] = static_cast<i32>(word);
            }
        s.taken = 1;
    }
    ok = ok && at == bytes;
    menu.active = kNoSlot;  // another project: nothing saved or loaded yet
    for (u32 i = 0; i < snapshots::slots; ++i) {
        store[i].taken = 0;
        if (ok && read[i].taken) store[i] = read[i];
    }
    ++generation;
}
