#include "extension_runtime.hpp"

namespace opxy { namespace platform {
namespace {
extensions::Context current;
const ExtensionEntry* entries;
const ExtensionRegistry* registry;
u32 entry_count;
bool initialized, dispatching;
char notification[32];

// The actions one callback may take, and what it has done with them: one stock
// undo transaction and at most one notification per callback.
struct Transaction { u32 granted; bool attempted, notified, snapped; };
Transaction transaction;

bool same_text(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
u32 bits(u32 count) { return count >= 32 ? ~0u : (1u << count) - 1; }

// The collection must agree with the registry, whatever the build checked:
// claims exclusive and known, every action backed by a claim its holder owns.
bool ready() {
    if (initialized) return entries != nullptr;
    initialized = true;
    u32 count = 0;
    const ExtensionEntry* list = extension_collection(count);
    const ExtensionRegistry& known = extension_registry();
    if (!list || count > 16 || known.claim_count > 32 || known.action_count > 32) return false;
    u32 claimed = 0;
    for (u32 i = 0; i < count; ++i) {
        const ExtensionEntry& entry = list[i];
        if (!entry.id || !*entry.id || !entry.hooks || (entry.claims & claimed) ||
            (entry.claims & ~bits(known.claim_count)) || (entry.actions & ~bits(known.action_count)) ||
            (entry.claims && !entry.hooks->queued_control)) return false;
        for (u32 a = 0; a < known.action_count; ++a) {
            if (!(entry.actions & (1u << a))) continue;
            const u32 needs = known.action_claims[a] ? known.action_claims[a] : bits(known.claim_count);
            if (!(entry.claims & needs)) return false;
        }
        claimed |= entry.claims;
        for (u32 j = 0; j < i; ++j)
            if (same_text(entry.id, list[j].id)) return false;
    }
    entries = list;
    registry = &known;
    entry_count = count;
    return true;
}

// Keyboard keys a chord took from stock (bit key - 26), and the entry each went to.
const u32 kKeyboardFirst = extensions::keys::keyboard_first, kKeyboardKeys = 24;
u32 withheld;
u32 taker[kKeyboardKeys];

// Actions an entry's fired claims grant.
u32 grants(const ExtensionEntry& entry, u32 fired) {
    u32 result = 0;
    for (u32 a = 0; fired && a < registry->action_count; ++a) {
        const u32 needs = registry->action_claims[a] ? registry->action_claims[a] : entry.claims;
        if ((entry.actions & (1u << a)) && (fired & needs)) result |= 1u << a;
    }
    return result;
}
// Actions granted while a claimed key fires and stock accepted it.
u32 granted(const ExtensionEntry& entry, const extensions::Control& control) {
    if (!control.accepted) return 0;
    u32 fired = 0;
    for (u32 c = 0; c < registry->claim_count; ++c)
        if ((entry.claims & (1u << c)) && registry->claim_keys[c] == control.key) fired |= 1u << c;
    return grants(entry, fired);
}

// The caller's context must still be current: a stale one means the project,
// pattern or selection moved on since the event it acts on.
u32 write_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count,
                bool replace) {
    if (!(transaction.granted & action_edit_notes) || transaction.attempted || !notes || !count ||
        count > 120 || track >= 8) return 0;
    bool playing = false;
    const RecordingContext now = extension_context(playing);
    if (!(now == expected.recording) || playing != expected.playing ||
        !(now == current.recording) || playing != current.playing) return 0;
    const TrackPattern target = extension_pattern(track);
    if (!target.editable || !target.length || target.length >= (1u << 23)) return 0;
    for (u32 i = 0; i < count; ++i)
        if (notes[i].pitch > 127 || !notes[i].velocity || notes[i].velocity > 127 ||
            notes[i].position < -240 || notes[i].position >= static_cast<i32>(target.length) ||
            notes[i].length > target.length) return 0;
    transaction.attempted = true; // One undo transaction per callback, including failures.
    return extension_write(track, notes, count, replace);
}
void notify(const char* text) {
    if (!(transaction.granted & action_notify) || transaction.notified || !text) return;
    u32 length = 0;
    while (length < 32 && text[length]) ++length;
    if (!length || length == 32) return;
    for (u32 i = 0; i <= length; ++i) notification[i] = text[i];
    transaction.notified = true;
    extension_notify(notification);
}
bool take_snapshot(u32 slot) {
    if (!(transaction.granted & action_snapshot) || transaction.snapped || slot >= 10) return false;
    transaction.snapped = true;
    return sdk_snapshot_take(slot);
}
u32 insert_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, false);
}
u32 replace_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, true);
}
const extensions::Actions actions = {insert_notes, notify, replace_notes, take_snapshot};
}

void extensions_sync() {
    if (dispatching || !ready()) return;
    current.recording = extension_context(current.playing);
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->context) entries[i].hooks->context(current);
    dispatching = false;
}
void extensions_input_activity() {
    if (dispatching || !ready()) return;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->input_activity) entries[i].hooks->input_activity();
    dispatching = false;
}
void extensions_recordable_note_on(u32 track, u32 pitch, u32 velocity) {
    if (dispatching) return;
    extensions_sync();
    if (!entries || track != current.recording.track || !current.recording.recordable ||
        !current.playing || pitch > 127 || velocity > 127) return;
    const i32 radius = extension_collision_radius(track);
    if (radius < 0) return;
    const extensions::LiveNote note = {track, pitch, velocity, extension_position(track),
                                       static_cast<u32>(radius)};
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->recordable_note_on) entries[i].hooks->recordable_note_on(current, note);
    dispatching = false;
}
void extensions_selected_note_off(u32 track, u32 pitch) {
    if (dispatching) return;
    extensions_sync();
    extensions_input_activity();
    if (!entries || track != current.recording.track) return;
    const extensions::NoteOff note = {track, pitch};
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->selected_note_off) entries[i].hooks->selected_note_off(current, note);
    dispatching = false;
}
void extensions_recording_tick(u32 delta) {
    if (dispatching) return;
    extensions_sync();
    if (!entries || !current.recording.recordable || !current.playing) return;
    const bool discontinuity = delta > 0x007fffffu;
    const extensions::Tick tick = {discontinuity ? 0 : extension_scaled_delta(current.recording.track, delta),
                                   discontinuity};
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->recording_tick) entries[i].hooks->recording_tick(current, tick);
    dispatching = false;
}
namespace {
// The snapshot menu, as stock's bar key works. A claim with a partner key opens
// it when the two are pressed together (either one first, waiting for the other:
// pressed alone, it reaches stock when released). Held, the menu is momentary
// and closes on release; tapped, it stays open until the pair again or any other
// key, which then reaches stock as usual. A claim without a partner opens it
// while its key is held. The keys it takes, whose releases it takes too (bit key).
const u32 kMenuKeys = 64, kM1 = 6;
u32 menu_withheld[kMenuKeys / 32];
bool menu_open;
bool menu_holding;  // pair mode: the pair that opened the menu is still down
u32 menu_key;  // hold mode: the key holding the menu open
u32 pending;   // pair mode: the first key of the pair, waiting for the other

bool menu_taken(u32 key) { return key < kMenuKeys && (menu_withheld[key / 32] >> (key % 32) & 1); }
void close_menu() {
    if (menu_open) sdk_snapshot_menu(kMenuClose, 0);
    menu_open = menu_holding = false;
}

// The claim that opens the menu: a black-key chord claim whose owner holds the
// snapshot action. False for none.
bool menu_claim(u32& key, u32& partner) {
    if (!ready() || !registry->claim_chords) return false;
    for (u32 c = 0; c < registry->claim_count; ++c) {
        if (registry->claim_chords[c] != chord_black_keys) continue;
        for (u32 i = 0; i < entry_count; ++i)
            if ((entries[i].claims & (1u << c)) && (entries[i].actions & action_snapshot)) {
                key = registry->claim_keys[c];
                partner = registry->claim_partners ? registry->claim_partners[c] : 0;
                return key && key < kMenuKeys && partner < kMenuKeys;
            }
    }
    return false;
}

bool chord_input(u32 key, bool pressed, extensions::Time time);
}

u32 extensions_key_input(u32 key, bool pressed, extensions::Time time) {
    if (dispatching) return key_stock;
    if (key < kMenuKeys) {
        u32& taken = menu_withheld[key / 32];
        const u32 bit = 1u << (key % 32);
        if (taken & bit) {
            if (pressed) return key_taken;  // a repeat of a key already taken
            taken &= ~bit;
            if (key == pending) {
                pending = 0;
                // The pair's key alone: stock's after all, and like any other key
                // it closes a menu left open.
                if (menu_open && !menu_holding) close_menu();
                return key_tap;
            }
            if (key == menu_key) {
                menu_key = 0;
                menu_open = false;
                return sdk_snapshot_menu(kMenuClose, 0) ? key_taken : key_tap;
            }
            // The pair that opened the menu, both let go: a hold closes it, a tap leaves it open.
            u32 claim = 0, partner = 0;
            if (menu_holding && menu_claim(claim, partner) && (key == claim || key == partner) &&
                !menu_taken(key == claim ? partner : claim)) {
                menu_holding = false;
                if (sdk_snapshot_menu(kMenuHeld, 0)) close_menu();
            }
            return key_taken;
        }
        if (pressed) {
            u32 claim = 0, partner = 0;
            const bool menu = menu_claim(claim, partner);
            if (menu && partner && (key == claim || key == partner)) {
                const u32 other = key == claim ? partner : claim;
                if (pending == other) {
                    pending = 0;
                    if (menu_open) {
                        close_menu();
                    } else {
                        menu_open = menu_holding = sdk_snapshot_menu(kMenuOpen, 1) != 0;
                    }
                    taken |= bit;
                    return key_taken;
                }
                if (!pending) {
                    pending = key;
                    taken |= bit;
                    return key_taken;
                }
            } else if (menu && !partner && key == claim && !menu_open) {
                if (sdk_snapshot_menu(kMenuOpen, 0)) {
                    menu_key = key;
                    menu_open = true;
                    taken |= bit;
                    return key_taken;
                }
            } else if (menu_open) {
                const i32 black = extensions::keys::black_index(key);
                if (black >= 0) sdk_snapshot_menu(kMenuSelect, black);
                else if (key >= kM1 && key < kM1 + 4) sdk_snapshot_menu(kMenuAction, static_cast<i32>(key - kM1));
                else {
                    // Any other key leaves the menu and does what it does.
                    if (!menu_key) close_menu();
                    return key_stock;
                }
                taken |= bit;
                return key_taken;
            }
        }
    }
    return chord_input(key, pressed, time) ? key_taken : key_stock;
}

bool extensions_knob_input(u32 knob, i32 delta) {
    if (!menu_open || dispatching || knob != 0) return false;
    sdk_snapshot_menu(kMenuStep, delta);
    return true;
}

namespace {
bool chord_input(u32 key, bool pressed, extensions::Time time) {
    if (dispatching || key < kKeyboardFirst || key >= kKeyboardFirst + kKeyboardKeys) return false;
    const u32 bit = 1u << (key - kKeyboardFirst);
    u32 index = 0, fired = 0;
    if (pressed) {
        // A black key while a chord's claimed key is held: the claim's owner takes it.
        if (withheld & bit) return true;  // a repeat of a key already taken
        if (!ready() || !registry->claim_chords || extensions::keys::black_index(key) < 0) return false;
        for (u32 c = 0; c < registry->claim_count && !fired; ++c) {
            if (registry->claim_chords[c] != chord_black_keys || !extension_key_held(registry->claim_keys[c]))
                continue;
            for (u32 i = 0; i < entry_count && !fired; ++i)
                if (entries[i].claims & (1u << c)) { index = i; fired = 1u << c; }
        }
        if (!fired) return false;
        withheld |= bit;
        taker[key - kKeyboardFirst] = index;
    } else {
        // Its release goes where its press went, held key or not.
        if (!(withheld & bit)) return false;
        withheld &= ~bit;
        index = taker[key - kKeyboardFirst];
        if (!entries || index >= entry_count) return true;
        for (u32 c = 0; c < registry->claim_count; ++c)
            if ((entries[index].claims & (1u << c)) && registry->claim_chords[c] == chord_black_keys) fired |= 1u << c;
    }
    extensions_sync();
    const extensions::Control control = {key, pressed, true, false, time};
    dispatching = true;
    transaction = {grants(entries[index], fired), false, false, false};
    if (entries[index].hooks->queued_control) entries[index].hooks->queued_control(current, control, actions);
    transaction = {0, false, false, false};
    dispatching = false;
    return true;
}
}
void extensions_queued_control(const extensions::Control& control) {
    if (dispatching) return;
    extensions_sync();
    if (!entries) return;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i) {
        transaction = {granted(entries[i], control), false, false, false};
        if (entries[i].hooks->queued_control) entries[i].hooks->queued_control(current, control, actions);
        transaction = {0, false, false, false};
    }
    dispatching = false;
}
}}
