// Exercise the actual scheduler against a bounded stock-call model, not hardware.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "stock.hpp"
#include "arranger_state.hpp"
void host_barrier() {}
#include <arranger_state.cpp>
void* fake_address(opxy::u32);
void* fake_memory(opxy::u32);
namespace opxy { namespace platform {
template<class F> F* fake_stock(u32 address) { return reinterpret_cast<F*>(fake_address(address)); }
template<class T> T* fake_at(Address address) { return static_cast<T*>(fake_memory(static_cast<u32>(address))); }
u32 fake_word(u32 address);
u32 fake_byte(u32 address);
}}
#define stock fake_stock
#define at fake_at
#define word fake_word
#define byte fake_byte
#include <arranger_transport.cpp>
#undef stock
#undef at
#undef word
#undef byte

namespace test {
const u32 project_address = 0x10000, controller = 0x10000000;
std::vector<u8> project(0x54076c), control(0x9000);
u8 recorder_memory[0x1000] = {}, globals[32] = {};
u32 role = 1, task = 1, requested_pattern = 0xffffffffu, recorded, finished;
bool record_target = true, project_linear = false;
const u8* gui_edit_source;
bool inject_publish_note, inject_record_off;
u32 callback_depth, max_callback_depth;
u32 stops, bounce_callbacks;
bool cancel_bounce_on_dispatch;
u32 generation = 1, dispatches, starts, scene_changes, queued_fires, resets, global_retimes;
opxy::i64 emitted_phase;
struct Tick { u32 track, phase, amount; const u8* pattern; };
std::vector<Tick> ticks;
void require(bool ok, const char* why) { if (!ok) { std::puts(why); std::exit(1); } }
void put(u32 address, u32 value) { std::memcpy(fake_memory(address), &value, 4); }
u32 get(u32 address) { u32 value; std::memcpy(&value, fake_memory(address), 4); return value; }
u32 track_object(u32 track) { return controller + 0x108 + track * 0x290; }
u32 gui_role() { return role; }
u32 current_task() { return task; }
u32 get_generation() { return generation; }
void reset(u32 object) { ++resets; put(object + 0x190, 0); put(object + 0x198, 0); }
void publish(u32 address, u32 track) {
    if (!inject_publish_note || track) return;
    ++callback_depth;
    if (callback_depth > max_callback_depth) max_callback_depth = callback_depth;
    require(callback_depth < 4, "record input must not recursively republish its track");
    sdk_arranger_record_note(address, 65, track, 100);
    --callback_depth;
} // Stock publication does not clear the delayed queue.
void set_phase(u32 object, u32 phase, u32 previous, u32 brain, u32 destructive_edit) {
    // Native fifth argument runs 5ab858, unlinking notes and decrementing count.
    require(!destructive_edit, "playback must never invoke destructive note editing");
    if (previous) {
        require(previous == 1 && brain == 0, "natural advance preserves stock event flags");
        const u32 before = get(object + 0x198), period = 480 * 1680;
        emitted_phase += phase >= before ? phase - before : period - before + phase;
    }
    put(object + 0x198, phase);
}
void raw_advance(u32 object, u32 delta) {
    emitted_phase += delta;
    put(object + 0x198, get(object + 0x198) + delta);
}
void release(u32, u32, u32, u32, u32, u32) {}
void song_advance(u32) { ++scene_changes; }
void stop(u32 address) {
    ++stops;
    *static_cast<u8*>(fake_memory(address + 0x305d)) = 0;
    *static_cast<u8*>(fake_memory(address + 0x305e)) = 0;
    for (u32 track = 0; track < 8; ++track) reset(track_object(track));
}
const u8* scene_indices() { return project.data() + 0x98; }
// Verified592998 writes latch305f only on change, then clears recorder state.
void record_latch(u32 address, u32 enabled) {
    require(!opxy::platform::fake_byte(recorder) && !opxy::platform::fake_byte(recorder+1), "native latch must not enter alternate recorder mode");
    *static_cast<u8*>(fake_memory(address + 0x305f)) = static_cast<u8>(enabled);
}
void wrong_record_source(u32) { require(false, "master sequencer recording fell back to stock scene source"); }
void undo_reset(u32 address) { put(address + 0x4610, 0xffffffffu); }
void record_note(u32 address, u32 pitch, u32 track, u32 velocity) {
    ++callback_depth;
    if (callback_depth > max_callback_depth) max_callback_depth = callback_depth;
    require(callback_depth < 4, "a stock callback must not replay the in-flight record event");
    if (inject_record_off) sdk_arranger_record_off(recorder, track, pitch);
    const u8* indices = sdk_arranger_record_indices();
    put(address + 0x4610, track);
    put(address + 0x4614, indices[track]);
    const u8* pattern = sdk_arranger_pattern(track);
    require(pattern == project.data() + 0xd7c + track * 0x53f44 + indices[track] * 0x53f4,
        "record destination and undo identity agree");
    const u32 note = project_address + static_cast<u32>(pattern - project.data()) + 0x457c + 4;
    u8* payload = static_cast<u8*>(fake_memory(note));
    payload[8] = static_cast<u8>(pitch); payload[9] = static_cast<u8>(velocity); payload[10] = 2;
    put(note, get(track_object(track) + 0x198) / 1680);
    put(note + 4, 0);
    put(0x860c0134, note); put(0x860c0138, track); put(0x860c0224, 1);
    sdk_arranger_record_changed(track);
    ++recorded;
    --callback_depth;
}
void record_off(u32, u32 track, u32 pitch) {
    if (!get(0x860c0224)) return;
    const u32 note = get(0x860c0134);
    u8* payload = static_cast<u8*>(fake_memory(note));
    if (track != get(0x860c0138) || pitch != payload[8]) return;
    payload[10] &= static_cast<u8>(~2u);
    put(0x860c0224, 0); ++finished;
}
void tick(u32 object, u32 delta, u32) {
    const u32 track = opxy::platform::fake_byte(object + 0x194);
    queued_fires += get(object + 0x190);
    put(object + 0x190, 0);
    const u8* pattern = sdk_arranger_pattern(track);
    const u32 interrupted_task = task;
    task = 2; // Same master CPU, unrelated GUI task preempts the sequencer.
    require(sdk_arranger_pattern(track) == project.data() + 0xd7c + track * 0x53f44,
        "GUI preemption cannot borrow sequencer clip source");
    task = interrupted_task;
    require(sdk_arranger_pattern(track) == pattern, "sequencer source survives GUI preemption");
    if (get(object + 0x198) >= 480 * 1680) {
        // Stock wrap helper derives this from global clock, not clip position.
        ++global_retimes;
        put(object + 0x198, 37 * 1680);
    }
    const u32 before = get(object + 0x198);
    sdk_arranger_advance_phase(object, delta * 1680);
    ticks.push_back(Tick{track, before, get(object + 0x198) - before, pattern});
}
void dispatch(u32 address, u32 delta, u32 flag) {
    ++dispatches;
    if (!opxy::platform::fake_byte(address + 0x305d) && !opxy::platform::fake_byte(address + 0x305e)) return;
    sdk_arranger_scene_advance(address);
    for (u32 track = 0; track < 8; ++track) sdk_arranger_track_tick(track_object(track), delta, flag);
}
const u8* source(u32 track, u32 pattern = 0) { return project.data() + 0xd7c + track * 0x53f44 + pattern * 0x53f4; }
void setup() {
    for (u32 track = 0; track < 8; ++track) {
        const u32 pool = project_address + 0xd7c + track * 0x53f44;
        put(pool + 0x53f40, 2);
        for (u32 pattern = 0; pattern < 2; ++pattern) {
            static_cast<u8*>(fake_memory(pool + pattern * 0x53f4))[0] = 1;
            static_cast<u8*>(fake_memory(pool + pattern * 0x53f4))[8] = 3;
        }
        static_cast<u8*>(fake_memory(track_object(track) + 0x194))[0] = static_cast<u8>(track);
    }
    sdk_arranger_bind_project(generation);
}
}

void* fake_memory(u32 address) {
    if (address >= 0x860c0000 && address < 0x860c1000) return test::recorder_memory + address - 0x860c0000;
    if (address >= 0x840a0524 && address < 0x840a0544) return test::globals + address - 0x840a0524;
    if (address >= test::project_address && address - test::project_address < test::project.size())
        return test::project.data() + address - test::project_address;
    if (address >= test::controller && address - test::controller < test::control.size())
        return test::control.data() + address - test::controller;
    test::require(false, "unexpected memory address"); return nullptr;
}
namespace opxy { namespace platform {
u32 fake_word(u32 address) {
    if (address == 0x84247150) return 1;
    if (address == 0x840a0524) return test::project_address;
    return test::get(address);
}
u32 fake_byte(u32 address) { return *static_cast<u8*>(fake_memory(address)); }
}}
void* fake_address(u32 address) {
    using namespace test;
    switch (address) {
    case 0x8c03eccc: return reinterpret_cast<void*>(current_task);
    case 0x8d5c3eac: return reinterpret_cast<void*>(gui_role);
    case 0x8d584224: return reinterpret_cast<void*>(get_generation);
    case 0x8d5ac024: return reinterpret_cast<void*>(reset);
    case 0x8d597730: return reinterpret_cast<void*>(publish);
    case 0x8d5ac4d8: return reinterpret_cast<void*>(set_phase);
    case 0x8d5ab82c: return reinterpret_cast<void*>(raw_advance);
    case 0x8d595420: return reinterpret_cast<void*>(release);
    case 0x8d5abd18: return reinterpret_cast<void*>(tick);
    case 0x8d598040: return reinterpret_cast<void*>(dispatch);
    case 0x8d597e10: return reinterpret_cast<void*>(song_advance);
    case 0x8d597d00: return reinterpret_cast<void*>(stop);
    case 0x8d593c9c: return reinterpret_cast<void*>(record_note);
    case 0x8d5904e2: return reinterpret_cast<void*>(record_off);
    case 0x8d587668: return reinterpret_cast<void*>(wrong_record_source);
    case 0x8d592998: return reinterpret_cast<void*>(record_latch);
    case 0x8d591948: return reinterpret_cast<void*>(undo_reset);
    case 0x8d5841e4: return reinterpret_cast<void*>(scene_indices);
    }
    require(false, "unexpected stock call"); return nullptr;
}

extern "C" const u8* sdk_arranger_edit_source(u32, bool) { return test::gui_edit_source; }
extern "C" bool sdk_arranger_project_linear() { return test::project_linear; }
extern "C" void sdk_arranger_bounce_dispatch(u32) {
    ++test::bounce_callbacks;
    if (test::cancel_bounce_on_dispatch) sdk_arranger_bounce_end();
}
extern "C" bool sdk_arranger_record_target(u32, u32* pattern) {
    *pattern = test::requested_pattern; return test::record_target;
}
extern "C" void sdk_arranger_stock_start(u32) {
    ++test::starts;
    *static_cast<u8*>(fake_memory(test::controller + 0x305d)) = 1;
    for (u32 track = 0; track < 8; ++track) test::reset(test::track_object(track));
}
#define POINTER(name, offset) extern "C" const u8* sdk_arranger_stock_##name(u32 track) { return test::source(track) + offset; }
POINTER(pattern, 0)
POINTER(pattern_ptr58, 0x58)
POINTER(pattern_ptr38, 0x38)
POINTER(pattern_ptr5394, 0x5394)
POINTER(pattern_ptr457c, 0x457c)
POINTER(instrument_pattern, 0)
#undef POINTER
#define VALUE(name) extern "C" u32 sdk_arranger_stock_##name(u32) { return 0; }
VALUE(pattern_byte30)
VALUE(pattern_word34)
VALUE(pattern_length)
VALUE(pattern_scale)
VALUE(pattern_word4)
VALUE(pattern_byte9)
VALUE(pattern_signed10)
#undef VALUE
extern "C" u32 sdk_arranger_stock_pattern_row_mask(u32, u32) { return 0; }
extern "C" u32 sdk_arranger_stock_pattern_row_value(u32, u32, u32) { return 0; }

int main() {
    using namespace test;
    setup();
    opxy::arranger::Model& model = sdk_arranger_model();
    require(sdk_arranger_extension_target(0) && !sdk_arranger_extension_target(8), "Song Capture remains available on valid lanes");
    project_linear = true;
    require(!sdk_arranger_extension_target(0), "pending Linear activation cannot capture stock Song source");
    task = 2; gui_edit_source = source(0,1);
    require(sdk_arranger_extension_target(0), "explicit GUI clip B is a valid Capture source");
    gui_edit_source = nullptr; task = 1; project_linear = false;
    require(model.insert({0, 0, 0, 10, 0, 480}) >= 0, "first clip");
    require(model.insert({0, 1, 15, 10, 0, 480}) >= 0, "second clip");
    model.seek(5);
    sdk_arranger_publish(true, true);
    sdk_arranger_dispatch(controller, 100, 0);
    require(sdk_arranger_audio_model().position() == 5 && ticks.empty(), "stopped seek must not drift");
    sdk_arranger_start(controller);
    sdk_arranger_dispatch(controller, 12, 0);
    require(starts == 1 && sdk_arranger_audio_model().position() == 17, "start retains seek");
    require(ticks.size() == 2 && ticks[0].pattern == source(0) && ticks[1].pattern == source(0, 1), "gap admission/source routing");
    require(ticks[0].phase == 5 * 1680 && ticks[0].amount == 5 * 1680 - 1, "clip end is exclusive");
    require(ticks[1].phase == 0 && ticks[1].amount == 2 * 1680, "next clip starts at zero");
    require(scene_changes == 0, "linear mode must not advance stock scenes");
    require(sdk_arranger_extension_target(0) && !sdk_arranger_extension_target(1), "Capture resolves active B and rejects empty lane");
    const u32 muted_resets = resets, muted_ticks = static_cast<u32>(ticks.size());
    require(model.setMuted(1, true), "mute current B instance");
    put(track_object(0) + 0x190, 1);
    sdk_arranger_publish(true);
    sdk_arranger_dispatch(controller, 1, 0);
    require(resets > muted_resets && ticks.size() == muted_ticks && !queued_fires,
        "muting current instance closes gates and discards queued playback");
    require(!sdk_arranger_extension_target(0), "muted clip rejects Capture");
    const u32 closed_resets = resets;
    sdk_arranger_dispatch(controller, 1, 0);
    require(resets == closed_resets && ticks.size() == muted_ticks, "muted instance releases once, not every tick");
    require(model.setMuted(1, false), "unmute instance");
    sdk_arranger_publish(true); sdk_arranger_dispatch(controller, 1, 0);
    require(sdk_arranger_extension_target(0) && ticks.back().phase == 4 * 1680,
        "unmuted instance resumes at current timeline phase without seek");
    sdk_arranger_audio_seek(12);
    require(!sdk_arranger_extension_target(0), "Capture rejects gap before dispatcher refreshes cached B");
    sdk_arranger_audio_seek(17);
    auto* selected_source = const_cast<u8*>(source(0,1));
    selected_source[0x14] = 8; project[0xd7c+0x14] = 8;
    sdk_arranger_record_changed(0);
    require(!(selected_source[0x14]&8) && (project[0xd7c+0x14]&8), "Capture publisher marks selected B without touching A");
    put(track_object(0) + 0x190, 1);
    sdk_arranger_publish(false);
    sdk_arranger_dispatch(controller, 1, 0);
    require(queued_fires == 0, "disabling must discard queued linear notes before stock tick");
    require(scene_changes == 1, "stock scene behavior resumes");

    model = opxy::arranger::Model{};
    require(model.insert({0, 0, 0, 2000, 100, 480}) >= 0, "offset repeat clip");
    model.seek(0);
    sdk_arranger_publish(true, true);
    sdk_arranger_dispatch(controller, 1, 0);
    const u32 old_resets = resets, old_queued = queued_fires;
    const opxy::i64 old_emitted = emitted_phase;
    put(track_object(0) + 0x190, 1);
    sdk_arranger_dispatch(controller, 480, 0);
    sdk_arranger_dispatch(controller, 1, 0);
    require(global_retimes == 0, "repeat must not use global song phase");
    require(get(track_object(0) + 0x198) == 102 * 1680, "repeat preserves source offset");
    require(resets == old_resets && queued_fires == old_queued + 1, "natural repeat retains queued notes");
    require(emitted_phase - old_emitted == 481 * 1680, "repeat must process its complete interval");

    model = opxy::arranger::Model{};
    require(model.insert({0, 0, 0, 2000, 0, 480}) >= 0, "whole source period clip");
    model.seek(0);
    sdk_arranger_publish(true, true);
    const opxy::i64 before_cycle = emitted_phase;
    sdk_arranger_dispatch(controller, 480, 0);
    require(emitted_phase - before_cycle == 480 * 1680, "whole source cycle must not collapse into equal endpoints");
    require(get(track_object(0) + 0x198) == 0, "whole source cycle wraps to zero");

    model = opxy::arranger::Model{};
    require(model.insert({0, 0, 0, 1000, 0, 480}) >= 0, "loop clip");
    require(model.setLoop(true, 100, 101), "tiny loop");
    model.seek(100);
    sdk_arranger_publish(true, true);
    const u32 previous_dispatches = dispatches;
    sdk_arranger_dispatch(controller, 100000, 0);
    require(dispatches - previous_dispatches <= 64, "bounded catch-up");
    require(sdk_arranger_audio_model().position() == 100, "loop arithmetic retains overshoot");
    *static_cast<u8*>(fake_memory(controller + 0x305d)) = 0;
    require(sdk_arranger_request_start(999, false), "plain Play command accepted");
    require(sdk_arranger_request_start(321, false), "latest Play replaces an unconsumed command");
    sdk_arranger_start(controller);
    require(sdk_arranger_audio_model().position() == 321, "plain Play uses edit cursor");
    *static_cast<u8*>(fake_memory(controller + 0x305d)) = 0;
    model.seek(777);
    sdk_arranger_publish(true, true);
    require(sdk_arranger_request_start(888, true), "resume command accepted after acknowledgement");
    sdk_arranger_start(controller);
    require(sdk_arranger_audio_model().position() == 321, "Shift Play retains stopped playback despite GUI seek");
    require(sdk_arranger_request_start(222, false), "playing restart command");
    sdk_arranger_dispatch(controller, 1, 0);
    // Disable the previous tiny loop before exercising native recording.
    model = opxy::arranger::Model{};
    require(model.insert({0, 1, 0, 20, 5, 480}) >= 0, "record first source");
    require(model.insert({0, 0, 20, 20, 0, 480}) >= 0, "record next source");
    sdk_arranger_publish(true, true);
    sdk_arranger_dispatch(controller, 1, 0);
    task = 2;
    requested_pattern = 1;
    require(sdk_arranger_request_record(0, 1), "bind EDIT recording target");
    const u32 before_record = recorded;
    sdk_arranger_record_note(controller, 60, 0, 100);
    require(recorded == before_record, "GUI recording must wait for audio owner");
    task = 1;
    sdk_arranger_dispatch(controller, 1, 0);
    require(recorded == before_record + 1 && get(controller + 0x4614) == 1,
        "record selected source with matching undo identity");
    require(project[0x98] == 0, "recording never changes scene pattern");
    require(get(project_address + 0xd7c + 0x53f4 + 0x457c + 4) == 6,
        "record position uses clip offset and playback phase");
    const u32 before_finished = finished;
    sdk_arranger_dispatch(controller, 19, 0);
    require(finished == before_finished + 1 && get(0x860c0224) == 0,
        "source transition finalizes held recording before new source");
    task = 2;
    sdk_arranger_record_note(controller, 61, 0, 100);
    task = 1;
    sdk_arranger_dispatch(controller, 1, 0);
    require(recorded == before_record + 1, "EDIT target cannot record into different playback source");
    task = 2;
    requested_pattern = 0xffffffffu;
    require(sdk_arranger_request_record(0, 0xffffffffu), "bind timeline recording target");
    sdk_arranger_record_note(controller, 62, 0, 100);
    task = 1;
    sdk_arranger_dispatch(controller, 1, 0);
    require(recorded == before_record + 2, "timeline recording uses current selected-lane clip");
    task = 2;
    for (u32 index = 0; index < 65; ++index) sdk_arranger_record_off(recorder, 0, 62);
    task = 1;
    sdk_arranger_dispatch(controller, 1, 0);
    require(get(0x860c0224) == 0, "queue overflow finalizes pending notes");
    task = 2;
    require(sdk_arranger_request_record(0, 0xffffffffu), "rearm after overflow");
    sdk_arranger_record_note(controller, 63, 0, 100);
    sdk_arranger_record_off(recorder, 0, 63);
    task = 1;
    sdk_arranger_dispatch(controller, 1, 0);
    require(recorded == before_record + 3 && get(0x860c0224) == 0,
        "queued note on/off order is preserved");
    const u32 before_restart = starts;
    require(sdk_arranger_request_start(3, false), "restart while playing");
    sdk_arranger_dispatch(controller, 1, 0);
    require(starts == before_restart + 1 && sdk_arranger_audio_model().position() == 4,
        "Play while playing restarts from edit cursor");
    require(sdk_arranger_request_start(999, true), "Shift Play while playing");
    sdk_arranger_dispatch(controller, 1, 0);
    require(starts == before_restart + 1 && sdk_arranger_audio_model().position() == 5,
        "Shift Play while playing continues without resetting notes");
    *static_cast<u8*>(fake_memory(controller + 0x3060)) = 1;
    sdk_arranger_dispatch(controller, 1, 0);
    require(sdk_arranger_record_status() == 2, "recording status at valid clip");
    *static_cast<u8*>(fake_memory(controller + 0x305d)) = 0;
    task = 2;
    sdk_arranger_record_note(controller, 64, 0, 100);
    task = 1;
    sdk_arranger_dispatch(controller, 1, 0);
    require(recorded == before_record + 3 && sdk_arranger_record_status() == 1,
        "stopped recorder is armed but cannot insert queued live notes");
    require(sdk_arranger_request_start(7, false), "stopped Play without stock key forwarding");
    sdk_arranger_dispatch(controller, 1, 0);
    require(sdk_arranger_audio_model().position() == 8 && opxy::platform::fake_byte(controller + 0x305d),
        "audio must acknowledge stopped Play without a stock Start callback");
    require(sdk_arranger_request_start(99, false), "pending Play before Stop");
    *static_cast<u8*>(fake_memory(controller + 0x305d)) = 0;
    require(sdk_arranger_request_seek(2), "Stop seek replaces pending Play");
    sdk_arranger_dispatch(controller, 1, 0);
    require(sdk_arranger_audio_model().position() == 2 && !opxy::platform::fake_byte(controller + 0x305d),
        "Stop seek cannot restart playback");
    require(sdk_arranger_request_start(2, false), "restart for callback regression");
    sdk_arranger_dispatch(controller, 1, 0);
    const u32 callbacks_before = recorded, resets_before_input = resets;
    inject_publish_note = true;
    inject_record_off = true;
    force_seek = true;
    sdk_arranger_record_note(controller, 66, 0, 100);
    require(recorded == callbacks_before && resets == resets_before_input,
        "audio input admission cannot re-enter source preparation or stock recording");
    sdk_arranger_dispatch(controller, 1, 0);
    inject_publish_note = false;
    sdk_arranger_dispatch(controller, 1, 0);
    inject_record_off = false;
    require(recorded == callbacks_before + 2 && max_callback_depth == 1,
        "nested stock input/off callbacks remain bounded and process each note once");
    require(get(0x860c0224) == 0, "deferred callback note-off completes recording");
    model = opxy::arranger::Model{};
    require(model.insert({0, 0, 10, 20, 0, 480}) >= 0, "bounce lane clip");
    require(model.insert({1, 0, 10, 20, 0, 480}) >= 0, "other lane clip");
    require(model.setLoop(true, 0, 1), "bounce must ignore tiny timeline loop");
    model.seek(15);
    sdk_arranger_publish(true, true);
    sdk_arranger_consume();
    ticks.clear();
    require(sdk_arranger_bounce_begin(0, 10, 30), "begin exclusive bounce");
    sdk_arranger_dispatch(controller, 5, 0);
    require(ticks.empty() && sdk_arranger_audio_model().position() == 10 &&
        !sdk_arranger_bounce_transport_running(), "arming waits stopped for capture-ready without dropping first notes");
    require(sdk_arranger_bounce_start(), "start after first capture block acknowledgement");
    require(!sdk_arranger_bounce_start(), "capture acknowledgement cannot start twice");
    sdk_arranger_dispatch(controller, 25, 0);
    require(ticks.size() == 1 && ticks[0].track == 0, "bounce suppresses every other lane");
    require(sdk_arranger_audio_model().position() == 30 && !sdk_arranger_bounce_transport_running() &&
        !opxy::platform::fake_byte(controller + 0x305d), "bounce stops exactly at its native boundary");
    require(sdk_arranger_audio_model().loopEnabled() && sdk_arranger_audio_model().loopEnd() == 1,
        "bounce never modifies saved loop configuration");
    sdk_arranger_bounce_end();
    require(sdk_arranger_audio_model().position() == 15 && opxy::platform::fake_byte(controller + 0x305d),
        "bounce end restores prior playhead and playing state");
    require(sdk_arranger_bounce_begin(0, 10, 30) && sdk_arranger_bounce_start(), "start cancellable bounce");
    sdk_arranger_bounce_stop();
    ticks.clear();
    sdk_arranger_dispatch(controller, 8, 0);
    require(ticks.empty() && !sdk_arranger_bounce_transport_running() &&
        sdk_arranger_audio_model().position() == 10, "cancel holds exclusive stopped gate through asynchronous save/apply");
    sdk_arranger_bounce_end();
    require(sdk_arranger_audio_model().position() == 15 && opxy::platform::fake_byte(controller + 0x305d),
        "final acknowledgement restores playback only after destination job finishes");
    stop(controller);
    require(sdk_arranger_bounce_begin(0, 10, 30), "bounce from stopped state");
    sdk_arranger_bounce_end();
    require(sdk_arranger_audio_model().position() == 15 && !opxy::platform::fake_byte(controller + 0x305d),
        "cancel restores stopped state without changing project mutes");
    // Native note admission requires held REC or latched REC. REC+Play must
    // snapshot the gesture before its asynchronous Start consumes the command.
    model = opxy::arranger::Model{};
    require(model.insert({0,1,0,480,0,480}) >= 0 && model.setLoop(true,0,480), "overdub loop uses clip B while scene stays A");
    model.seek(0); sdk_arranger_publish(true,true);
    task=2; requested_pattern=0xffffffffu;
    require(sdk_arranger_request_record(0,0xffffffffu), "arm selected lane");
    *static_cast<u8*>(fake_memory(controller+0x3060))=1;
    require(sdk_arranger_request_start(0,false,true), "REC+Play captures held intent");
    *static_cast<u8*>(fake_memory(controller+0x3060))=0; // physical release before dispatch
    task=1;sdk_arranger_dispatch(controller,1,0); // Native sequencer thread runs on master, like GUI.
    require(opxy::platform::fake_byte(controller+0x305f)==1, "REC+Play remains latched after physical release");
    require(!ticks.empty() && ticks.back().track == 0 && ticks.back().pattern == source(0,1),
        "master sequencer actually plays clip B while scene remains A");
    const u32 overdub_before=recorded;
    for(u32 lap=0;lap<2;++lap) {
        if(opxy::platform::fake_byte(controller+0x305f)) sdk_arranger_record_note(controller,60+lap,0,100);
        sdk_arranger_dispatch(controller,480,0);
    }
    require(recorded==overdub_before+2&&opxy::platform::fake_byte(controller+0x305f), "record admission persists across successive loop passes");
    require(project[0x98]==0 && get(controller+0x4614)==1, "loop overdub and undo bind clip B without changing scene A");
    // A second native REC press calls592998(controller,0); its physical release
    // leaves both admission flags off. A loop boundary must not relatch it.
    record_latch(controller,0);
    sdk_arranger_dispatch(controller,480,0);
    require(!opxy::platform::fake_byte(controller+0x305f), "REC cancellation survives loop boundary");
    require(sdk_arranger_request_start(0,false,true), "rearm before plain Play test");
    sdk_arranger_dispatch(controller,1,0);
    require(sdk_arranger_request_start(0,false), "plain Play ends recording");
    sdk_arranger_dispatch(controller,1,0);
    require(!opxy::platform::fake_byte(controller+0x305f), "plain Play cannot arm recording unintentionally");
    const u32 before_rejected_start = starts;
    *static_cast<u8*>(fake_memory(recorder+1))=1;
    require(sdk_arranger_request_start(0,false,true), "REC command queued before mode check");
    sdk_arranger_dispatch(controller,0,0);
    require(!opxy::platform::fake_byte(controller+0x305f) && starts == before_rejected_start, "alternate recorder mode rejects queued latch and start");
    *static_cast<u8*>(fake_memory(recorder+1))=0;
    require(sdk_arranger_request_start(0,false,true), "REC+Play queued before Stop");
    record_latch(controller,0); stop(controller);
    require(sdk_arranger_request_seek(0), "Stop supersedes unconsumed recording start");
    sdk_arranger_dispatch(controller,1,0);
    require(!opxy::platform::fake_byte(controller+0x305f)&&!opxy::platform::fake_byte(controller+0x305d), "Stop prevents pending REC+Play from rearming or restarting");

    const u32 before_project_change = sdk_arranger_audio_model().position();
    require(sdk_arranger_request_start(456, false), "old-project command accepted before swap");
    ++generation;
    sdk_arranger_start(controller);
    require(sdk_arranger_audio_model().position() == before_project_change, "old-project Play command rejected");
    const u32 cursor = sdk_arranger_audio_model().position();
    sdk_arranger_dispatch(controller, 8, 0);
    require(sdk_arranger_audio_model().position() == cursor, "foreign project cannot use old timeline");
    require(!sdk_arranger_extension_target(0), "Capture rejects stale project binding");
    sdk_arranger_bind_project(generation);
    sdk_arranger_publish(true, true);
    sdk_arranger_consume();
    require(sdk_arranger_bounce_begin(0, 10, 30), "arm bounce before project replacement");
    const u32 stops_before_replacement = stops, callbacks_before_replacement = bounce_callbacks;
    cancel_bounce_on_dispatch = true;
    ++generation;
    sdk_arranger_dispatch(controller, 1, 0);
    require(bounce_callbacks == callbacks_before_replacement + 1 && stops == stops_before_replacement,
        "project replacement acknowledges bounce cancellation without stopping the new project");
    return 0;

}
