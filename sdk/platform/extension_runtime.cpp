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
struct Transaction { u32 granted; bool attempted, notified; };
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
        // A page claim reaches its owner's layer hook, any other its queued control.
        for (u32 c = 0; c < known.claim_count; ++c) {
            if (!(entry.claims & (1u << c))) continue;
            const bool page = known.claim_pages && known.claim_pages[c];
            if (page ? !entry.hooks->layer : !entry.hooks->queued_control) return false;
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

// Actions an entry's fired claims grant.
u32 grants(const ExtensionEntry& entry, u32 fired) {
    u32 result = 0;
    for (u32 a = 0; fired && a < registry->action_count; ++a) {
        const u32 needs = registry->action_claims[a] ? registry->action_claims[a] : entry.claims;
        if ((entry.actions & (1u << a)) && (fired & needs)) result |= 1u << a;
    }
    return result;
}
bool page_claim(u32 c) { return registry->claim_pages && registry->claim_pages[c]; }
// Actions granted while a claimed key fires and stock accepted it. A page
// claim's key is not its gesture: it grants through its layer.
u32 granted(const ExtensionEntry& entry, const extensions::Control& control) {
    if (!control.accepted) return 0;
    u32 fired = 0;
    for (u32 c = 0; c < registry->claim_count; ++c)
        if ((entry.claims & (1u << c)) && !page_claim(c) && registry->claim_keys[c] == control.key) fired |= 1u << c;
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
u32 insert_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, false);
}
u32 replace_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, true);
}
bool show_lfo(u32 track, u32 lfo) {
    if (!(transaction.granted & action_lfo_copies) || track >= 8 || lfo >= extensions::lfo_count) return false;
    return extension_show_lfo(track, lfo);
}
u32 shown_lfo(u32 track) {
    if (!(transaction.granted & action_lfo_copies) || track >= 8) return 0;
    return extension_shown_lfo(track);
}
const extensions::Actions actions = {insert_notes, notify, replace_notes, show_lfo, shown_lfo};
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
void extensions_queued_control(const extensions::Control& control) {
    if (dispatching) return;
    extensions_sync();
    if (!entries) return;
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i) {
        transaction = {granted(entries[i], control), false, false};
        if (entries[i].hooks->queued_control) entries[i].hooks->queued_control(current, control, actions);
        transaction = {0, false, false};
    }
    dispatching = false;
}

namespace {
// The open page layer (extension.hpp, Layer): its entry, claim, page and
// track, or none. Keys it took from stock: only it hears their release.
const u32 kNone = ~0u, kKeys = 64;
u32 layer_entry = kNone, layer_claim, layer_page, layer_track;
u32 layer_took[kKeys / 32];

bool layer_send(extensions::Layer::Kind kind, u32 key = 0, bool pressed = false, i32 delta = 0,
                bool shift = false) {
    if (layer_entry == kNone || dispatching) return false;
    const ExtensionEntry& entry = entries[layer_entry];
    const extensions::Layer event = {kind, key, pressed, delta, shift, layer_track};
    dispatching = true;
    transaction = {grants(entry, 1u << layer_claim), false, false};
    const bool took = entry.hooks->layer(current, event, actions);
    transaction = {0, false, false};
    dispatching = false;
    return took;
}
void close_layer() {
    if (layer_entry == kNone) return;
    layer_send(extensions::Layer::close);
    layer_entry = kNone;
    layer_took[0] = layer_took[1] = 0;
}
// The page claim for `page`, and its owner.
bool page_owner(u32 page, u32& claim, u32& entry) {
    if (!page || !ready() || !registry->claim_pages) return false;
    for (u32 c = 0; c < registry->claim_count; ++c) {
        if (registry->claim_pages[c] != page) continue;
        for (u32 i = 0; i < entry_count; ++i)
            if ((entries[i].claims & (1u << c)) && entries[i].hooks->layer) {
                claim = c;
                entry = i;
                return true;
            }
    }
    return false;
}
}

bool extensions_hold(u32 action) {
    if (!ready()) return false;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].actions & action) return true;
    return false;
}

void extensions_page(u32 page, u32 track) {
    if (dispatching) return;
    if (layer_entry != kNone && layer_page == page && layer_track == track) return;
    close_layer();
    u32 claim = 0, entry = 0;
    if (track >= 8 || !page_owner(page, claim, entry)) return;
    layer_entry = entry;
    layer_claim = claim;
    layer_page = page;
    layer_track = track;
    layer_took[0] = layer_took[1] = 0;
    layer_send(extensions::Layer::open);
}

bool extensions_page_key(u32 key, bool pressed, bool shift) {
    if (layer_entry == kNone || dispatching || key >= kKeys) return false;
    u32& took = layer_took[key / 32];
    const u32 bit = 1u << (key % 32);
    if (!pressed) {
        if (!(took & bit)) return false;  // a release of a press stock had
        took &= ~bit;
        layer_send(extensions::Layer::keypress, key, false, 0, shift);
        return true;  // its press was taken, so stock never saw it
    }
    const bool taken = layer_send(extensions::Layer::keypress, key, true, 0, shift);
    if (taken) took |= bit;
    return taken;
}

bool extensions_page_turn(u32 encoder, i32 delta, bool shift) {
    if (layer_entry == kNone || dispatching || encoder >= 4) return false;
    return layer_send(extensions::Layer::turn, encoder, false, delta, shift);
}
}}
