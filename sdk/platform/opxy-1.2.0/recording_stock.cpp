#include "recording_stock.hpp"
#include "recording_container.hpp"
#include "../extension_runtime.hpp"
#include "stock.hpp"

extern "C" void sdk_recording_advance(opxy::u32, opxy::u32) __asm__("sdk_recording_advance");
extern "C" void sdk_recording_insert(opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_recording_insert");
namespace opxy { namespace platform { namespace recording_detail {
const u32 controller_address = 0x860cf9c8;
const u32 active_address = 0x860c9030;
const u32 raw_per_tick = 1680;
RecordingContext saved_context;
bool saved_running, saved_locked, enabled, undo_taken, known_position;
u32 saved_revision, lap = 1, expected_position;
u32 generations[120];
i32 cached_positions[120];
u32 cached_effective[120];
bool cached[120];
extensions::RecordingFrame frame;
extensions::RecordingSelection selection;
bool used[120], active[120];

void reset() {
    lap = 1; known_position = undo_taken = false;
    for (u32 i = 0; i < 120; ++i) { generations[i] = 0; cached[i] = false; }
    const auto* policy = extension_recording_policy();
    if (policy) policy->reset();
}
bool sync() {
    if (!recording_mode_enabled()) {
        if (enabled) reset();
        enabled = false;
        return false;
    }
    bool running = false;
    const RecordingContext context = extension_context(running);
    // Momentary Record is also the capture shortcut. Replace is deliberately
    // limited to stock's latched Record+Play mode, never transient tap arming.
    const bool locked = byte(controller_address + 0x3e2f) != 0;
    const u32 revision = recording_mode_revision();
    if (!(context == saved_context) || running != saved_running || locked != saved_locked ||
        revision != saved_revision) reset();
    saved_context = context; saved_running = running; saved_locked = locked; saved_revision = revision;
    enabled = recording_mode_enabled() && context.eligible && running && locked &&
        context.track < 8 && context.length && context.length <= 0x7fffffffu / raw_per_tick;
    return enabled;
}
recording::Container* container() {
    return stock<recording::Container*(u32, u32)>(0x8d585c60)(saved_context.track, 0);
}
bool active_slots(const recording::Container& notes) {
    for (u32 i = 0; i < 120; ++i) active[i] = (notes.slots[i].note.flags & 2) != 0;
    const u32 count = word(active_address + 0xf0);
    if (count > 20) return false;
    for (u32 i = 0; i < count; ++i) {
        if (byte(active_address + i * 12 + 4) != saved_context.track) continue;
        const Address payload = reinterpret_cast<Address>(at<recording::Note>(word(active_address + i * 12)));
        const Address base = reinterpret_cast<Address>(&notes.slots[0].note);
        if (payload < base || (payload - base) % 16 || (payload - base) / 16 >= 120) return false;
        const u32 slot = static_cast<u32>((payload - base) / 16);
        if (!used[slot]) return false;
        active[slot] = true;
    }
    return true;
}
u32 effective_raw(const recording::Note& note) {
    const i32 quantized = stock<i32(i32, u32, u32)>(0x8d5aa714)(
        note.position, saved_context.quantization, saved_context.scale);
    const i32 length = static_cast<i32>(saved_context.length);
    i32 phase = quantized % length;
    if (phase < 0) phase += length;
    return static_cast<u32>(phase) * raw_per_tick;
}
bool load_frame(recording::Container& notes) {
    if (!recording::valid(notes, used) || !active_slots(notes)) return false;
    for (u32 i = 0; i < 120; ++i) {
        const recording::Note& note = notes.slots[i].note;
        if (used[i] && (note.position < -240 || note.position >= static_cast<i32>(saved_context.length) ||
            note.length > saved_context.length || note.pitch > 127 || note.velocity > 127)) return false;
        if (used[i] && (!cached[i] || cached_positions[i] != note.position)) {
            cached_positions[i] = note.position;
            cached_effective[i] = effective_raw(note);
            cached[i] = true;
        }
        frame.slots[i] = {generations[i], used[i] ? cached_effective[i] : 0, used[i], active[i]};
    }
    return true;
}
void begin_undo() {
    if (undo_taken) return;
    stock<void(u32)>(0x8d58fd38)(controller_address);
    stock<void(u32, u32)>(0x8d591f40)(controller_address, saved_context.track);
    undo_taken = true;
}
bool apply(recording::Container& notes, u32 from, u32 to, u32 length) {
    if (from == to) return true;
    if (!load_frame(notes)) return false;
    frame.lap = lap; frame.from_raw = from; frame.to_raw = to; frame.length_raw = length;
    for (u32 i = 0; i < 120; ++i) selection.slots[i] = false;
    extension_recording_policy()->select(frame, selection);
    u32 count = 0;
    for (u32 i = 0; i < 120; ++i) if (selection.slots[i]) {
        if (!used[i] || active[i]) return false;
        ++count;
    }
    if (!count) return true;
    // First destructive edit starts one undo snapshot for this armed take.
    // No container mutation occurs until the whole selection is validated.
    begin_undo();
    u32 removed = 0;
    if (!recording::erase(notes, selection, active, removed)) return false;
    if (removed) stock<void(u32)>(0x8d585d00)(saved_context.track);
    return true;
}
void advance(u32 track_controller, u32 delta) {
    if (!sync() || byte(track_controller + 0x194) != saved_context.track || !delta) return;
    const i32 position = static_cast<i32>(word(track_controller + 0x198));
    const u32 length = saved_context.length * raw_per_tick;
    // Seek/count-in/discontinuous updates are not a musical pass. Keep stock
    // traversal but discard replacement ownership until an ordinary interval.
    if (position < 0 || static_cast<u32>(position) >= length || delta > length ||
        (known_position && expected_position != static_cast<u32>(position))) {
        reset(); return;
    }
    auto* notes = container();
    if (!notes) { reset(); return; }
    const u32 from = static_cast<u32>(position), end = from + delta;
    if (!apply(*notes, from, end < length ? end : length, length)) { reset(); return; }
    if (end >= length) {
        ++lap;
        if (!apply(*notes, 0, end - length, length)) { reset(); return; }
    }
    expected_position = end >= length ? end - length : end;
    known_position = true;
}
}}}

namespace opxy { namespace platform {
void recording_sync() { recording_detail::sync(); }
u32 recording_input_count(u32 state, u32 track) {
    if (state != 0x860c9628 || !recording_detail::sync() || track != recording_detail::saved_context.track) return 20;
    const u32 count = word(recording_detail::active_address + 0xf0);
    auto* notes = recording_detail::container();
    if (count >= 20 || !notes || notes->count >= 120 || !recording::valid(*notes, recording_detail::used)) return 20;
    recording_detail::begin_undo();
    return count;
}
void recording_inserted(u32 track, u32 slot) {
    using namespace recording_detail;
    if (!sync() || track != saved_context.track || slot >= 120) return;
    auto* notes = container();
    if (!notes || !recording::valid(*notes, used) || !used[slot]) return;
    // Count a fresh slot incarnation even when a repeated note is identical.
    const u32 generation = ++generations[slot];
    const u32 effective = effective_raw(notes->slots[slot].note);
    i32 tick = extension_position(track);
    const i32 ticks = static_cast<i32>(saved_context.length);
    tick %= ticks; if (tick < 0) tick += ticks;
    const u32 phase = static_cast<u32>(tick) * raw_per_tick;
    const u32 length = saved_context.length * raw_per_tick;
    const i32 distance = static_cast<i32>(effective) - static_cast<i32>(phase);
    const u32 intended = distance < -static_cast<i32>(length / 2) ? lap + 1 :
        distance > static_cast<i32>(length / 2) ? lap - 1 : lap;
    const extensions::RecordedSlot event = {slot, generation, intended};
    extension_recording_policy()->recorded(event);
}
void recording_input_done(u32 previous_count, u32 track, u32 pitch) {
    using namespace recording_detail;
    if (previous_count >= 20 || !sync() || track != saved_context.track ||
        word(active_address + 0xf0) != previous_count + 1 ||
        byte(active_address + previous_count * 12 + 4) != track) return;
    auto* notes = container();
    if (!notes) return;
    const Address payload = reinterpret_cast<Address>(at<recording::Note>(word(active_address + previous_count * 12)));
    const Address base = reinterpret_cast<Address>(&notes->slots[0].note);
    if (payload < base || (payload - base) % 16 || (payload - base) / 16 >= 120) return;
    const u32 slot = static_cast<u32>((payload - base) / 16);
    if (notes->slots[slot].note.pitch != pitch || !(notes->slots[slot].note.flags & 2)) return;
    recording_inserted(track, slot);
}
}}
void sdk_recording_advance(opxy::u32 controller, opxy::u32 delta) {
    opxy::platform::recording_detail::advance(controller, delta);
    opxy::platform::stock<void(opxy::u32, opxy::u32)>(0x8d5a9b5c)(controller, delta);
}
void sdk_recording_insert(opxy::u32 state, opxy::u32 pitch, opxy::u32 track, opxy::u32 velocity) {
    const opxy::u32 before = pitch <= 127 && velocity > 0 && velocity <= 127 ?
        opxy::platform::recording_input_count(state, track) : 20;
    opxy::platform::stock<void(opxy::u32, opxy::u32, opxy::u32, opxy::u32)>(0x8d58ed0a)(state, pitch, track, velocity);
    opxy::platform::recording_input_done(before, track, pitch);
}
