#include "extension_runtime.hpp"

namespace opxy { namespace platform {
namespace {
extensions::Context current;
const ExtensionEntry* entries;
const ExtensionRegistry* registry;
u32 entry_count;
bool initialized, dispatching, settings_loaded;
char notification[32];
// Each entry's settings rows: the value last given and the rows changed since
// the last delivery.
u8 setting_values[16][kSettingRows];
u32 setting_changed[16];

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

// A settings page as the build checked it: a title, 1-12 rows, each with a
// label, a key, 1-8 values (one: an info row) and a starting value among them.
bool valid_settings(const ExtensionSettings& page) {
    if (!page.title || !*page.title || !page.rows || !page.row_count || page.row_count > kSettingRows) return false;
    for (u32 r = 0; r < page.row_count; ++r) {
        const SettingRow& row = page.rows[r];
        if (!row.label || !*row.label || !row.key || !*row.key || !row.values || !row.value_count ||
            row.value_count > kSettingValues || row.initial >= row.value_count) return false;
        for (u32 v = 0; v < row.value_count; ++v)
            if (!row.values[v] || (!*row.values[v] && row.value_count > 1)) return false;
    }
    return true;
}

// The collection must agree with the registry, whatever the build checked:
// claims exclusive and known, every action backed by a claim its holder owns.
bool ready() {
    if (initialized) return entries != nullptr;
    initialized = true;
    u32 count = 0;
    const ExtensionEntry* list = extension_collection(count);
    const ExtensionRegistry& known = extension_registry();
    if (!list || count > 16 || known.claim_count > 32 || known.action_count > 32) return false;
    u32 claimed = 0, pages = 0;
    for (u32 i = 0; i < count; ++i) {
        const ExtensionEntry& entry = list[i];
        if (!entry.id || !*entry.id || !entry.hooks || (entry.claims & claimed) ||
            (entry.claims & ~bits(known.claim_count)) || (entry.actions & ~bits(known.action_count)) ||
            (entry.claims && !entry.hooks->queued_control) ||
            (entry.settings && (!valid_settings(*entry.settings) || ++pages > kSettingPages))) return false;
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
    for (u32 i = 0; i < count; ++i)
        for (u32 r = 0; list[i].settings && r < list[i].settings->row_count; ++r)
            setting_values[i][r] = static_cast<u8>(list[i].settings->rows[r].initial);
    return true;
}

// Saved values first (once), then each changed row to its extension.
void deliver_settings() {
    if (!settings_loaded) {
        settings_loaded = true;
        extension_settings_load();
        for (u32 i = 0; i < entry_count; ++i)
            for (u32 r = 0; entries[i].settings && r < entries[i].settings->row_count; ++r)
                if (entries[i].settings->rows[r].value_count > 1) setting_changed[i] |= 1u << r;
    }
    for (u32 i = 0; i < entry_count; ++i) {
        const u32 changed = setting_changed[i];
        setting_changed[i] = 0;
        for (u32 r = 0; changed && r < kSettingRows; ++r)
            if ((changed & (1u << r)) && entries[i].hooks->setting)
                entries[i].hooks->setting(r, setting_values[i][r]);
    }
}

// Actions granted while a claimed key fires and stock accepted it.
u32 granted(const ExtensionEntry& entry, const extensions::Control& control) {
    if (!control.accepted) return 0;
    u32 fired = 0;
    for (u32 c = 0; c < registry->claim_count; ++c)
        if ((entry.claims & (1u << c)) && registry->claim_keys[c] == control.key) fired |= 1u << c;
    u32 result = 0;
    for (u32 a = 0; fired && a < registry->action_count; ++a) {
        const u32 needs = registry->action_claims[a] ? registry->action_claims[a] : entry.claims;
        if ((entry.actions & (1u << a)) && (fired & needs)) result |= 1u << a;
    }
    return result;
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
const extensions::Actions actions = {insert_notes, notify, replace_notes};
}

const ExtensionSettings* extensions_settings_page(u32 page, u32& entry) {
    if (!ready()) return nullptr;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].settings && !page--) {
            entry = i;
            return entries[i].settings;
        }
    return nullptr;
}
void extensions_setting(u32 entry, u32 row, u32 value) {
    if (!ready() || entry >= entry_count || !entries[entry].settings ||
        row >= entries[entry].settings->row_count || value >= entries[entry].settings->rows[row].value_count ||
        setting_values[entry][row] == value) return;
    setting_values[entry][row] = static_cast<u8>(value);
    setting_changed[entry] |= 1u << row;
}

void extensions_sync() {
    if (dispatching || !ready()) return;
    current.recording = extension_context(current.playing);
    dispatching = true;
    deliver_settings();
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
}}
