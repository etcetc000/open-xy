#include "extension_runtime.hpp"
#include "arrangement_descriptor.hpp"
extern "C" const opxy::extensions::ArrangementSurface* sdk_arrangement_owner() __asm__("sdk_arrangement_owner");

namespace opxy { namespace platform {
namespace {
extensions::Context current;
const ExtensionEntry* entries;
const ExtensionRegistry* registry;
u32 entry_count;
bool initialized, dispatching, surface_reply_scope;
char notification[32];

// The actions one callback may take, and what it has done with them: one stock
// undo transaction and at most one notification per callback.
struct Transaction { u32 granted; bool attempted, notified; u32 owner; };
// Acknowledged single mailbox: controller publishes request last; GUI publishes
// completion last; controller keeps storage owned through the result callback.
struct PendingRead {
    volatile u32 state; // 0 free, 1 requested, 2 complete
    u32 owner, ticket; extensions::PatternRef ref;
    bool success; u32 count; RecordedNote notes[extensions::pattern_note_capacity];
};
PendingRead pending_read;
u32 read_ticket;
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
            (entry.claims && !entry.hooks->queued_control && !(entry.hooks->arrangement && entry.hooks->arrangement->intent))) return false;
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
bool discover_pattern(u32 track, u32 index, extensions::PatternInfo& out) {
    return (transaction.granted & action_read_patterns) && extension_discover_pattern(track, index, out);
}
bool read_pattern(const extensions::PatternRef& ref, RecordedNote* out, u32 capacity, u32& count) {
    count = 0;
    return (transaction.granted & action_read_patterns) && extension_read_pattern(ref, out, capacity, count);
}
u32 request_pattern_read(const extensions::PatternRef& ref) {
    if (!(transaction.granted & action_read_patterns) || !transaction.owner || pending_read.state ||
        ref.track >= 8 || ref.index >= 16 || (!entries[transaction.owner-1].hooks->pattern_read_result &&
         !(entries[transaction.owner-1].hooks->arrangement && entries[transaction.owner-1].hooks->arrangement->pattern_result))) return 0;
    if (!++read_ticket) ++read_ticket;
    pending_read.owner=transaction.owner; pending_read.ticket=read_ticket; pending_read.ref=ref;
    pending_read.success=false;pending_read.count=0;
    extension_barrier();pending_read.state=1;
    return read_ticket;
}
bool post_arrangement_reply(const extensions::ArrangementContext& expected, const u8* data, u32 size) {
    if (!surface_reply_scope || !(transaction.granted & action_surface_reply) || !transaction.owner || transaction.owner > entry_count ||
        entries[transaction.owner - 1].hooks->arrangement != sdk_arrangement_owner()) return false;
    return sdk_arrangement_post_reply(expected, data, size);
}
const extensions::Actions actions = {insert_notes, notify, replace_notes, discover_pattern, read_pattern, request_pattern_read, post_arrangement_reply};
void deliver_arrangement_intent() {
    extensions::ArrangementIntent intent;
    const auto* surface = sdk_arrangement_owner();
    if (!surface || !surface->intent || !sdk_arrangement_take_intent(&intent)) return;
    for (u32 i = 0; i < entry_count; ++i) {
        if (entries[i].hooks->arrangement != surface) continue;
        transaction = {entries[i].actions & ~action_edit_notes, false, false, i + 1};
        surface_reply_scope = true;
        surface->intent(current, intent, actions);
        surface_reply_scope = false;
        transaction = {0, false, false, 0};
        return;
    }
}
void deliver_pattern_read() {
    if (pending_read.state != 2) return;
    extension_barrier();
    const u32 owner=pending_read.owner;
    extensions::PatternInfo now{};
    const extensions::PatternRef& ref=pending_read.ref;
    const bool success=pending_read.success && extension_discover_pattern(ref.track,ref.index,now) &&
        now.ref.project==ref.project && now.ref.generation==ref.generation;
    const extensions::PatternReadResult result={pending_read.ticket,ref,success,
        success ? pending_read.notes : nullptr,success ? pending_read.count : 0};
    if(owner && owner<=entry_count && entries[owner-1].hooks->pattern_read_result)
        entries[owner-1].hooks->pattern_read_result(result);
    if (owner && owner <= entry_count) {
        const auto* surface = entries[owner - 1].hooks->arrangement;
        if (surface && surface == sdk_arrangement_owner() && surface->pattern_result) {
            transaction = {entries[owner - 1].actions & action_surface_reply, false, false, owner};
            surface_reply_scope = true;
            surface->pattern_result(result, actions);
            surface_reply_scope = false;
            transaction = {0, false, false, 0};
        }
    }
    extension_barrier();pending_read.state=0;
}
}

void extensions_sync() {
    if (dispatching || !ready()) return;
    current.recording = extension_context(current.playing);
    dispatching = true;
    for (u32 i = 0; i < entry_count; ++i)
        if (entries[i].hooks->context) entries[i].hooks->context(current);
    deliver_pattern_read();
    deliver_arrangement_intent();
    dispatching = false;
}
void extensions_gui_poll() {
    if (pending_read.state != 1) return;
    extension_barrier();
    pending_read.success=extension_read_pattern(pending_read.ref,pending_read.notes,extensions::pattern_note_capacity,pending_read.count);
    extension_barrier();pending_read.state=2;
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
        transaction = {granted(entries[i], control), false, false, i+1};
        if (entries[i].hooks->queued_control) entries[i].hooks->queued_control(current, control, actions);
        transaction = {0, false, false, 0};
    }
    dispatching = false;
}
}}
