#include "extension_runtime.hpp"
#include <opxy/snapshots.hpp>

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
            (entry.claims & ~bits(known.claim_count)) || (entry.actions & ~bits(known.action_count))) return false;
        // A layer claim reaches its owner's layer hook, any other its queued control.
        for (u32 c = 0; c < known.claim_count; ++c) {
            if (!(entry.claims & (1u << c))) continue;
            const bool layer = known.claim_layers && known.claim_layers[c];
            if (layer ? !entry.hooks->layer : !entry.hooks->queued_control) return false;
        }
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
// Actions granted while a claimed key fires and stock accepted it. A pair's or a
// layer's key alone is not its gesture: those grant through it (chord, layer).
u32 granted(const ExtensionEntry& entry, const extensions::Control& control) {
    if (!control.accepted) return 0;
    u32 fired = 0;
    for (u32 c = 0; c < registry->claim_count; ++c)
        if ((entry.claims & (1u << c)) && registry->claim_keys[c] == control.key &&
            !(registry->claim_partners && registry->claim_partners[c]) &&
            !(registry->claim_layers && registry->claim_layers[c]))
            fired |= 1u << c;
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
// One snapshot write (take, load or clear) per callback.
// A refused one (an empty slot to load, nothing to clear) does not count.
bool may_write() { return (transaction.granted & action_snapshot) && !transaction.snapped; }
bool wrote(bool done) { transaction.snapped = transaction.snapped || done; return done; }
bool take_snapshot(u32 slot) { return slot < snapshots::slots && may_write() && wrote(sdk_snapshot_take(slot)); }
bool load_snapshot(u32 slot) { return slot < snapshots::slots && may_write() && wrote(sdk_snapshot_load(slot)); }
u32 clear_snapshots(u32 mask) {
    if (!mask || (mask >> snapshots::slots) || !may_write()) return 0;
    const u32 cleared = sdk_snapshot_clear(mask);
    wrote(cleared != 0);
    return cleared;
}
u32 insert_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, false);
}
u32 replace_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, true);
}
const extensions::Actions actions = {insert_notes, notify, replace_notes, take_snapshot, load_snapshot,
                                     clear_snapshots};
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
// The open layer (extension.hpp, Layer): its entry and claim, or none. A pair
// claim's first key waits for the other (pending); pressed alone, it reaches
// stock when released. Keys taken from stock, their releases with them (bit key).
const u32 kKeys = 64, kNone = ~0u;
u32 key_withheld[kKeys / 32];
u32 layer_entry = kNone, layer_claim;
bool layer_holding, layer_latched, layer_used;
u32 layer_held_ms;
u32 layer_openings;  // counts openings, so a reopening starts with every node hidden
u32 layer_key = kNone;  // a single-key claim's key, held since it opened a layer
bool layer_single;      // the open layer is that key's
u32 layer_took[kKeys / 32];  // keys the open layer took: only it hears their release
u32 pending = kNone;

bool taken_key(u32 key) { return key < kKeys && (key_withheld[key / 32] >> (key % 32) & 1); }

// The layer claim `key` belongs to (its key or its partner), and its owner.
bool layer_of(u32 key, u32& claim, u32& partner, u32& entry) {
    if (!ready() || !registry->claim_layers) return false;
    for (u32 c = 0; c < registry->claim_count; ++c) {
        if (!registry->claim_layers[c]) continue;
        const u32 other = registry->claim_partners ? registry->claim_partners[c] : 0;
        if (key != registry->claim_keys[c] && (!other || key != other)) continue;
        for (u32 i = 0; i < entry_count; ++i)
            if ((entries[i].claims & (1u << c)) && entries[i].hooks->layer) {
                claim = c;
                partner = other;
                entry = i;
                return true;
            }
    }
    return false;
}
bool layer_send(extensions::Layer::Kind kind, u32 key = 0, bool pressed = false, i32 delta = 0, u32 elapsed = 0) {
    if (layer_entry == kNone || dispatching) return false;
    extensions_sync();
    const ExtensionEntry& entry = entries[layer_entry];
    const extensions::Layer event = {kind, key, pressed, delta, elapsed, layer_latched,
                                     layer_holding ? layer_held_ms : 0};
    dispatching = true;
    transaction = {grants(entry, 1u << layer_claim), false, false, false};
    const bool took = entry.hooks->layer(current, event, actions);
    transaction = {0, false, false, false};
    dispatching = false;
    return took;
}
void open_layer(u32 claim, u32 entry) {
    layer_entry = entry;
    layer_claim = claim;
    layer_holding = true;
    layer_latched = layer_used = layer_single = false;
    layer_held_ms = 0;
    layer_took[0] = layer_took[1] = 0;
    ++layer_openings;
    layer_send(extensions::Layer::open);
}
void close_layer() {
    if (layer_entry == kNone) return;
    layer_send(extensions::Layer::close);
    layer_entry = kNone;
    layer_holding = layer_latched = layer_single = false;
}
// A held gesture let go: a hold (a key taken, or tap_ms) closes, a tap latches.
bool tapped() { return !layer_used && layer_held_ms < extensions::tap_ms; }

bool chord_input(u32 key, bool pressed, extensions::Time time);
}

u32 extensions_key_input(u32 key, bool pressed, extensions::Time time) {
    if (dispatching) return key_stock;
    if (key < kKeys) {
        u32& taken = key_withheld[key / 32];
        const u32 bit = 1u << (key % 32);
        if (taken & bit) {
            if (pressed) return key_taken;  // a repeat of a key already taken
            taken &= ~bit;
            if (key == pending) {
                // The pair's key alone: stock's after all, and like any other key
                // it closes a latched layer.
                pending = kNone;
                if (layer_latched) close_layer();
                return key_tap;
            }
            if (key == layer_key) {
                layer_key = kNone;
                // Its layer, if another gesture has not replaced it: a tap reaches stock.
                if (!layer_single) return key_taken;
                const bool tap = tapped();
                close_layer();
                return tap ? key_tap : key_taken;
            }
            u32 claim = 0, partner = 0, entry = 0;
            if (layer_holding && layer_of(key, claim, partner, entry) && claim == layer_claim) {
                // The pair let go (both keys): a hold closes it, a tap leaves it open.
                if (!taken_key(key == registry->claim_keys[claim] ? partner : registry->claim_keys[claim])) {
                    layer_holding = false;
                    if (tapped()) layer_latched = true;
                    else close_layer();
                }
                return key_taken;
            }
            if (layer_took[key / 32] & bit) {
                layer_took[key / 32] &= ~bit;
                layer_send(extensions::Layer::keypress, key, false);
            }
            return key_taken;
        }
        if (pressed) {
            u32 claim = 0, partner = 0, entry = 0;
            if (layer_of(key, claim, partner, entry)) {
                const u32 other = key == registry->claim_keys[claim] ? partner : registry->claim_keys[claim];
                if (partner) {
                    // A pair key let go and pressed again while the other holds the
                    // layer open: still the hold.
                    if (layer_holding && layer_claim == claim && taken_key(other)) {
                        taken |= bit;
                        return key_taken;
                    }
                    if (pending == other) {
                        pending = kNone;
                        const bool again = layer_entry != kNone && layer_claim == claim;
                        close_layer();
                        if (!again) open_layer(claim, entry);
                        taken |= bit;
                        return key_taken;
                    }
                    if (pending == kNone) {
                        pending = key;
                        taken |= bit;
                        return key_taken;
                    }
                } else if (layer_entry == kNone) {
                    open_layer(claim, entry);
                    layer_key = key;
                    layer_single = true;
                    taken |= bit;
                    return key_taken;
                }
            }
            if (layer_entry != kNone) {
                if (layer_send(extensions::Layer::keypress, key, true)) {
                    if (layer_holding) layer_used = true;
                    taken |= bit;
                    layer_took[key / 32] |= bit;
                    return key_taken;
                }
                // A key the owner leaves: stock's, and it ends a latched layer.
                if (layer_latched) close_layer();
                return key_stock;
            }
        }
    }
    return chord_input(key, pressed, time) ? key_taken : key_stock;
}

bool extensions_knob_input(u32 knob, i32 delta) {
    if (layer_entry == kNone || dispatching) return false;
    if (!layer_send(extensions::Layer::turn, knob, false, delta)) {
        // A turn the owner leaves: stock's, and like a key it ends a latched layer.
        if (layer_latched) close_layer();
        return false;
    }
    if (layer_holding) layer_used = true;
    return true;
}

bool extensions_layer_open(u32& first, u32& count, u32& opening) {
    if (layer_entry == kNone) return false;
    first = entries[layer_entry].layer_first;
    count = entries[layer_entry].layer_count;
    opening = layer_openings;
    return true;
}
void extensions_layer_frame(u32 elapsed_ms) {
    if (layer_entry == kNone || dispatching) return;
    if (layer_holding && layer_held_ms < 60000) layer_held_ms += elapsed_ms;
    layer_send(extensions::Layer::frame, 0, false, 0, elapsed_ms);
}
void extensions_layer_draw(const extensions::Canvas& canvas) {
    if (layer_entry == kNone || dispatching || !entries[layer_entry].hooks->draw) return;
    dispatching = true;
    entries[layer_entry].hooks->draw(canvas);
    dispatching = false;
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
            // A pair claim's chord needs both of its keys held.
            const u32 partner = registry->claim_partners ? registry->claim_partners[c] : 0;
            if (registry->claim_chords[c] != chord_black_keys || !extension_key_held(registry->claim_keys[c]) ||
                (partner && !extension_key_held(partner)))
                continue;
            // Only an owner that receives queued control takes the key.
            for (u32 i = 0; i < entry_count && !fired; ++i)
                if ((entries[i].claims & (1u << c)) && entries[i].hooks->queued_control) { index = i; fired = 1u << c; }
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
