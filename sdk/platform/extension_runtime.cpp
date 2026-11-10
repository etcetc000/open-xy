#include "extension_runtime.hpp"

namespace opxy { namespace platform {
namespace {
extensions::Context current;
const ExtensionEntry* entries;
const ExtensionRegistry* registry;
u32 entry_count;
bool initialized, dispatching;
char notification[64];

// The actions one callback may take, and what it has done with them: one stock
// undo transaction and at most one notification per callback.
// moving: a sequencer_change callback, where loops that hold may move.
struct Transaction { u32 granted, declared; bool attempted, notified, looped, started, moving; };
Transaction transaction;
// Each track's bar count and song-time bar length at the last sync.
struct Shape { u8 bars[extensions::sequencer_tracks]; u32 ticks[extensions::sequencer_tracks]; };
Shape shape;

bool any_loop(const extensions::Sequencer& s) {
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t)
        if (s.track[t].loop != extensions::no_bar) return true;
    return false;
}
// Whether any track's bar count or scale changed since the last sync.
bool reshaped(const extensions::Sequencer& s) {
    bool changed = false;
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) {
        changed |= shape.bars[t] != s.track[t].bars || shape.ticks[t] != s.track[t].bar_ticks;
        shape.bars[t] = s.track[t].bars;
        shape.ticks[t] = s.track[t].bar_ticks;
    }
    return changed;
}

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
    if (!list || count > 16 || known.claim_count > 32 || known.claim_key_count > 32 ||
        known.action_count > 32) return false;
    for (u32 k = 0; k < known.claim_key_count; ++k)
        if (known.claim_keys[k].claim >= known.claim_count) return false;
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

// Actions granted while a claimed key fires and stock accepted it.
u32 granted(const ExtensionEntry& entry, const extensions::Control& control) {
    if (!control.accepted) return 0;
    u32 fired = 0;
    for (u32 k = 0; k < registry->claim_key_count; ++k) {
        const ClaimKey& pair = registry->claim_keys[k];
        if ((entry.claims & (1u << pair.claim)) && pair.key == control.key) fired |= 1u << pair.claim;
    }
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
// A popup's text: 1 to `limit` characters, copied, once per callback.
bool take_notice(const char* text, u32 limit) {
    if (!(transaction.granted & action_notify) || transaction.notified || !text) return false;
    u32 length = 0;
    while (length <= limit && text[length]) ++length;
    if (!length || length > limit) return false;
    for (u32 i = 0; i <= length; ++i) notification[i] = text[i];
    transaction.notified = true;
    return true;
}
void notify(const char* text) {
    if (take_notice(text, 31)) extension_notify(notification);
}
void notify_for(const char* text, u32 milliseconds) {
    if (milliseconds < 100 || milliseconds > 10000) return;
    if (take_notice(text, 63)) extension_notify_for(notification, milliseconds);
}
u32 insert_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, false);
}
u32 replace_notes(const extensions::Context& expected, u32 track, const RecordedNote* notes, u32 count) {
    return write_notes(expected, track, notes, count, true);
}
// Ending a loop only gives playback back to stock, so a holder of the action
// may do it in any queued_control callback; holding bars needs the grant, or
// a sequencer_change callback while bars already loop (moving them, never
// starting a loop). Bars must exist in the playing pattern of a context that
// is still current.
// The context is still current, and the loop's bars exist in the playing
// patterns as the context saw them. `sequencer` receives the tracks now.
bool loop_fits(const extensions::Context& expected, const extensions::BarLoop& loop,
               extensions::Sequencer& sequencer) {
    bool playing = false;
    const RecordingContext now = extension_context(playing);
    if (!(now == expected.recording) || playing != expected.playing ||
        !(now == current.recording) || playing != current.playing) return false;
    extension_sequencer(sequencer);
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) {
        const u8 bar = loop.bar[t];
        if (bar != extensions::no_bar && (bar >= sequencer.track[t].bars ||
            sequencer.track[t].bars != expected.sequencer.track[t].bars)) return false;
    }
    return true;
}
bool loop_bars(const extensions::Context& expected, const extensions::BarLoop& loop) {
    if (!(transaction.declared & action_loop_bars) || transaction.looped) return false;
    bool any = false;
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) any |= loop.bar[t] != extensions::no_bar;
    extensions::Sequencer sequencer;
    if (!loop_fits(expected, loop, sequencer)) return false;
    if (any && !(transaction.granted & action_loop_bars) && !(transaction.moving && any_loop(sequencer)))
        return false;
    transaction.looped = true;  // One loop change per callback, including failures.
    return extension_loop(loop);
}
// Both grants, while playing, once for the callback's loop change and start.
bool loop_at_bar_end(const extensions::Context& expected, const extensions::BarLoop& loop, u32 song_tick) {
    const u32 needs = action_loop_bars | action_play_from;
    if ((transaction.granted & needs) != needs || transaction.looped || transaction.started) return false;
    extensions::Sequencer sequencer;
    if (!expected.playing || !loop_fits(expected, loop, sequencer)) return false;
    transaction.looped = transaction.started = true;
    return extension_loop_at_bar_end(loop, song_tick, current.recording.track);
}
// Starting (or, playing, restarting) playback needs the grant, once per
// callback, from a current context.
bool play_from(const extensions::Context& expected, u32 song_tick) {
    if (!(transaction.granted & action_play_from) || transaction.started) return false;
    bool playing = false;
    const RecordingContext now = extension_context(playing);
    if (playing != expected.playing || !(now == expected.recording) || !(now == current.recording) ||
        playing != current.playing) return false;
    transaction.started = true;
    return extension_play_from(song_tick);
}
const extensions::Actions actions = {insert_notes, notify, replace_notes, loop_bars, play_from, notify_for,
                                     loop_at_bar_end};
}

void extensions_sync() {
    if (dispatching || !ready()) return;
    current.recording = extension_context(current.playing);
    extension_sequencer(current.sequencer);
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->context) entries[i].hooks->context(current);
    if (reshaped(current.sequencer) && any_loop(current.sequencer)) {
        for (u32 i = 0; i < entry_count; ++i) {
            if (!entries[i].hooks->sequencer_change) continue;
            transaction = {0, entries[i].actions, false, false, false, false, true};
            entries[i].hooks->sequencer_change(current, actions);
            transaction = {0, 0, false, false, false, false, false};
        }
        current.recording = extension_context(current.playing);
        extension_sequencer(current.sequencer);
    }
    dispatching = false;
}
namespace {
bool owned(u32 claim) {
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].claims & (1u << claim)) return true;
    return false;
}
}
bool extensions_takes(u32 key, u32 screen) {
    if (!ready() || !screen) return false;
    for (u32 k = 0; k < registry->claim_key_count; ++k) {
        const ClaimKey& pair = registry->claim_keys[k];
        if (pair.key == key && pair.screen == screen && owned(pair.claim) &&
            (pair.held == extensions::keys::none || extension_key_held(pair.held))) return true;
    }
    return false;
}
bool extensions_needs_held(u32 held, u32 screen) {
    if (!ready() || !screen || held == extensions::keys::none) return false;
    for (u32 k = 0; k < registry->claim_key_count; ++k) {
        const ClaimKey& pair = registry->claim_keys[k];
        if (pair.held == held && pair.screen == screen && owned(pair.claim)) return true;
    }
    return false;
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
        transaction = {granted(entries[i], control), entries[i].actions, false, false, false, false, false};
        if (entries[i].hooks->queued_control) entries[i].hooks->queued_control(current, control, actions);
        transaction = {0, 0, false, false, false, false, false};
    }
    dispatching = false;
}
}}
