// Host boundary double for the real track control platform adapter: stock's
// live parameter table, its writer, the GUI tick it wraps and the displayed
// values. The machine is a scripted overlay; no machine behavior is tested.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <track_control.cpp>

namespace {
using opxy::i32;
using opxy::u32;
namespace control = opxy::control;

i32 table[control::all_tracks * control::track_parameters];
// Stock's base values, in each track's data block at +0x3864.
const u32 kBlocks = 0x90000000, kBlockBytes = 0x10000;
u32 bases[control::tracks][control::track_parameters];
u32 second_bases[control::tracks][control::track_parameters], pattern[16];
u32 engines[16], ticks, writes, resets, runs, sets, edits_seen;
// The machine holding each aux track (aux_tracks.cpp), and its ticks.
u32 aux_engines[16], aux_ticks;
control::Entry last_edit;
u32 capture, mark;
i32 seen;  // the value of track 0, parameter 1 the machine was given
u32 seen_elapsed, seen_bpm, seen_ticks, seen_playing, seen_key, seen_muted;
// The project's scene records (g_project + 0x98, 33 bytes a scene: patterns, then
// each track's flags, 0x02 muted) and the current scene (g_project + 6), at a
// stand-in address the word at 840a0524 names.
const u32 kProjectAt = 0xa0000000;
u32 project_word = kProjectAt;
unsigned char project_bytes[0x98 + 33 * 3];
unsigned char& mute_flags(u32 scene, u32 track) { return project_bytes[0x98 + scene * 33 + 16 + track]; }
// The core clock, the project tempo and the song position.
u32 cycles;
unsigned short tempo = 1200;
u32 ticks96;
unsigned char playing[2];
i32 seen_own;  // and of its own engine control 1
// Each track's LFO amount, which stock's displayed value adds.
i32 lfo[control::all_tracks][control::track_parameters];
// The scripted overlay for the next tick.
control::Entry script[8];
u32 script_count;

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
i32& value(u32 track, u32 index) { return table[track * control::track_parameters + index]; }

void stock_tick() { ++ticks; }
u32 stock_engine(u32 track) { return track < 16 ? engines[track] : 0; }
u32 telemetry25, lfo_publishes;
unsigned char cv_lfo_on;
void stock_lfo_publish(u32, u32, i32, i32) { ++lfo_publishes; }
u32 stock_telemetry(u32 id, u32 value) {
    require(id == 25, "Own LFO amount published in the wrong telemetry id");
    telemetry25 = value;
    return 1;
}
u32 stock_track_data(u32 track) { return kBlocks + (track + pattern[track] * 16) * kBlockBytes; }
void stock_param_set(u32 project, u32 track, u32 index, i32 v) {
    require(project == kProject && track < 16 && index < control::track_parameters, "Invalid set");
    ++sets;
    if (track >= control::tracks) return;
    (pattern[track] ? second_bases : bases)[track][index] = static_cast<u32>(v < 0 ? 0 : v);
    value(track, index) = v < 0 ? 0 : v;
}
void stock_live_set(u32 project, u32 track, u32 index, i32 v) {
    require(project == kProject && track < control::tracks && index < control::track_parameters && v >= 0,
            "Invalid live write");
    ++writes;
    value(track, index) = v;
}
// 8d5ac9dc: the live value plus the LFO amount, as a page shows it.
i32 stock_shown(u32 project, u32 track, u32 index, u32 flag) {
    require(project == kProject && track < control::all_tracks && index < control::track_parameters && flag == 1,
            "Invalid displayed value");
    return value(track, index) + lfo[track][index];
}
// 8d5ac910: the base value, which a stopped page shows.
i32 stock_base(u32 project, u32 track, u32 index) {
    require(project == kProject && track < control::tracks && index < control::track_parameters, "Invalid base");
    return static_cast<i32>((pattern[track] ? second_bases : bases)[track][index]);
}
// 8d4d6f14: the GUI's selected track.
u32 selected = 0;
u32 stock_selected() { return selected; }
int ready() { return 1; }
u32 page_checks;
void reset(u32) { ++resets; }
void run(u32 copy, const control::Input* input, control::Overlay* overlay) {
    require(copy == 0, "A unique machine's copy is not 0");
    ++runs;
    require(overlay->count == 0, "Overlay not cleared");
    seen = input->values[0][1];
    seen_elapsed = input->elapsed_us;
    seen_bpm = input->transport.bpm_x10;
    seen_ticks = input->transport.ticks96;
    seen_playing = input->transport.playing;
    seen_key = input->page_key;
    seen_muted = input->muted;
    seen_own = input->values[input->track][1];
    require(input->capturing == (capture_track == input->track), "Capturing not given");
    edits_seen = input->edit_count;
    if (input->edit_count) last_edit = input->edits[input->edit_count - 1];
    require(overlay->capture == 0, "Capture not cleared");
    overlay->capture = capture;
    require(overlay->mark == 0, "Mark not cleared");
    overlay->mark = mark;
    require(overlay->display[0] == 0 && overlay->display[3] == 0, "Display words not cleared");
    overlay->display[1] = 77;
    for (u32 i = 0; i < script_count; ++i) overlay->entries[overlay->count++] = script[i];
}
// Machine 41, not unique: each copy overlays its scripted entries, with its
// copy's own count of runs, and asks to capture and mark when told.
u32 other_runs[control::tracks], other_resets[control::tracks], other_capture[control::tracks];
u32 other_mark, other_edits[control::tracks];
control::Entry other_script[control::tracks][4];
u32 other_count[control::tracks];
void reset_other(u32 copy) { require(copy < control::tracks, "Copy out of range"); ++other_resets[copy]; }
void run_other(u32 copy, const control::Input* input, control::Overlay* overlay) {
    require(copy == input->track, "A copy is not its track");
    ++other_runs[copy];
    other_edits[copy] = input->edit_count;
    overlay->capture = other_capture[copy];
    overlay->mark = other_mark;
    overlay->display[0] = static_cast<i32>(100 + copy);
    for (u32 i = 0; i < other_count[copy]; ++i) overlay->entries[overlay->count++] = other_script[copy][i];
}
void plan(std::initializer_list<control::Entry> entries) {
    script_count = 0;
    for (const control::Entry& e : entries) script[script_count++] = e;
}
void tick(u32 expected_writes, const char* message) {
    writes = 0;
    sdk_gui_tick();
    require(writes == expected_writes, message);
}
}

// The hidden-pages check (synth_controls.cpp) runs once per GUI tick.
void sdk_synth_pages() { ++page_checks; }
u32 page_key;
void sdk_synth_queue_page_key(u32, u32 key) { page_key = key; }
u32 sdk_synth_page_key(u32 track) { return track == 2 ? page_key : 0; }
opxy::u32 sdk_benchmark_cycles() { return cycles; }
// The aux service's entry points, which its startup fills in on the device.
opxy::u32 aux_engine(opxy::u32 track) { return track < control::all_tracks ? aux_engines[track] : 0; }
void aux_tick() { ++aux_ticks; }
const volatile unsigned short* stock_project_tempo() { return &tempo; }

extern "C" {
const opxy::platform::ControlMachine sdk_control_machines[] = {
    {reinterpret_cast<const char*>(40), ready, reset, run, nullptr, 0, nullptr, nullptr, true, 1u << 11, nullptr, {}, true},
    {reinterpret_cast<const char*>(41), ready, reset_other, run_other, nullptr, 0, nullptr, nullptr, false, 0, nullptr, {}},
    {nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, false, 0, nullptr, {}}};
const opxy::u32 sdk_control_machine_count = 2;
}

opxy::platform::Address opxy::platform::host_stock_address(opxy::platform::Address address) {
    if (address >= kLive && address < kLive + sizeof(table))
        return reinterpret_cast<Address>(table) + (address - kLive);
    if (address == 0x8d580074) return reinterpret_cast<Address>(&stock_tick);
    if (address == 0x8d4d68b8) return reinterpret_cast<Address>(&stock_lfo_publish);
    if (address == 0x8d4d666a) return reinterpret_cast<Address>(&stock_telemetry);
    if (address == 0x080376e4 + 25 * 4) return reinterpret_cast<Address>(&telemetry25);
    if (address == kBlocks + 11 * kBlockBytes + 0x24) return reinterpret_cast<Address>(&cv_lfo_on);
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&stock_engine);
    if (address == 0x8d5acea8) return reinterpret_cast<Address>(&stock_live_set);
    if (address == 0x8d5acf28) return reinterpret_cast<Address>(&stock_param_set);
    if (address == 0x8d587708) return reinterpret_cast<Address>(&stock_track_data);
    if (address == 0x8d5ac9dc) return reinterpret_cast<Address>(&stock_shown);
    if (address == 0x8d5ac910) return reinterpret_cast<Address>(&stock_base);
    if (address == 0x8d4d6f14) return reinterpret_cast<Address>(&stock_selected);
    if (address == 0x8d584210) return reinterpret_cast<Address>(&stock_project_tempo);
    if (address == 0x86bd0080) return reinterpret_cast<Address>(&ticks96);
    if (address == 0x860c9b39) return reinterpret_cast<Address>(&playing[0]);
    if (address == 0x860c9b3a) return reinterpret_cast<Address>(&playing[1]);
    if (address == 0x840a0524) return reinterpret_cast<Address>(&project_word);
    if (address >= kProjectAt && address < kProjectAt + sizeof(project_bytes))
        return reinterpret_cast<Address>(project_bytes) + (address - kProjectAt);
    if (address >= kBlocks && address < kBlocks + 32 * kBlockBytes) {
        const u32 block = (address - kBlocks) / kBlockBytes, track = block % 16;
        const u32 offset = (address - kBlocks) % kBlockBytes;
        if (offset == 0x18) return reinterpret_cast<Address>(&engines[track]);
        const u32 at = offset - 0x3864;
        if (track < control::tracks && at < sizeof(bases[0]))
            return reinterpret_cast<Address>(&(block >= 16 ? second_bases : bases)[track][at / 4]);
    }
    std::abort();
}

int main() {
    value(0, 1) = 100;
    value(3, 9) = 7;
    plan({{0, 1, 0, 500}});
    tick(0, "Overlay written without a machine");
    require(ticks == 1 && runs == 0 && value(0, 1) == 100 && page_checks == 1, "Stock tick not delegated alone");

    engines[2] = 40;
    // Own track and out-of-range entries are ignored; the later duplicate wins.
    plan({{0, 1, 0, 300}, {2, 0, 0, 9}, {9, 1, 0, 9}, {0, 58, 0, 9}, {0, 1, 0, 500}});
    tick(1, "Overlay not written once per parameter");
    require(runs == 1 && resets == 1 && seen == 100 && value(0, 1) == 500 && value(2, 0) == 0, "Overlay not applied");
    tick(0, "Unchanged overlay rewritten");
    require(seen == 100 && resets == 1, "Machine given the overlay instead of stock's value");

    // Muting the machine's track in the current scene releases its overlays; it
    // still runs and is told. Another scene's mute, or another flag, changes nothing.
    mute_flags(1, 2) = 0x02;
    mute_flags(0, 2) = 0x01;
    tick(0, "Another scene's mute or flag applied");
    require(seen_muted == 0 && value(0, 1) == 500, "Unmuted machine's overlay lost");
    mute_flags(0, 2) = 0x03;
    const u32 muted_runs = runs;
    tick(1, "Muted machine's overlay not released");
    require(seen_muted == 1 && runs == muted_runs + 1 && value(0, 1) == 100, "Muted machine not run or not released");
    project_bytes[6] = 1;
    tick(0, "Overlay written in another scene that mutes the track");
    require(seen_muted == 1 && value(0, 1) == 100, "Scene 2's mute not applied");
    project_bytes[6] = 0;
    mute_flags(0, 2) = 0x01;
    mute_flags(1, 2) = 0;
    tick(1, "Unmuted machine's overlay not written again");
    require(seen_muted == 0 && value(0, 1) == 500, "Unmuted machine's overlay not back");

    // A stock edit under the overlay becomes the value beneath it.
    value(0, 1) = 120;
    tick(1, "Overlay not restored over a stock edit");
    require(seen == 120 && value(0, 1) == 500, "Stock edit not seen");

    // Dropping a parameter restores stock's value; negatives clamp to 0.
    plan({{3, 9, 0, -5}});
    tick(2, "Release and new overlay not written");
    require(value(0, 1) == 120 && value(3, 9) == 0, "Release not restored");

    // A stock write in the same tick as the release is kept.
    value(3, 9) = 11;
    plan({});
    tick(0, "Stock's own write overwritten on release");
    require(value(3, 9) == 11, "Stock write lost");

    // Moving the machine resets it; removing it restores everything.
    plan({{0, 1, 0, 600}});
    tick(1, "Overlay not written");
    engines[2] = 0;
    engines[5] = 40;
    tick(0, "Same overlay rewritten after a move");
    require(resets == 2, "Moved machine not reset");
    engines[5] = 0;
    tick(1, "Overlay not restored when the machine left");
    require(value(0, 1) == 120 && ticks == 13, "Removal not restored");

    // Edits pass through untouched while nothing captures.
    bases[0][5] = 50;
    sdk_param_edit_set(kProject, 0, 5, 70);
    require(sets == 1 && bases[0][5] == 70, "Edit not delegated");
    bases[0][5] = 50;

    // Capture: edits of other tracks still reach stock and arrive next tick;
    // the host's own and other tracks' edits are not captured.
    engines[2] = 40;
    capture = 1;
    plan({});
    tick(0, "Capture wrote an overlay");
    sdk_param_edit_set(kProject, 0, 5, 900);
    sdk_param_edit_set(kProject, 2, 0, 5);
    sdk_param_edit_set(kProject, 9, 0, 5);
    require(sets == 4 && bases[0][5] == 900, "Captured edit not delegated");
    tick(0, "Capture wrote an overlay");
    require(edits_seen == 1 && last_edit.track == 0 && last_edit.index == 5 && last_edit.value == 900,
            "Edit not captured");
    sdk_param_edit_set(kProject, 0, 5, -3);
    tick(0, "Capture wrote an overlay");
    require(edits_seen == 1 && last_edit.value == 0, "Second edit not captured");
    tick(0, "Capture wrote an overlay");
    require(edits_seen == 0, "Edits delivered twice");
    // Ending the capture sets the base from before the first edit back.
    capture = 0;
    sets = 0;
    tick(0, "End of capture wrote an overlay");
    require(sets == 1 && bases[0][5] == 50 && value(0, 5) == 50, "Capture not restored");
    sdk_param_edit_set(kProject, 0, 5, 60);
    tick(0, "Uncaptured edit wrote an overlay");
    require(edits_seen == 0 && bases[0][5] == 60, "Edit captured after the capture ended");

    // The machine's own engine controls arrive as its page shows them.
    value(2, 1) = 50;
    lfo[2][1] = 1000;
    tick(0, "Own LFO wrote an overlay");
    require(seen_own == 1050, "Own LFO not included");

    // Pages show an overlay playing or stopped; other parameters as stock does.
    plan({{0, 1, 0, 500}});
    tick(1, "Overlay not written");
    bases[0][2] = 30;
    value(0, 2) = 40;
    require(sdk_param_shown(kProject, 0, 1, 0) == 500 && sdk_param_shown(kProject, 0, 1, 1) == 500,
            "Overlay not shown");
    require(sdk_param_shown(kProject, 0, 2, 0) == 30 && sdk_param_shown(kProject, 0, 2, 1) == 40,
            "Stock value not shown");
    // Turning an overlaid parameter shows its base value for a while.
    sdk_param_edit_set(kProject, 0, 1, 80);
    for (u32 i = 0; i < kTurnTicks; ++i) {
        require(sdk_param_shown(kProject, 0, 1, 1) == 80, "Turned base not shown");
        tick(i == 0 ? 1 : 0, "Overlay not restored once over the turn");
    }
    require(sdk_param_shown(kProject, 0, 1, 1) == 500, "Overlay not shown after the turn");

    // A capture that ends under an overlay leaves stock's restored value beneath it.
    bases[0][6] = 10;
    value(0, 6) = 10;
    capture = 1;
    plan({{0, 6, 0, 800}});
    tick(2, "Release and new overlay not written");
    sdk_param_edit_set(kProject, 0, 6, 300);
    tick(1, "Overlay not restored over the captured edit");
    capture = 0;
    tick(1, "Overlay not restored over the end of the capture");
    plan({});
    tick(1, "Release not written");
    require(value(0, 6) == 10 && bases[0][6] == 10, "Edit from the capture restored under the overlay");

    // Two copies of the machine: neither runs, and its overlay ends as if it
    // had left, until one copy is removed.
    const i32 own = value(0, 1);
    plan({{0, 1, 0, 700}});
    tick(1, "Overlay not written");
    engines[6] = 40;
    require(sdk_control_copies(2) == 2 && sdk_control_copies(6) == 2 && sdk_control_copies(16) == 0,
            "Copies not counted");
    const u32 ran = runs;
    tick(1, "Overlay not released with two copies");
    require(runs == ran && value(0, 1) == own, "Two copies ran");
    engines[6] = 0;
    tick(1, "Overlay not written once one copy is left");
    require(runs == ran + 1 && value(0, 1) == 700 && sdk_control_copies(2) == 1, "Single copy not run again");

    // Each tick carries the time since the last, from the 400 MHz core clock
    // (0 after a reset, at most a second), and the tempo and song position.
    cycles += 400 * 2500;
    ticks96 = 480;
    playing[1] = 1;
    tick(0, "Timing rewrote the overlay");
    require(seen_elapsed == 2500 && seen_bpm == 1200 && seen_ticks == 480 && seen_playing == 1, "Time not given");
    cycles += 400u * 5000000u;
    playing[1] = 0;
    tick(0, "Timing rewrote the overlay");
    require(seen_elapsed == 1000000 && seen_playing == 0, "Elapsed time not capped");
    // A hidden page key pressed on the host's track reaches the machine.
    page_key = 4;
    tick(0, "Page key rewrote the overlay");
    require(seen_key == 4, "Page key not given");
    page_key = 0;

    // What the control passes to its display, for the selected track.
    selected = 2;
    require(sdk_control_display(1) == 77 && sdk_control_display(0) == 0 && sdk_control_display(9) == 0,
            "Display words not kept");

    // Marks over every page: an overlaid selected track once the machine asks
    // for it, then a capture whether or not it asks.
    selected = 0;
    require(opxy::platform::control_marks().kind == 0, "Overlaid track marked unasked");
    mark = 1;
    tick(0, "Mark rewrote the overlay");
    require(opxy::platform::control_marks().kind == opxy::platform::kMarkOverlaid &&
            opxy::platform::control_marks().machine == 0, "Overlaid track not marked");
    selected = 3;
    require(opxy::platform::control_marks().kind == 0, "Track without overlay marked");
    capture = 1;
    tick(0, "Capture rewrote the overlay");
    require(opxy::platform::control_marks().kind == opxy::platform::kMarkCapture, "Capture not marked");
    capture = 0;
    mark = 0;
    tick(0, "End of capture rewrote the overlay");
    require(opxy::platform::control_marks().kind == 0, "Mark kept after the machine stopped asking");

    // Several machines: machine 40 on track 2 and two copies of machine 41,
    // which is not unique, on tracks 4 and 6. Every one runs, each copy with
    // its own state, and each display reads its own track's words.
    plan({{0, 1, 0, 500}});
    tick(1, "Overlay not written");
    const i32 before = value(0, 2);
    engines[4] = 41;
    engines[6] = 41;
    other_count[4] = 1;
    other_script[4][0] = control::Entry{0, 2, 0, 222};
    tick(1, "Second machine's overlay not written");
    require(other_runs[4] == 1 && other_runs[6] == 1 && other_resets[4] == 1 && other_resets[6] == 1 &&
            value(0, 1) == 500 && value(0, 2) == 222, "Machines not run together");
    selected = 6;
    require(sdk_control_display(0) == 106, "Display words not the selected track's");
    selected = 2;
    require(sdk_control_display(1) == 77 && sdk_control_display(0) == 0, "First machine's words lost");
    // Two machines on one parameter: the later track's wins, and its marks show.
    other_count[6] = 1;
    other_script[6][0] = control::Entry{0, 1, 0, 666};
    tick(1, "Contested parameter not written once");
    require(value(0, 1) == 666 && sdk_param_shown(kProject, 0, 1, 1) == 666, "Later track did not win");
    other_mark = 1;
    tick(0, "Mark rewrote the overlay");
    selected = 0;
    require(opxy::platform::control_marks().kind == opxy::platform::kMarkOverlaid &&
            opxy::platform::control_marks().machine == 1, "Winner's marks not shown");
    // The later machine gives the parameter up: the earlier one's comes back.
    other_count[6] = 0;
    other_mark = 0;
    tick(1, "Earlier overlay not written back");
    require(value(0, 1) == 500, "Earlier machine's overlay not restored");
    // One capture at a time: the first asking starts, and keeps it while it asks.
    other_capture[6] = 1;
    tick(0, "Capture rewrote the overlay");
    require(capture_track == 6, "First asking copy not capturing");
    capture = 1;
    tick(0, "Capture rewrote the overlay");
    require(capture_track == 6, "Capture taken from the copy capturing");
    sdk_param_edit_set(kProject, 0, 9, 40);
    tick(0, "Capture rewrote the overlay");
    require(other_edits[6] == 1 && edits_seen == 0, "Edits not given to the capturing copy alone");
    other_capture[6] = 0;
    tick(0, "Capture handover rewrote the overlay");
    require(capture_track == 2, "Capture not handed to the next asking");
    require(opxy::platform::control_marks().kind == opxy::platform::kMarkCapture &&
            opxy::platform::control_marks().machine == 0, "Capture marks not the capturing machine's");
    capture = 0;
    tick(0, "End of capture rewrote the overlay");
    // A copy leaving ends its overlay; the others carry on.
    engines[4] = 0;
    tick(1, "Departed copy's overlay not released");
    require(value(0, 2) == before && value(0, 1) == 500, "Departed copy not released alone");
    engines[6] = 0;

    // Without the aux service, aux tracks hold nothing; with it, its entry points answer.
    aux_engines[11] = 40;
    require(opxy::platform::control_engine(11) == 0 && sdk_control_copies(11) == 0, "Aux track held without the service");
    aux_engines[11] = 0;
    sdk_aux.engine = aux_engine;
    sdk_aux.tick = aux_tick;

    // A machine holding an aux track runs there after the instrument tracks,
    // reads its own controls from that track, and counts as a copy as it would
    // on an instrument track.
    engines[2] = 0;
    tick(1, "Overlay not released when the machine left");
    const i32 stock_value = value(0, 1);
    aux_engines[11] = 40;
    value(11, 1) = 33;
    lfo[11][1] = 0;
    plan({{0, 1, 0, 400}, {11, 2, 0, 9}});
    const u32 aux_resets = resets, aux_before = aux_ticks;
    tick(1, "Aux host's overlay not written");
    require(resets == aux_resets + 1 && seen_own == 33 && value(0, 1) == 400 && value(11, 2) == 0 &&
            sdk_control_copies(11) == 1 && aux_ticks == aux_before + 1, "Aux host not run");
    selected = 11;
    require(sdk_control_display(1) == 77, "Aux host's display words not kept");

    // The CV track's own LFO: published in id 25, and shown in place of the shared
    // pair while its LFO is on, for the slot it targets.
    require(sdk_lfo_held_amount(11, 1, 5) == 5, "Nothing published yet, but stock's amount replaced");
    require(sdk_lfo_held_amount(3, 1, 5) == 5, "Another track's amount replaced");
    // The aux service publishes this word; its publisher has its own fixture.
    telemetry25 = 0xab012000;
    require(sdk_lfo_held_amount(11, 1, 5) == 0, "LFO off, but an amount shown");
    cv_lfo_on = 1;
    require(sdk_lfo_held_amount(11, 1, 5) == 0x20000000, "Own amount not shown");
    require(sdk_lfo_held_amount(11, 2, 5) == 0, "Own amount shown on another slot");
    telemetry25 = 0xab02e000;
    require(sdk_lfo_held_amount(11, 2, 5) == -0x20000000, "Negative amount not kept");
    cv_lfo_on = 0;
    engines[2] = 40;
    require(sdk_control_copies(11) == 2 && sdk_control_copies(2) == 2, "Aux and instrument copies not counted");
    tick(1, "Overlay not released with two copies");
    require(value(0, 1) == stock_value, "Two copies ran");
    engines[2] = 0;
    aux_engines[11] = 0;
    tick(0, "Nothing to release");
    require(sdk_control_copies(11) == 0, "Released aux track still counted");

    // Capture restores the edited pattern, never the newly playing one.
    aux_engines[11] = 40;
    for (u32 s = 0; s < 3; ++s) mute_flags(s, 11) = 0;
    plan({}); capture = 1;
    sdk_gui_tick();
    bases[0][1] = 100; value(0, 1) = 100;
    sdk_param_edit_set(kProject, 0, 1, 150);
    pattern[0] = 1;
    second_bases[0][1] = 700; value(0, 1) = 700;
    capture = 0;
    sdk_gui_tick();
    require(bases[0][1] == 100 && second_bases[0][1] == 700 && value(0, 1) == 700,
            "Capture restored into a different pattern");

    // Both patterns may be edited in one capture and restore independently.
    capture = 1; sdk_gui_tick();
    sdk_param_edit_set(kProject, 0, 1, 750);
    pattern[0] = 0; value(0, 1) = 100;
    sdk_param_edit_set(kProject, 0, 1, 200);
    capture = 0; sdk_gui_tick();
    require(bases[0][1] == 100 && second_bases[0][1] == 700 && value(0, 1) == 100,
            "Capture confused two patterns on the same track");

    // A preset/external write supersedes the edit, even in the same pattern.
    capture = 1; sdk_gui_tick();
    sdk_param_edit_set(kProject, 0, 1, 150);
    bases[0][1] = 900; value(0, 1) = 900;
    capture = 0; sdk_gui_tick();
    require(bases[0][1] == 900 && value(0, 1) == 900, "Capture overwrote a later base write");

    capture = 1; sdk_gui_tick();
    sdk_param_edit_set(kProject, 0, 1, 950);
    bases[0][1] = 800; value(0, 1) = 800;
    sdk_param_edit_set(kProject, 0, 1, 850);
    capture = 0; sdk_gui_tick();
    require(bases[0][1] == 800 && value(0, 1) == 800, "Further edits revived a superseded capture base");

    // A new/load project can reuse the same buffer: discard without touching it.
    capture = 1; sdk_gui_tick();
    sdk_param_edit_set(kProject, 0, 1, 950);
    plan({{0, 1, 0, 1000}}); sdk_gui_tick();
    sdk_control_forget();
    bases[0][1] = 1000; value(0, 1) = 1000;
    capture = 0; plan({}); sdk_gui_tick();
    require(bases[0][1] == 1000 && value(0, 1) == 1000 && original_count == 0,
            "Old capture or overlay leaked into a replacement project");
    return 0;
}
