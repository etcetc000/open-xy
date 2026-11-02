#include "arranger_resolver.hpp"
#include "arranger_phase.hpp"
#include "arranger_state.hpp"
#include "arranger_transport.hpp"
#include "arranger_project.hpp"
#include "barrier.hpp"
#include "stock.hpp"

using opxy::u8;
using opxy::u32;
using opxy::i32;

extern "C" const u8* sdk_arranger_edit_source(u32, bool) __asm__("sdk_arranger_edit_source");
extern "C" bool sdk_arranger_record_target(u32, u32*) __asm__("sdk_arranger_record_target");
extern "C" void sdk_arranger_start(u32) __asm__("sdk_arranger_start");
extern "C" void sdk_arranger_bounce_dispatch(u32) __asm__("sdk_arranger_bounce_dispatch");

namespace {
const u8* sources[8];
const u8* instruments[8];
const opxy::arranger::Clip* clips[8];
u32 source_project;
u32 source_generation;
bool releasing;
u32 project_generation() { return opxy::platform::stock<u32()>(0x8d584224)(); }
bool project_matches() { return sdk_arranger_audio_project_matches(project_generation()); }
// Both GUI and sequencer run on master. Startup692a stores the sequencer task
// handle at84247150; native eccc returns current RTOS task (also used by4dd318).
// Task identity prevents a preempting GUI callback from borrowing playback state.
bool sequencer_context() {
    using namespace opxy::platform;
    const u32 task = word(0x84247150);
    return task && stock<u32()>(0x8d5c3eac)() && stock<u32()>(0x8c03eccc)() == task;
}
const u8* pattern_source(u32 track, bool instrument = false) {
    using namespace opxy::platform;
    if (track >= 8) return nullptr;
    if (!sequencer_context())
        return stock<u32()>(0x8d5c3eac)() ? sdk_arranger_edit_source(track, instrument) : nullptr;
    if ((!sdk_arranger_audio_active() && !releasing) ||
        source_project != word(0x840a0524) || source_generation != project_generation() ||
        !project_matches()) return nullptr;
    return instrument ? instruments[track] : sources[track];
}
u32 read_word(const u8* source) {
    return source[0] | (u32(source[1]) << 8) | (u32(source[2]) << 16) | (u32(source[3]) << 24);
}
}

// Each stock symbol is a trampoline containing the authenticated entry prologue.
// GUI reads and inactive/invalid playback selections delegate unchanged.
#define ARRANGER_POINTER(name, offset) \
extern "C" const u8* sdk_arranger_stock_##name(u32) __asm__("sdk_arranger_stock_" #name); \
extern "C" const u8* sdk_arranger_##name(u32) __asm__("sdk_arranger_" #name); \
extern "C" const u8* sdk_arranger_##name(u32 track) { \
    const u8* source = pattern_source(track); \
    return source ? source + offset : sdk_arranger_stock_##name(track); \
}

#define ARRANGER_VALUE(name, expression) \
extern "C" u32 sdk_arranger_stock_##name(u32) __asm__("sdk_arranger_stock_" #name); \
extern "C" u32 sdk_arranger_##name(u32) __asm__("sdk_arranger_" #name); \
extern "C" u32 sdk_arranger_##name(u32 track) { \
    const u8* source = pattern_source(track); \
    return source ? (expression) : sdk_arranger_stock_##name(track); \
}

ARRANGER_POINTER(pattern, 0)
ARRANGER_POINTER(pattern_ptr58, 0x58)
ARRANGER_POINTER(pattern_ptr38, 0x38)
ARRANGER_POINTER(pattern_ptr5394, 0x5394)
ARRANGER_POINTER(pattern_ptr457c, 0x457c)
ARRANGER_VALUE(pattern_byte30, source[0x30])
ARRANGER_VALUE(pattern_word34, read_word(source + 0x34))
ARRANGER_VALUE(pattern_length, source[0])
ARRANGER_VALUE(pattern_scale, source[8])
ARRANGER_VALUE(pattern_word4, read_word(source + 4))
ARRANGER_VALUE(pattern_byte9, source[9])
ARRANGER_VALUE(pattern_signed10, source[10] < 128 ? u32(source[10]) : u32(source[10]) - 256u)
#undef ARRANGER_POINTER
#undef ARRANGER_VALUE

extern "C" u32 sdk_arranger_stock_pattern_row_mask(u32, u32) __asm__("sdk_arranger_stock_pattern_row_mask");
extern "C" u32 sdk_arranger_pattern_row_mask(u32, u32) __asm__("sdk_arranger_pattern_row_mask");
extern "C" u32 sdk_arranger_pattern_row_mask(u32 track, u32 row) {
    const u8* source = pattern_source(track);
    if (!source) return sdk_arranger_stock_pattern_row_mask(track, row);
    const u8* value = source + 0x3062 + row * 16;
    return value[0] | (u32(value[1]) << 8);
}

extern "C" u32 sdk_arranger_stock_pattern_row_value(u32, u32, u32) __asm__("sdk_arranger_stock_pattern_row_value");
extern "C" u32 sdk_arranger_pattern_row_value(u32, u32, u32) __asm__("sdk_arranger_pattern_row_value");
extern "C" u32 sdk_arranger_pattern_row_value(u32 track, u32 row, u32 column) {
    const u8* source = pattern_source(track);
    return source ? source[0x3064 + row * 16 + column] :
        sdk_arranger_stock_pattern_row_value(track, row, column);
}

extern "C" const u8* sdk_arranger_stock_instrument_pattern(u32) __asm__("sdk_arranger_stock_instrument_pattern");
extern "C" const u8* sdk_arranger_instrument_pattern(u32) __asm__("sdk_arranger_instrument_pattern");
extern "C" const u8* sdk_arranger_instrument_pattern(u32 track) {
    const u8* source = pattern_source(track, true);
    return source ? source : sdk_arranger_stock_instrument_pattern(track);
}

namespace {
u32 dispatch_controller;
u32 dispatch_step;
bool dispatching;
struct TrackSelection {
    bool valid;
    u32 pattern, start, offset, phase_remainder;
    const u8* source;
    const u8* instrument;
};
TrackSelection selected[8];
bool force_seek;
bool was_active;
alignas(4) volatile u32 transport_sequence;
volatile u32 transport_position, transport_generation, transport_action, transport_record;
u32 transport_handled;
bool prepare_track(u32 object, u32 track);
struct RecordEvent { u32 generation, controller, track, pitch, velocity, pattern, action; };
struct RecordQueue {
    RecordEvent events[64];
    alignas(4) volatile u32 write, read, overflow;
};
RecordQueue record_queues[2];
bool draining_records;
alignas(4) volatile u32 record_owner;
alignas(4) volatile u32 record_status;
u32 record_lane = 8, record_pattern, record_generation;
bool bounce_active, bounce_was_playing, bounce_started;
alignas(4) volatile u32 bounce_running;
u32 bounce_track, bounce_limit, bounce_position, bounce_generation, bounce_project;
const u32 recorder = 0x860c072c, pending_notes = 0x860c0134;
bool timeline_loop() { return !bounce_active && sdk_arranger_audio_model().loopEnabled(); }
u32 advance_timeline(u32 delta) {
    if (!bounce_active) return sdk_arranger_audio_advance(delta);
    const u32 current = sdk_arranger_audio_model().position();
    const u32 position = current >= bounce_limit || delta > bounce_limit - current ? bounce_limit : current + delta;
    sdk_arranger_audio_seek(position);
    return position;
}

// A single GUI producer may replace an unconsumed command. Audio never spins
// waiting for a producer: an interrupted publication is retried next callback.
u32 consume_transport_command(u32 old_position, u32 controller) {
    using opxy::platform::barrier;
    const u32 sequence = transport_sequence;
    if ((sequence & 1u) || sequence == transport_handled) return 3;
    barrier();
    const u32 position = transport_position, generation = transport_generation, action = transport_action, record = transport_record;
    barrier();
    if (sequence != transport_sequence) return 3;
    transport_handled = sequence;
    if (!sdk_arranger_audio_active() || !project_matches() || generation != project_generation()) return 3;
    // Recheck at consumption: alternate recorder mode makes59025c rebuild notes.
    // In normal mode its byte+1 test returns immediately, with no external calls.
    if (action < 2 && (opxy::platform::byte(recorder) || opxy::platform::byte(recorder + 1))) return 3;
    // Native Play with REC held calls592998(controller,1); our cursor-start
    // replaces that handler, so preserve its persistent recording latch here.
    // A plain Play ends latched recording; a seek leaves stock Stop's state alone.
    if (action < 2) opxy::platform::stock<void(u32,u32)>(0x8d592998)(controller, record);
    sdk_arranger_audio_seek(action == 1 ? old_position : position);
    return action;
}

bool publish_transport_command(u32 position, u32 action, bool record = false) {
    using opxy::platform::barrier;
    ++transport_sequence;
    barrier();
    transport_position = position;
    transport_generation = project_generation();
    transport_action = action;
    transport_record = record ? 1u : 0u;
    barrier();
    ++transport_sequence;
    return true;
}

void close_recorded_notes(u32 track = 8) {
    using namespace opxy::platform;
    if (byte(recorder) || byte(recorder + 1)) return;
    const u32 count = word(pending_notes + 0xf0);
    if (count > 20) return;
    struct Note { u32 track, pitch; } notes[20];
    u32 used = 0;
    for (u32 index = 0; index < count; ++index) {
        const u32 entry = pending_notes + index * 12;
        const u32 lane = byte(entry + 4);
        if (lane >= 8 || (track < 8 && track != lane)) continue;
        const u8* note = at<const u8>(word(entry));
        const u8* source = selected[lane].source;
        if (!selected[lane].valid || !source) continue;
        const __UINTPTR_TYPE__ pointer = reinterpret_cast<__UINTPTR_TYPE__>(note);
        const __UINTPTR_TYPE__ begin = reinterpret_cast<__UINTPTR_TYPE__>(source + 0x457c + 4);
        if (pointer < begin || pointer >= begin + 120 * 16 || (pointer - begin) % 16) continue;
        notes[used++] = Note{lane, note[8]};
    }
    for (u32 index = 0; index < used; ++index)
        stock<void(u32, u32, u32)>(0x8d5904e2)(recorder, notes[index].track, notes[index].pitch);
}

bool queue_record(const RecordEvent& event) {
    using opxy::platform::barrier;
    const u32 role = sequencer_context() ? 0 : 1;
    RecordQueue& queue = record_queues[role];
    const u32 next = (queue.write + 1) % 64;
    if (next == queue.read) { queue.overflow = 1; return false; }
    barrier();
    queue.events[queue.write] = event;
    barrier();
    queue.write = next;
    return true;
}

void discard_record_queue() {
    for (u32 role = 0; role < 2; ++role) {
        record_queues[role].read = record_queues[role].write;
        record_queues[role].overflow = 0;
    }
}

void release_track(u32 controller, u32 track) {
    using namespace opxy::platform;
    struct Release { u8 track, note, kind, flags; } pending[50];
    const u32 count = word(controller + 0x3048);
    if (count > 50) return;
    u32 used = 0;
    for (u32 index = 0; index < count; ++index) {
        const u8* record = at<const u8>(controller + 0x2a08 + index * 32);
        if (record[12] != track) continue;
        pending[used++] = Release{record[12], record[13], record[17],
            static_cast<u8>((record[26] ? 2 : 0) | (record[24] ? 8 : 0))};
    }
    const u8* next_source = sources[track];
    const u8* next_instrument = instruments[track];
    sources[track] = selected[track].valid ? selected[track].source : nullptr;
    instruments[track] = selected[track].valid ? selected[track].instrument : nullptr;
    releasing = true;
    for (u32 index = 0; index < used; ++index) {
        const Release& value = pending[index];
        stock<void(u32, u32, u32, u32, u32, u32)>(0x8d595420)(
            controller, value.track, value.note, value.kind, value.flags, 0);
    }
    releasing = false;
    sources[track] = next_source;
    instruments[track] = next_instrument;
}

void prepare_sources() {
    using namespace opxy::platform;
    const opxy::arranger::Model& model = sdk_arranger_audio_model();
    source_project = word(0x840a0524);
    source_generation = project_generation();
    const u8* project = at<const u8>(source_project);
    for (u32 track = 0; track < 8; ++track) {
        u32 phase;
        const opxy::arranger::Clip* clip = (!bounce_active || track == bounce_track) ?
            model.active(static_cast<u8>(track), model.position(), phase) : nullptr;
        if (clip && clip->muted) clip = nullptr;
        if (record_owner && selected[track].valid && (force_seek || !clip ||
            selected[track].pattern != clip->pattern || selected[track].start != clip->start ||
            selected[track].offset != clip->offset || selected[track].phase_remainder != clip->phase_remainder)) close_recorded_notes(track);
        clips[track] = clip;
        sources[track] = clip ? arranger_pattern_source(project, track, clip->pattern, false) : nullptr;
        instruments[track] = clip ? arranger_pattern_source(project, track, clip->pattern, true) : nullptr;
        if (sources[track] && (!instruments[track] || !sources[track][0] || sources[track][8] >= 16))
            sources[track] = instruments[track] = nullptr;
    }
}

void apply_record(const RecordEvent& event) {
    using namespace opxy::platform;
    if (event.generation != project_generation() || !project_matches()) return;
    if (event.action == 2) {
        close_recorded_notes();
        record_lane = event.track;
        record_pattern = event.pattern;
        record_generation = event.generation;
        return;
    }
    if (!event.action) {
        stock<void(u32, u32, u32)>(0x8d5904e2)(recorder, event.track, event.pitch);
        return;
    }
    if (bounce_active || !sdk_arranger_audio_active() || event.track >= 8 || event.track != record_lane ||
        record_generation != event.generation || !sources[event.track] ||
        !clips[event.track] || byte(recorder) || byte(recorder + 1) ||
        (!byte(event.controller + 0x305d) && !byte(event.controller + 0x305e)) ||
        (event.pattern != 0xffffffffu && clips[event.track]->pattern != event.pattern) ||
        (record_pattern != 0xffffffffu && clips[event.track]->pattern != record_pattern)) return;
    // A new source starts a distinct undo snapshot; identities use the same resolver.
    const u32 previous_track = word(event.controller + 0x4610);
    if (previous_track != event.track || word(event.controller + 0x4614) != clips[event.track]->pattern)
        stock<void(u32)>(0x8d591948)(event.controller);
    stock<void(u32, u32, u32, u32)>(0x8d593c9c)(
        event.controller, event.pitch, event.track, event.velocity);
}

void drain_record_queue() {
    using opxy::platform::barrier;
    if (draining_records) return;
    draining_records = true;
    if (record_queues[0].overflow || record_queues[1].overflow) {
        close_recorded_notes();
        barrier();
        discard_record_queue();
        record_lane = 8;
        draining_records = false;
        return;
    }
    for (u32 order = 0; order < 2; ++order) {
        const u32 role = 1 - order;
        RecordQueue& queue = record_queues[role];
        const u32 end = queue.write;
        barrier();
        for (u32 count = 0; queue.read != end && count < 63; ++count) {
            const RecordEvent event = queue.events[queue.read];
            // Retire before calling stock: a nested note-off cannot replay it.
            barrier();
            queue.read = (queue.read + 1) % 64;
            apply_record(event);
        }
    }
    draining_records = false;
}

void update_record_status(u32 controller) {
    using namespace opxy::platform;
    if (!byte(controller + 0x3060) && !byte(controller + 0x305f)) record_status = 0;
    else if (record_lane >= 8 || record_generation != project_generation()) record_status = 3;
    else if (!byte(controller + 0x305d) && !byte(controller + 0x305e)) record_status = 1;
    else {
        u32 phase;
        const opxy::arranger::Model& model = sdk_arranger_audio_model();
        const opxy::arranger::Clip* clip = model.active(static_cast<u8>(record_lane), model.position(), phase);
        record_status = clip && !clip->muted && (record_pattern == 0xffffffffu || clip->pattern == record_pattern) ? 2 : 3;
    }
    barrier();
}

u32 source_phase(const opxy::arranger::Clip& clip, const u8* source, u32 time) {
    return opxy::platform::arranger_source_phase(clip, source, time);
}

u32 slice_length(u32 remaining) {
    const opxy::arranger::Model& model = sdk_arranger_audio_model();
    const u32 now = model.position();
    u32 result = remaining;
    for (u32 slot = 0; slot < opxy::arranger::Model::capacity; ++slot) {
        const opxy::arranger::Clip* clip = model.clip(slot);
        if (!clip) continue;
        const u32 end = clip->start + clip->length;
        if (clip->start > now && clip->start - now < result) result = clip->start - now;
        if (end > now && end - now < result) result = end - now;
    }
    if (timeline_loop() && now < model.loopEnd() && model.loopEnd() - now < result)
        result = model.loopEnd() - now;
    if (bounce_active && now < bounce_limit && bounce_limit - now < result) result = bounce_limit - now;
    for (u32 track = 0; track < 8; ++track) {
        if (!sources[track] || !clips[track]) continue;
        const u32 period = u32(sources[track][0]) * 480 * 1680;
        const u32 phase = source_phase(*clips[track], sources[track], now);
        const u32 distance = period - phase;
        const u32 rate = opxy::platform::arranger_source_rate(sources[track][8]);
        u32 to_wrap = (distance + rate - 1) / rate;
        // Equal start/end raw phases are not a whole-cycle event interval.
        if (!phase && to_wrap > 1) --to_wrap;
        if (to_wrap < result) result = to_wrap;
    }
    return result;
}
}

namespace {
bool prepare_track(u32 object, u32 track) {
    using namespace opxy::platform;
    TrackSelection& previous = selected[track];
    const u8* source = sources[track];
    if (!source) {
        if (previous.valid || force_seek) {
            release_track(dispatch_controller, track);
            stock<void(u32)>(0x8d5ac024)(object);
        }
        previous.valid = false;
        return false;
    }
    const opxy::arranger::Model& model = sdk_arranger_audio_model();
    const opxy::arranger::Clip* clip = clips[track];
    if (!clip) return false;
    if (force_seek || !previous.valid || previous.pattern != clip->pattern ||
        previous.start != clip->start || previous.offset != clip->offset || previous.phase_remainder != clip->phase_remainder) {
        release_track(dispatch_controller, track);
        stock<void(u32)>(0x8d5ac024)(object);
        stock<void(u32, u32)>(0x8d597730)(dispatch_controller, track);
        const u32 phase = source_phase(*clip, source, model.position());
        stock<void(u32, u32, u32, u32, u32)>(0x8d5ac4d8)(object, phase, 0, 0, 0);
        previous = TrackSelection{true, clip->pattern, clip->start, clip->offset, clip->phase_remainder, source, instruments[track]};
    }
    return true;
}
}

extern "C" void sdk_arranger_track_tick(u32, u32, u32) __asm__("sdk_arranger_track_tick");
extern "C" void sdk_arranger_track_tick(u32 object, u32 delta, u32 flag) {
    using namespace opxy::platform;
    const u32 track = byte(object + 0x194);
    if (bounce_active && track != bounce_track) return;
    if (!dispatching || !sdk_arranger_audio_active() || track >= 8 || sources[track])
        stock<void(u32, u32, u32)>(0x8d5abd18)(object, delta, flag);
}

extern "C" void sdk_arranger_dispatch(u32, u32, u32) __asm__("sdk_arranger_dispatch");
extern "C" void sdk_arranger_dispatch(u32 controller, u32 delta, u32 flag) {
    using namespace opxy::platform;
    dispatch_controller = controller;
    const u32 before_publish = sdk_arranger_audio_model().position();
    force_seek = sdk_arranger_consume() || force_seek;
    if (bounce_active) sdk_arranger_audio_seek(before_publish);
    if (!project_matches()) {
        // The coordinator can still be draining a writer/job after this old
        // project's transport gate has been discarded. Deliver its final ack.
        sdk_arranger_bounce_dispatch(controller);
        bounce_active = false;
        bounce_running = 0;
        transport_handled = transport_sequence;
        record_owner = 0;
        record_status = 0;
        record_lane = 8;
        discard_record_queue();
        // A project commit owns its own stock cleanup; never release its new notes
        // on behalf of the old project's cached clip selection.
        for (u32 track = 0; track < 8; ++track) {
            selected[track].valid = false;
            sources[track] = instruments[track] = nullptr;
            clips[track] = nullptr;
        }
        was_active = false;
        force_seek = true;
        stock<void(u32, u32, u32)>(0x8d598040)(controller, delta, flag);
        return;
    }
    sdk_arranger_bounce_dispatch(controller);
    if (!sdk_arranger_audio_active()) {
        if (bounce_active) sdk_arranger_bounce_end();
        transport_handled = transport_sequence;
        if (was_active) {
            close_recorded_notes();
            for (u32 track = 0; track < 8; ++track) {
                release_track(controller, track);
                stock<void(u32)>(0x8d5ac024)(controller + 0x108 + track * 0x290);
                selected[track].valid = false;
                sources[track] = instruments[track] = nullptr;
                stock<void(u32, u32)>(0x8d597730)(controller, track);
            }
        }
        was_active = false;
        record_owner = 0;
        record_status = 0;
        record_lane = 8;
        discard_record_queue();
        stock<void(u32, u32, u32)>(0x8d598040)(controller, delta, flag);
        return;
    }
    record_owner = 1;
    if (bounce_active) transport_handled = transport_sequence;
    const u32 action = consume_transport_command(before_publish, controller);
    if (action < 3) {
        const bool playing = byte(controller + 0x305d) || byte(controller + 0x305e);
        if (!action || (action == 1 && !playing)) sdk_arranger_start(controller);
        else if (action == 2) { close_recorded_notes(); force_seek = true; }
    }
    if (!byte(controller + 0x305d) && !byte(controller + 0x305e)) {
        // Clock service continues while stopped. Accept seeks, but do not advance
        // the arranger until stock has enabled either playback state.
        prepare_sources();
        close_recorded_notes();
        drain_record_queue();
        update_record_status(controller);
        stock<void(u32, u32, u32)>(0x8d598040)(controller, delta, flag);
        return;
    }
    if (!was_active) force_seek = true;
    was_active = true;
    u32 slices = 0;
    while (delta && slices++ < 64) {
        const opxy::arranger::Model& model = sdk_arranger_audio_model();
        if (timeline_loop() && model.position() >= model.loopEnd()) {
            sdk_arranger_audio_advance(0);
            force_seek = true;
        }
        prepare_sources();
        for (u32 track = 0; track < 8; ++track)
            prepare_track(controller + 0x108 + track * 0x290, track);
        drain_record_queue();
        const u32 step = slice_length(delta);
        const u32 before = sdk_arranger_audio_model().position();
        const bool wraps = timeline_loop() && model.loopEnd() - before == step;
        dispatch_step = step;
        dispatching = true;
        stock<void(u32, u32, u32)>(0x8d598040)(controller, step, flag);
        dispatching = false;
        advance_timeline(step);
        force_seek = wraps;
        delta -= step;
        if (bounce_active && sdk_arranger_audio_model().position() >= bounce_limit) {
            stock<void(u32)>(0x8d597d00)(controller);
            bounce_running = 0;
            delta = 0;
        }
    }
    if (delta) {
        close_recorded_notes();
        // A clock discontinuity must not replay an unbounded backlog on audio.
        // Close old gates once and resume at the resulting cursor next callback.
        for (u32 track = 0; track < 8; ++track) {
            release_track(controller, track);
            stock<void(u32)>(0x8d5ac024)(controller + 0x108 + track * 0x290);
            selected[track].valid = false;
        }
        advance_timeline(delta);
        prepare_sources();
        force_seek = true;
    }
    update_record_status(controller);
}

extern "C" void sdk_arranger_advance_phase(u32, u32) __asm__("sdk_arranger_advance_phase");
extern "C" void sdk_arranger_advance_phase(u32 object, u32 raw_delta) {
    using namespace opxy::platform;
    const u32 track = byte(object + 0x194);
    if (dispatching && sdk_arranger_audio_active() && track < 8 && sources[track]) {
        const opxy::arranger::Model& model = sdk_arranger_audio_model();
        const opxy::arranger::Clip* clip = clips[track];
        const bool clip_end = clip && clip->start + clip->length - model.position() == dispatch_step;
        const bool loop_end = timeline_loop() && model.position() < model.loopEnd() &&
            model.loopEnd() - model.position() == dispatch_step;
        const bool bounce_end = bounce_active && bounce_limit - model.position() == dispatch_step;
        // Stock's event interval includes its upper endpoint. Clip regions do not.
        if ((clip_end || loop_end || bounce_end) && raw_delta) --raw_delta;
        const u32 period = u32(sources[track][0]) * 480 * 1680;
        const u32 phase = (word(object + 0x198) + raw_delta) % period;
        // Use stock's existing interval-processing flags. Wrapping here keeps
        // its next tick out of the global-clock retiming path and retains queues.
        // The fifth flag edits/deletes source notes; playback must leave it off.
        stock<void(u32, u32, u32, u32, u32)>(0x8d5ac4d8)(object, phase, 1, 0, 0);
        return;
    }
    stock<void(u32, u32)>(0x8d5ab82c)(object, raw_delta);
}

extern "C" void sdk_arranger_scene_advance(u32) __asm__("sdk_arranger_scene_advance");
extern "C" void sdk_arranger_scene_advance(u32 controller) {
    if (!dispatching || !sdk_arranger_audio_active())
        opxy::platform::stock<void(u32)>(0x8d597e10)(controller);
}

extern "C" void sdk_arranger_stock_start(u32) __asm__("sdk_arranger_stock_start");
bool sdk_arranger_bounce_begin(u32 track, u32 start, u32 end) {
    using namespace opxy::platform;
    if (bounce_active || track >= 8 || start >= end || !dispatch_controller ||
        !sdk_arranger_audio_active() || !project_matches()) return false;
    u32 phase;
    const opxy::arranger::Model& model = sdk_arranger_audio_model();
    const opxy::arranger::Clip* clip = model.active(static_cast<u8>(track), start, phase);
    const u8* project = at<const u8>(word(0x840a0524));
    const u8* source = clip ? arranger_pattern_source(project, track, clip->pattern, false) : nullptr;
    if (!clip || end > clip->start + clip->length || !source || !source[0] || source[8] >= 16) return false;
    bounce_position = model.position();
    bounce_was_playing = byte(dispatch_controller + 0x305d) || byte(dispatch_controller + 0x305e);
    bounce_generation = project_generation();
    bounce_project = word(0x840a0524);
    close_recorded_notes();
    stock<void(u32)>(0x8d597d00)(dispatch_controller);
    discard_record_queue();
    record_lane = 8;
    bounce_track = track;
    bounce_limit = end;
    bounce_active = true;
    bounce_started = false;
    bounce_running = 0;
    transport_handled = transport_sequence;
    for (u32 lane = 0; lane < 8; ++lane) selected[lane].valid = false;
    sdk_arranger_audio_seek(start);
    prepare_sources();
    force_seek = true;
    barrier();
    return true;
}

bool sdk_arranger_bounce_start() {
    using namespace opxy::platform;
    if (!bounce_active || bounce_started || !project_matches() ||
        bounce_generation != project_generation() || bounce_project != word(0x840a0524)) return false;
    transport_handled = transport_sequence;
    sdk_arranger_start(dispatch_controller);
    bounce_started = true;
    bounce_running = 1;
    barrier();
    return true;
}

void sdk_arranger_bounce_stop() {
    using namespace opxy::platform;
    if (!bounce_active) return;
    const bool valid = project_matches() && bounce_generation == project_generation() &&
        bounce_project == word(0x840a0524);
    if (valid) {
        if (bounce_running && !byte(dispatch_controller + 0x305d) &&
            !byte(dispatch_controller + 0x305e)) bounce_was_playing = false;
        close_recorded_notes();
        stock<void(u32)>(0x8d597d00)(dispatch_controller);
    }
    bounce_running = 0;
    for (u32 track = 0; track < 8; ++track) selected[track].valid = false;
    force_seek = true;
    barrier();
}

void sdk_arranger_bounce_end() {
    using namespace opxy::platform;
    if (!bounce_active) return;
    const bool valid = project_matches() && bounce_generation == project_generation() &&
        bounce_project == word(0x840a0524);
    const bool resume = valid && sdk_arranger_audio_active() && bounce_was_playing && (!bounce_running ||
        byte(dispatch_controller + 0x305d) || byte(dispatch_controller + 0x305e));
    if (valid) stock<void(u32)>(0x8d597d00)(dispatch_controller);
    bounce_running = 0;
    bounce_active = false;
    bounce_started = false;
    for (u32 track = 0; track < 8; ++track) selected[track].valid = false;
    force_seek = true;
    if (valid) {
        sdk_arranger_audio_seek(bounce_position);
        prepare_sources();
        if (resume) sdk_arranger_start(dispatch_controller);
    }
    barrier();
}

bool sdk_arranger_bounce_transport_running() {
    opxy::platform::barrier();
    return bounce_running != 0;
}

extern "C" void sdk_arranger_record_note(u32, u32, u32, u32) __asm__("sdk_arranger_record_note");
extern "C" void sdk_arranger_record_note(u32 controller, u32 pitch, u32 track, u32 velocity) {
    using namespace opxy::platform;
    u32 pattern = 0xffffffffu;
    const bool gui = stock<u32()>(0x8d5c3eac)() && !sequencer_context();
    const bool gui_target = gui && sdk_arranger_record_target(track, &pattern);
    if (!record_owner && !gui_target) {
        stock<void(u32, u32, u32, u32)>(0x8d593c9c)(controller, pitch, track, velocity);
        return;
    }
    if (track >= 8 || pitch > 127 || velocity > 127 || (gui && !gui_target)) return;
    const RecordEvent event = {project_generation(), controller, track, pitch, velocity, pattern, 1};
    queue_record(event);
}

extern "C" void sdk_arranger_record_off(u32, u32, u32) __asm__("sdk_arranger_record_off");
extern "C" void sdk_arranger_record_off(u32 state, u32 track, u32 pitch) {
    using namespace opxy::platform;
    u32 pattern;
    const bool gui = stock<u32()>(0x8d5c3eac)() && !sequencer_context();
    if (!releasing && (record_owner || (gui && sdk_arranger_record_target(track, &pattern)))) {
        queue_record(RecordEvent{project_generation(), 0, track, pitch, 0, 0, 0});
        return;
    }
    stock<void(u32, u32, u32)>(0x8d5904e2)(state, track, pitch);
}

extern "C" const u8* sdk_arranger_record_indices() __asm__("sdk_arranger_record_indices");
extern "C" const u8* sdk_arranger_record_indices() {
    using namespace opxy::platform;
    const u8* original = stock<const u8*()>(0x8d5841e4)();
    const u32 role = sequencer_context() ? 0 : 1;
    static u8 indices[2][16];
    for (u32 track = 0; track < 16; ++track) indices[role][track] = original[track];
    const u8* project = at<const u8>(word(0x840a0524));
    for (u32 track = 0; track < 8; ++track) {
        const u8* source = pattern_source(track);
        if (!source) continue;
        const u8* pool = project + 0xd7c + track * 0x53f44;
        const __UINTPTR_TYPE__ delta = reinterpret_cast<__UINTPTR_TYPE__>(source) -
            reinterpret_cast<__UINTPTR_TYPE__>(pool);
        if (!(delta % 0x53f4) && delta / 0x53f4 < 16)
            indices[role][track] = static_cast<u8>(delta / 0x53f4);
    }
    return indices[role];
}

bool sdk_arranger_extension_target(u32 track) {
    if (track >= 8) return false;
    if (!sdk_arranger_audio_active() && !sdk_arranger_project_linear()) return true;
    const u8* source = pattern_source(track);
    if (!source) return false;
    if (!sequencer_context()) return true; // Valid explicit GUI editor source.
    // Capture can run before the next dispatcher slice refreshes cached sources.
    // Reject a gap or stale boundary selection instead of falling back to Song.
    u32 phase;
    const opxy::arranger::Model& model = sdk_arranger_audio_model();
    const opxy::arranger::Clip* clip = model.active(static_cast<u8>(track), model.position(), phase);
    return clip && !clip->muted && source == opxy::platform::arranger_pattern_source(
        opxy::platform::at<const u8>(opxy::platform::word(0x840a0524)), track, clip->pattern, false);
}

extern "C" void sdk_arranger_record_changed(u32) __asm__("sdk_arranger_record_changed");
extern "C" void sdk_arranger_record_changed(u32 track) {
    using namespace opxy::platform;
    const u8* source = pattern_source(track);
    if (!source) { stock<void(u32)>(0x8d587668)(track); return; }
    // Exact effects of stock587668, with the same source as the recorded notes.
    const_cast<u8*>(source)[0x14] &= static_cast<u8>(~8u);
    *at<u8>(0x840a052c) = 1;
    ++*at<u32>(0x840a0530);
}

bool sdk_arranger_request_start(u32 position, bool resume, bool record_held) {
    return publish_transport_command(position, resume ? 1 : 0, record_held);
}
bool sdk_arranger_request_seek(u32 position) { return publish_transport_command(position, 2); }
bool sdk_arranger_request_record(u32 track, u32 pattern) {
    if (track >= 8 || (pattern >= 16 && pattern != 0xffffffffu)) return false;
    return queue_record(RecordEvent{project_generation(), 0, track, 0, 0, pattern, 2});
}
u32 sdk_arranger_record_status() {
    opxy::platform::barrier();
    return record_status;
}
extern "C" void sdk_arranger_start(u32) __asm__("sdk_arranger_start");
extern "C" void sdk_arranger_start(u32 controller) {
    if (record_owner && project_matches()) close_recorded_notes();
    const u32 stopped_position = sdk_arranger_audio_model().position();
    sdk_arranger_consume();
    const bool active = sdk_arranger_audio_active() && project_matches();
    consume_transport_command(stopped_position, controller);
    if (active) { record_owner = 1; prepare_sources(); }
    sdk_arranger_stock_start(controller);
    force_seek = true;
}
