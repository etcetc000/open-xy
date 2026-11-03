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
u32 engines[16], ticks, writes, resets, runs, sets;
// The machine holding each aux track (aux_tracks.cpp), and its ticks.
u32 aux_engines[16], aux_ticks;
i32 seen;  // the value of track 0, parameter 1 the machine was given
u32 seen_elapsed, seen_bpm, seen_ticks, seen_playing, seen_key, seen_muted;
// The project's scene records (g_project + 0x98, 33 bytes a scene: patterns, then
// each track's flags, 0x02 muted) and the current scene (g_project + 6), at a
// stand-in address the word at 840a0524 names.
const u32 kProjectAt = 0xa0000000;
u32 project_word = kProjectAt;
u32 project_loading;  // the word at 840a0528
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
// The scripted stored writes for each tick, and what store() answered.
control::Entry stored_script[control::store_capacity + 2];
u32 stored_script_count;
bool stored_ok[control::store_capacity + 2];
// Machine 40's scripted notes and own turns, and what it was given.
control::Note note_script[control::note_capacity];
u32 note_script_count, own_turns, shift, seen_shift, seen_turn_count;
control::Turn seen_turns[control::turn_capacity];
// Stock's live notes: those sounding, counts of note-ons and note-offs, and
// the velocity of the last.
struct Sounding { u32 track, pitch; };
Sounding sounding[64];
u32 sounding_count, note_ons, note_offs, last_velocity;

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
// 8d595738 and 8d595420 with the preview call sites' arguments, and the
// controller's clock 8d59357c, whose 64-bit time note-on receives.
const u32 kNoteController = 0x860c6adc;
const __UINT64_TYPE__ kNoteTime = (static_cast<__UINT64_TYPE__>(5) << 32) | 1234;
void stock_note_on(u32 c, u32 track, u32 pitch, u32 a3, u32 a4, u32 velocity, u32 a6, u32 source, u32 low,
                   u32 high, u32 flags, const void* event) {
    require(c == kNoteController && a3 == 0xf0 && a4 == 0 && a6 == 0 && source == 1 && low == 1234 && high == 5 &&
            flags == 0 && !event, "Note-on arguments not the preview's");
    require(track < control::tracks && pitch < 128 && velocity >= 1 && velocity <= 127, "Invalid note played");
    require(sounding_count < 64, "Too many notes sounding");
    sounding[sounding_count++] = Sounding{track, pitch};
    ++note_ons;
    last_velocity = velocity;
}
void stock_note_off(u32 c, u32 track, u32 pitch, u32 source, u32 flags, const void* event) {
    require(c == kNoteController && source == 1 && flags == 0 && !event, "Note-off arguments not the preview's");
    u32 i = 0;
    while (i < sounding_count && !(sounding[i].track == track && sounding[i].pitch == pitch)) ++i;
    require(i < sounding_count, "Note-off for a note not sounding");
    sounding[i] = sounding[--sounding_count];
    ++note_offs;
}
__UINT64_TYPE__ stock_note_time(u32 c) {
    require(c == kNoteController, "Clock asked of another controller");
    return kNoteTime;
}
u32 stock_shift() { return shift; }
void notes(std::initializer_list<control::Note> list) {
    note_script_count = 0;
    for (const control::Note& n : list) note_script[note_script_count++] = n;
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
    require(overlay->display[0] == 0 && overlay->display[3] == 0, "Display words not cleared");
    require(overlay->stored_count == 0, "Stored writes not cleared");
    overlay->display[1] = 77;
    for (u32 i = 0; i < script_count; ++i) overlay->entries[overlay->count++] = script[i];
    for (u32 i = 0; i < stored_script_count; ++i) {
        const control::Entry& e = stored_script[i];
        stored_ok[i] = control::store(*overlay, e.track, e.index, e.value);
    }
    require(overlay->note_count == 0 && overlay->own_turns == 0, "Notes and own turns not cleared");
    for (u32 i = 0; i < note_script_count; ++i) overlay->notes[overlay->note_count++] = note_script[i];
    overlay->own_turns = own_turns;
    seen_shift = input->shift;
    seen_turn_count = input->turn_count;
    for (u32 i = 0; i < input->turn_count; ++i) seen_turns[i] = input->turns[i];
}
// Machine 41, not unique: each copy overlays its scripted entries, with its
// copy's own count of runs.
u32 other_runs[control::tracks], other_resets[control::tracks];
control::Entry other_script[control::tracks][4];
u32 other_count[control::tracks];
void reset_other(u32 copy) { require(copy < control::tracks, "Copy out of range"); ++other_resets[copy]; }
void run_other(u32 copy, const control::Input* input, control::Overlay* overlay) {
    require(copy == input->track, "A copy is not its track");
    ++other_runs[copy];
    overlay->display[0] = static_cast<i32>(100 + copy);
    for (u32 i = 0; i < other_count[copy]; ++i) overlay->entries[overlay->count++] = other_script[copy][i];
}
void plan(std::initializer_list<control::Entry> entries) {
    script_count = 0;
    for (const control::Entry& e : entries) script[script_count++] = e;
}
void plan_stored(std::initializer_list<control::Entry> entries) {
    stored_script_count = 0;
    for (const control::Entry& e : entries) stored_script[stored_script_count++] = e;
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
// The turn hooks' pointer (synth_controls.cpp), which the GUI tick fills in.
bool (*sdk_control_turn)(u32, u32, const i32*) = nullptr;
opxy::u32 sdk_benchmark_cycles() { return cycles; }
// The aux service's entry points, which its startup fills in on the device.
opxy::u32 aux_engine(opxy::u32 track) { return track < control::all_tracks ? aux_engines[track] : 0; }
void aux_tick() { ++aux_ticks; }
const volatile unsigned short* stock_project_tempo() { return &tempo; }

// Machine 42 keeps data with projects.
opxy::u32 keep_save(opxy::u8*) { return 0; }
void keep_load(const opxy::u8*, opxy::u32) {}

extern "C" {
const opxy::platform::ControlMachine sdk_control_machines[] = {
    // 40 and 41 may run on tracks 1-8 here, as no real machine does; 42 is an
    // aux machine as every real one is.
    {reinterpret_cast<const char*>(40), ready, reset, run, nullptr, 0, nullptr, nullptr, true, 0, nullptr, {}, true},
    {reinterpret_cast<const char*>(41), ready, reset_other, run_other, nullptr, 0, nullptr, nullptr, false, 0, nullptr, {}},
    {reinterpret_cast<const char*>(42), ready, reset, run, nullptr, 4, keep_save, keep_load, true, 1u << 11, nullptr, {}},
    {nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, false, 0, nullptr, {}}};
const opxy::u32 sdk_control_machine_count = 3;
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
    if (address == 0x8d595738) return reinterpret_cast<Address>(&stock_note_on);
    if (address == 0x8d595420) return reinterpret_cast<Address>(&stock_note_off);
    if (address == 0x8d59357c) return reinterpret_cast<Address>(&stock_note_time);
    if (address == 0x8d51f184) return reinterpret_cast<Address>(&stock_shift);
    if (address == 0x86bd0080) return reinterpret_cast<Address>(&ticks96);
    if (address == 0x860c9b39) return reinterpret_cast<Address>(&playing[0]);
    if (address == 0x860c9b3a) return reinterpret_cast<Address>(&playing[1]);
    if (address == 0x840a0524) return reinterpret_cast<Address>(&project_word);
    if (address == 0x840a0528) return reinterpret_cast<Address>(&project_loading);
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

    // Edits pass through untouched.
    bases[0][5] = 50;
    sdk_param_edit_set(kProject, 0, 5, 70);
    require(sets == 1 && bases[0][5] == 70, "Edit not delegated");
    bases[0][5] = 50;

    engines[2] = 40;
    plan({});
    tick(0, "A returning machine wrote an overlay");

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
    plan({});
    tick(1, "Release not written");

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
    // Two machines on one parameter: the later track's wins.
    other_count[6] = 1;
    other_script[6][0] = control::Entry{0, 1, 0, 666};
    tick(1, "Contested parameter not written once");
    require(value(0, 1) == 666 && sdk_param_shown(kProject, 0, 1, 1) == 666, "Later track did not win");
    // The later machine gives the parameter up: the earlier one's comes back.
    other_count[6] = 0;
    tick(1, "Earlier overlay not written back");
    require(value(0, 1) == 500, "Earlier machine's overlay not restored");
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

    // A new/load project can reuse the same buffer: discard without touching it.
    aux_engines[11] = 40;
    for (u32 s = 0; s < 3; ++s) mute_flags(s, 11) = 0;
    plan({{0, 1, 0, 1000}}); sdk_gui_tick();
    require(value(0, 1) == 1000, "Overlay before the replacement not written");
    sdk_control_forget();
    bases[0][1] = 1234; value(0, 1) = 1234;
    plan({}); sdk_gui_tick();
    require(bases[0][1] == 1234 && value(0, 1) == 1234 && applied_count == 0,
            "Old overlay leaked into a replacement project");

    // Locks win: a step's lock on an overlaid parameter plays as written, and the
    // overlay stands aside until stock sets the parameter to something else.
    playing[1] = 1;
    plan({{0, 2, 0, 600}});
    tick(1, "Overlay before the lock not written");
    require(value(0, 2) == 600, "Overlay before the lock not applied");
    sdk_param_step_set(kProject, 0, 2, 250);
    require(value(0, 2) == 250, "Lock not written");
    tick(0, "Overlay written over a lock");
    tick(0, "Overlay written over a held lock");
    require(value(0, 2) == 250, "Lock not heard");
    // A sliding lock writes again: still the lock's.
    sdk_param_step_set(kProject, 0, 2, 260);
    tick(0, "Overlay written over a sliding lock");
    require(value(0, 2) == 260, "Sliding lock not heard");
    // Stock's next step sets the parameter back: the overlay returns.
    value(0, 2) = 40;
    tick(1, "Overlay not back after the lock");
    require(value(0, 2) == 600, "Overlay not restored after the lock");
    // A lock of the overlay's own value is still the lock: released, it stays.
    sdk_param_step_set(kProject, 0, 2, 600);
    tick(0, "Overlay rewritten over a lock of its own value");
    plan({});
    tick(0, "A lock of the overlay's own value rewritten");
    require(value(0, 2) == 600, "Lock of the overlay's value undone");
    // A lock of the track's own value ends a step later; stopping ends any lock.
    bases[0][4] = 90;
    value(0, 4) = 90;
    plan({{0, 4, 0, 700}});
    tick(1, "Overlay not written");
    sdk_param_step_set(kProject, 0, 4, 90);
    tick(0, "Overlay written over a lock of the track's own value");
    require(value(0, 4) == 90, "Lock of the track's own value not heard");
    ticks96 += 24;
    tick(1, "Overlay not back a step after a lock of the track's own value");
    require(value(0, 4) == 700, "Lock of the track's own value held on");
    sdk_param_step_set(kProject, 0, 4, 95);
    tick(0, "Overlay written over a lock");
    playing[1] = 0;
    tick(1, "Overlay not back after stop");
    require(value(0, 4) == 700, "Lock held after stop");
    playing[1] = 1;
    // A lock where no machine overlays plays.
    sdk_param_step_set(kProject, 0, 3, 77);
    require(value(0, 3) == 77, "Lock without an overlay not written");
    plan({});
    tick(1, "Overlay not released after the locks");
    // A value stock set beneath an overlay is what the overlay's release restores,
    // even at the overlay's own value.
    plan({{0, 5, 0, 321}});
    tick(1, "Overlay not written");
    bases[0][5] = 321;
    opxy::platform::control_stock_set(0, 5, 321);
    plan({});
    tick(0, "Release rewrote the loaded value");
    require(value(0, 5) == 321, "Loaded value lost when the overlay was released");

    // Stored writes, from the machine on aux track 12: applied after control()
    // returns, through stock's setter, into the stored value; negatives count as 0.
    // Beneath an overlay of the same parameter, the overlay still plays this tick.
    plan({{0, 5, 0, 700}});
    tick(1, "Overlay not written");
    plan_stored({{0, 5, 0, 444}, {1, 3, 0, -7}});
    sets = 0;
    tick(1, "Overlay not written back over the stored write");
    require(stored_ok[0] && stored_ok[1] && sets == 2, "Stored writes not applied once each");
    require(bases[0][5] == 444 && bases[1][3] == 0 && value(1, 3) == 0, "Stored values not set");
    require(value(0, 5) == 700, "Overlay lost under its own parameter's stored write");
    // It persists: the overlay's release restores the stored write, not what was beneath.
    plan_stored({});
    plan({});
    tick(1, "Release not written");
    require(value(0, 5) == 444 && bases[0][5] == 444, "Stored write lost when the overlay was released");
    sets = 0;
    tick(0, "Stored write written again");
    require(sets == 0 && value(0, 5) == 444, "Stored write repeated");
    // Under a step's lock, only the stored value: the lock plays on, and stays the
    // value beneath an overlay of it.
    playing[1] = 1;
    plan({{0, 7, 0, 640}});
    tick(1, "Overlay not written");
    sdk_param_step_set(kProject, 0, 7, 250);
    plan_stored({{0, 7, 0, 555}});
    sets = 0;
    tick(0, "Overlay written over a lock");
    require(sets == 0 && bases[0][7] == 555 && value(0, 7) == 250 && lock_holds(0, 7),
            "Stored write played over a lock");
    plan_stored({});
    plan({});
    tick(0, "Release wrote over the lock");
    require(value(0, 7) == 250, "Lock lost when the overlay was released");
    sdk_control_forget();
    tick(0, "Nothing to write");
    // Out of the table: refused, nothing written.
    plan_stored({{8, 0, 0, 5}, {11, 1, 0, 5}, {0, 58, 0, 5}});
    tick(0, "Refused stored writes wrote");
    require(!stored_ok[0] && !stored_ok[1] && !stored_ok[2] && sets == 0, "Out-of-range stored write accepted");
    // At most store_capacity a tick: the rest are refused.
    stored_script_count = 0;
    for (u32 i = 0; i < control::store_capacity + 1; ++i)
        stored_script[stored_script_count++] = control::Entry{static_cast<opxy::u8>(i % 8),
                                                              static_cast<opxy::u8>(20 + i / 8), 0,
                                                              static_cast<i32>(1000 + i)};
    tick(0, "Stored writes wrote live overlays");
    require(stored_ok[control::store_capacity - 1] && !stored_ok[control::store_capacity] &&
            sets == control::store_capacity && bases[7][21] == 1015 && bases[0][22] == 0,
            "Stored writes not capped");
    // While the machine's track is muted it writes nothing.
    sets = 0;
    plan_stored({{0, 6, 0, 99}});
    mute_flags(0, 11) = 0x02;
    tick(0, "Muted machine wrote");
    require(sets == 0 && bases[0][6] != 99, "Muted machine's stored write applied");
    mute_flags(0, 11) = 0;
    tick(0, "Stored write wrote an overlay");
    require(sets == 1 && bases[0][6] == 99, "Unmuted machine's stored write not applied");
    // Never to its own track: the machine on track 3 writes nothing there.
    plan_stored({});
    tick(0, "Nothing to write");
    aux_engines[11] = 0;
    engines[2] = 40;
    tick(0, "Nothing to write");
    sets = 0;
    const u32 own_before = bases[2][1];
    plan_stored({{2, 1, 0, static_cast<i32>(own_before + 5)}});
    tick(0, "Own-track stored write wrote");
    require(stored_ok[0] && sets == 0 && bases[2][1] == own_before, "Stored write to the machine's own track applied");
    plan_stored({});
    engines[2] = 0;
    aux_engines[11] = 40;
    tick(0, "Nothing to write");
    // While a project loads, no machine runs and nothing is written; the aux
    // service still ticks, for its marks.
    plan({{0, 1, 0, 1500}});
    project_loading = 1;
    const u32 loading_runs = runs, loading_aux = aux_ticks;
    tick(0, "Overlay written while a project loads");
    require(runs == loading_runs && aux_ticks == loading_aux + 1, "Machines ran, or no marks, while a project loads");
    project_loading = 0;
    tick(1, "Overlay not written after the load");
    require(runs == loading_runs + 1 && aux_ticks == loading_aux + 2, "Machines not back after the load");
    plan({});
    tick(1, "Overlay not released");

    // An aux machine's engine on tracks 1-8 runs nothing there and is no copy.
    aux_engines[11] = 0;
    engines[1] = 42;
    const u32 stray_runs = runs;
    tick(0, "Aux machine wrote from an instrument track");
    require(runs == stray_runs && sdk_control_copies(1) == 0 && opxy::platform::control_engine(1) == 0,
            "Aux machine ran on an instrument track");
    const u32 first_resets = resets;
    aux_engines[11] = 42;
    tick(0, "Nothing to write");
    require(runs == stray_runs + 1 && sdk_control_copies(11) == 1, "Aux machine stopped by its engine on track 2");
    // Its first arrival with no load since startup resets it, though it keeps data.
    require(resets == first_resets + 1, "Machine keeping data not reset on its first arrival without a load");
    engines[1] = 0;
    aux_engines[11] = 40;
    tick(0, "Nothing to write");

    // A machine that keeps data with projects is reset before its load, and on
    // arrival only before any load: leaving the track and coming back keeps its
    // state. Others reset on arrival.
    aux_engines[11] = 0;
    tick(0, "Nothing to write");
    sdk_control_forget();
    const u32 load_resets = resets;
    opxy::platform::control_loaded(sdk_control_machines[2]);
    require(resets == load_resets + 1, "Loaded machine not reset before its load");
    aux_engines[11] = 42;
    tick(0, "Nothing to write");
    aux_engines[11] = 0;
    tick(0, "Nothing to write");
    aux_engines[11] = 42;
    tick(0, "Nothing to write");
    require(resets == load_resets + 1 && hosts[11], "Machine keeping data reset on arrival, losing its state");
    aux_engines[11] = 40;
    tick(0, "Nothing to write");
    require(resets == load_resets + 2, "Arriving machine not reset");

    // A track moved to another pattern while stock left the overlay playing gets
    // the new pattern's own value.
    plan({{0, 1, 0, 1700}});
    tick(1, "Overlay not written");
    second_bases[0][1] = 456;
    pattern[0] = 1;
    plan({});
    tick(1, "Stale overlay not replaced on a pattern change");
    require(value(0, 1) == 456, "New pattern's value not played");
    pattern[0] = 0;

    // Loading a project ends every lock.
    sdk_param_step_set(kProject, 0, 4, 95);
    sdk_control_forget();
    require(!lock_holds(0, 4), "Lock kept into another project");

    // Notes: only those in range play, through stock's live note-on.
    aux_engines[11] = 0;
    plan({});
    engines[2] = 40;
    tick(0, "Arrival wrote an overlay");
    notes({{0, 60, 100, 0, 10000}, {8, 60, 100, 0, 10000}, {1, 128, 100, 0, 10000}, {1, 61, 0, 0, 10000},
           {1, 62, 128, 0, 10000}, {1, 63, 100, 0, 0}});
    tick(0, "Notes wrote an overlay");
    require(note_ons == 1 && sounding_count == 1 && sounding[0].track == 0 && sounding[0].pitch == 60 &&
            last_velocity == 100, "Notes out of range played");
    // Each ends after its length, from the core clock.
    notes({});
    cycles += 400 * 4000;
    tick(0, "Held note wrote an overlay");
    require(sounding_count == 1, "Note ended early");
    cycles += 400 * 7000;
    tick(0, "Ending note wrote an overlay");
    require(sounding_count == 0 && note_offs == 1, "Note not ended after its length");
    // Playing a pitch the copy holds ends it first.
    notes({{3, 50, 90, 0, 1000000}});
    tick(0, "Note wrote an overlay");
    tick(0, "Retrigger wrote an overlay");
    require(note_ons == 3 && note_offs == 2 && sounding_count == 1, "Held pitch not ended before playing again");
    // Stopping the sequencer ends every note.
    notes({});
    playing[1] = 1;
    tick(0, "Start wrote an overlay");
    require(sounding_count == 1, "Starting ended a note");
    playing[1] = 0;
    tick(0, "Stop wrote an overlay");
    require(sounding_count == 0, "Stopping did not end the notes");
    // A muted machine plays nothing and ends what it holds.
    notes({{4, 70, 90, 0, 1000000}});
    tick(0, "Note wrote an overlay");
    const u32 ons = note_ons;
    require(sounding_count == 1, "Note before muting not held");
    mute_flags(project_bytes[6], 2) = 0x02;
    notes({{4, 71, 90, 0, 1000000}});
    tick(0, "Muted machine wrote an overlay");
    require(note_ons == ons && sounding_count == 0, "Muted machine played or kept its notes");
    mute_flags(project_bytes[6], 2) = 0;
    // A copy leaving its track ends its notes.
    notes({{4, 70, 90, 0, 1000000}});
    tick(0, "Note wrote an overlay");
    notes({});
    engines[2] = 0;
    tick(0, "Departure wrote an overlay");
    require(sounding_count == 0, "Departed copy's notes kept sounding");
    // No note outlasts the longest length; at most held_capacity sound at once.
    engines[2] = 40;
    notes({{5, 40, 90, 0, 0xffffffffu}});
    tick(0, "Note wrote an overlay");
    notes({});
    cycles += 400u * (control::longest_note_us - 1);
    tick(0, "Long note wrote an overlay");
    require(sounding_count == 1, "Long note ended early");
    cycles += 400u * 2;
    tick(0, "Long note wrote an overlay");
    require(sounding_count == 0, "Note outlasted the longest length");
    for (u32 round = 0; round < 2; ++round) {
        note_script_count = control::note_capacity;
        for (u32 i = 0; i < control::note_capacity; ++i)
            note_script[i] = control::Note{static_cast<opxy::u8>(i % 8), static_cast<opxy::u8>(round * 32 + i), 90, 0,
                                           1000000};
        tick(0, "Notes wrote an overlay");
    }
    require(sounding_count == control::held_capacity, "Held notes not bounded");
    notes({});
    playing[1] = 1;
    tick(0, "Start wrote an overlay");
    playing[1] = 0;
    tick(0, "Stop wrote an overlay");
    require(sounding_count == 0, "Bounded notes not ended");

    // Own turns: while the copy asks, its track's detents edit nothing and
    // reach it next tick, one direction each; other tracks' pass to stock.
    const i32 up[3] = {3, 1, 0}, down[3] = {-2, 1, 0}, still[3] = {0, 0, 0};
    require(!sdk_control_turn_offer(2, 5, up), "Turn taken before the copy asked");
    own_turns = 1;
    shift = 1;
    tick(0, "Own turns wrote an overlay");
    require(seen_shift == 1, "Shift not given");
    require(sdk_control_turn == sdk_control_turn_offer, "Turn hooks not given the control service's offer");
    require(sdk_control_turn_offer(2, 5, up) && sdk_control_turn_offer(2, 1, down) &&
            sdk_control_turn_offer(2, 1, still), "Own turn not taken");
    require(!sdk_control_turn_offer(3, 5, up) && !sdk_control_turn_offer(2, 8, up), "Another track's turn taken");
    tick(0, "Own turns wrote an overlay");
    require(seen_turn_count == 2 && seen_turns[0].slot == 5 && seen_turns[0].direction == 1 &&
            seen_turns[1].slot == 1 && seen_turns[1].direction == -1, "Own turns not given");
    tick(0, "Own turns wrote an overlay");
    require(seen_turn_count == 0, "Own turns given twice");
    own_turns = 0;
    shift = 0;
    tick(0, "End of own turns wrote an overlay");
    require(!sdk_control_turn_offer(2, 5, up) && seen_shift == 0, "Turn taken after the copy stopped asking");
    engines[2] = 0;
    tick(0, "Nothing to release");
    return 0;
}
