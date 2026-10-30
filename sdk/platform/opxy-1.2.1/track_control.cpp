// Stock OS 1.2.1 GUI tick for track control. 8c0baacc, the GUI's per-tick
// update, calls 8d580074 (a void leaf) just before the sequencer tick; the
// hook replaces that CALL.A, so stock calls it with the C ABI.
//
// Stock keeps each track's live parameters in g_project + 0x4080, 58 words a
// track: the values edits, locks and sequencing set, which 8d5acea8 writes and
// publishes to the audio side. The saved project holds separate base values,
// so an overlay written here never reaches a saved project. Each tick the SDK
// notices stock's own writes over an overlaid value, runs every machine on
// tracks 0..7, then on the aux tracks machines took over (aux_tracks.cpp), in
// track order with every value without overlays, writes their
// overlays through 8d5acea8 (the later track's winning a parameter both
// overlay), and restores what stock last set wherever no machine overlays.
//
// Capture: 8d5ad564, the encoder edit stock's controllers reach through
// 8d5ad5bc, reads the base value at track data + 0x3864 and, unless a step is
// held for a lock, calls the setter 8d5acf28 with the new value. That call at
// 8d5ad5ae routes through sdk_param_edit_set. While a machine captures (one
// copy at a time), it records the edit and the base value from before the
// first one, then makes the original call; when capture ends, the SDK sets each
// base value back.
//
// Display: stock's displayed-value getter 8d5ac9dc shows a parameter's live
// value while the sequencer plays and its base value otherwise, plus any LFO
// amount. A jump hook at 8d5aca48 (track_control_hooks.s) asks
// sdk_param_shown instead, which shows an overlaid parameter's overlay either
// way, except for a moment after an encoder edit of it, when it shows the base
// value the edit changed. Every other parameter shows as stock shows it. The
// host track's own engine controls reach the machine as that getter shows
// them, so an LFO on one of them moves the machine like a turn.
//
// Mute: stock keeps it per scene, in the scene records at g_project (the word
// at 840a0524) + 0x98, 33 bytes each: each track's pattern, then each track's
// flags, in which 0x02 is muted, then one more byte; the current scene is the
// byte at g_project + 6. Aux + track key and the mixer both set it; a new
// scene copies the current one's (measured on the device with a memory probe).
// While a machine's track is muted, as with stock's brain, none of its
// overlays, capture or marks apply; it still runs and is told (Input.muted).
#include "track_control.hpp"
#include "stock.hpp"
#include "synth_controls.hpp"
#include "benchmark.hpp"

namespace {
using opxy::i32;
using opxy::u32;
using opxy::u8;
using namespace opxy::platform;
namespace control = opxy::control;

const u32 kProject = 0x86bd027c;
const u32 kLive = kProject + 0x4080;

i32& live(u32 track, u32 index) {
    return *at<i32>(kLive + (track * control::track_parameters + index) * 4);
}
void live_set(u32 track, u32 index, i32 value) {
    stock<void(u32, u32, u32, i32)>(0x8d5acea8)(kProject, track, index, value);
}
void param_set(u32 project, u32 track, u32 index, i32 value) {
    stock<void(u32, u32, u32, i32)>(0x8d5acf28)(project, track, index, value);
}
// Stock's displayed value: 8d5ac9dc(project, track, index, 1), as the pages ask.
const u32 kEngineControls = 8;
// Whether `track` is muted in the current scene.
bool muted(u32 track) {
    const u32 project = word(0x840a0524);
    return byte(project + 0x98 + byte(project + 6) * 33 + 16 + track) & 0x02;
}
i32 shown(u32 track, u32 index) {
    return stock<i32(u32, u32, u32, u32)>(0x8d5ac9dc)(kProject, track, index, 1);
}
i32 base(u32 track, u32 index) {
    return static_cast<i32>(word(stock<u32(u32)>(0x8d587708)(track) + 0x3864 + index * 4));
}

// One overlaid parameter: the value written, stock's own value beneath it,
// and the track of the machine whose overlay it is.
struct Applied { u8 track, index, wanted, owner; i32 written, underlying; u32 block; };
// Every machine's overlays together.
const u32 kAppliedCapacity = 2 * control::overlay_capacity;
Applied applied[kAppliedCapacity];
u32 applied_count;
// This tick's overlays from every machine, the later track's winning a parameter.
struct Wanted { u8 track, index, owner; i32 value; };
Wanted wanted[kAppliedCapacity];
u32 wanted_count;
control::Input input;
control::Overlay overlay;
// Every track, aux tracks included, can host a machine; kNoTrack is none.
const u32 kNoTrack = control::all_tracks;
// The machine each track ran last tick, to reset a copy that arrives, and
// whether that copy's next tick is timed.
const ControlMachine* hosts[control::all_tracks];
bool timed[control::all_tracks];
// The core clock, 400 MHz, as cycles per microsecond, and the cycle count at
// the last tick, for each tick's elapsed time.
const u32 kCyclesPerMicrosecond = 400;
u32 last_cycles;
// GUI ticks so far, and the last encoder edit's parameter and tick. For
// kTurnTicks after it the edited parameter shows its base value.
const u32 kTurnTicks = 40;
u32 gui_ticks, turned_track = kNoTrack, turned_index, turned_tick;

// Capture: the track whose copy captures (kNoTrack when none), edits since the
// last tick, and each edited parameter's base value from before the capture.
// A parameter with no room to restore is not captured.
const u32 kOriginals = 2 * control::overlay_capacity;
u32 capture_track = kNoTrack;
// The tracks whose machine asked to mark the pages of the tracks it overlays.
bool marking[control::all_tracks];
control::Entry pending[control::edit_capacity];
u32 pending_count;
struct Original { control::Entry edit; u32 block, engine; i32 last; };
Original originals[kOriginals];
u32 original_count;
u32 capture_project;
// What each track's machine passed to its display last tick.
i32 shown_words[control::all_tracks][control::display_words];

}

opxy::platform::AuxService sdk_aux;

u32 opxy::platform::control_engine(u32 track) {
    if (track < control::tracks) return track_engine(track);
    return sdk_aux.engine ? sdk_aux.engine(track) : 0;
}

namespace {
const ControlMachine* owner(u32 track) {
    const u32 engine = control_engine(track);
    if (!engine) return nullptr;
    for (u32 i = 0; i < sdk_control_machine_count; ++i) {
        const ControlMachine& m = sdk_control_machines[i];
        if (m.engine() == engine) return m.ready() == 1 ? &m : nullptr;
    }
    return nullptr;
}
u32 machine_index(const ControlMachine* m) { return static_cast<u32>(m - sdk_control_machines); }

Applied* find(u32 track, u32 index) {
    for (u32 i = 0; i < applied_count; ++i)
        if (applied[i].track == track && applied[i].index == index) return &applied[i];
    return nullptr;
}

// Restoring a base value also makes it stock's value beneath any overlay.
void end_capture() {
    for (u32 i = 0; i < original_count; ++i) {
        if (capture_project != word(0x840a0524)) break;
        const Original& original = originals[i];
        const control::Entry& o = original.edit;
        i32* stored = at<i32>(original.block + 0x3864 + o.index * 4);
        // A replaced engine/preset or another writer owns its new value.
        if (word(original.block + 0x18) != original.engine || *stored != original.last) continue;
        if (stock<u32(u32)>(0x8d587708)(o.track) == original.block) {
            param_set(kProject, o.track, o.index, o.value);
            if (Applied* a = find(o.track, o.index)) a->underlying = o.value;
        } else {
            // Restore the pattern actually edited without publishing it into
            // the track's new current pattern. Its edit already dirtied the project.
            *stored = o.value;
        }
    }
    original_count = 0;
    pending_count = 0;
    capture_track = kNoTrack;
}

// Adds the overlay of the machine on `host`: entries in range and not on its
// own track; a later entry, or a later track's, replaces one for the same parameter.
void merge(u32 host) {
    for (u32 i = 0; i < overlay.count; ++i) {
        const control::Entry& e = overlay.entries[i];
        if (e.track >= control::tracks || e.index >= control::track_parameters || e.track == host) continue;
        Wanted* w = nullptr;
        for (u32 j = 0; j < wanted_count && !w; ++j)
            if (wanted[j].track == e.track && wanted[j].index == e.index) w = &wanted[j];
        if (!w) {
            if (wanted_count >= kAppliedCapacity) continue;
            w = &wanted[wanted_count++];
            w->track = e.track;
            w->index = e.index;
        }
        w->owner = static_cast<u8>(host);
        w->value = e.value < 0 ? 0 : e.value;
    }
}
}

Marks opxy::platform::control_marks() {
    if (capture_track < kNoTrack && hosts[capture_track])
        return Marks{kMarkCapture, machine_index(hosts[capture_track])};
    const u32 track = selected_track();
    for (u32 i = 0; i < applied_count; ++i) {
        const Applied& a = applied[i];
        if (a.track == track && marking[a.owner] && hosts[a.owner])
            return Marks{kMarkOverlaid, machine_index(hosts[a.owner])};
    }
    return Marks{0, 0};
}

extern "C" i32 sdk_control_display(u32 index) {
    const u32 track = selected_track();
    return track < control::all_tracks && index < control::display_words ? shown_words[track][index] : 0;
}

extern "C" u32 sdk_control_copies(u32 track) {
    if (track >= control::all_tracks) return 0;
    const u32 engine = control_engine(track);
    if (!engine) return 0;
    u32 count = 0;
    for (u32 t = 0; t < control::all_tracks; ++t) count += control_engine(t) == engine;
    return count;
}

extern "C" i32 sdk_param_shown(u32 project, u32 track, u32 index, u32 playing) {
    const Applied* a = track < control::tracks && index < control::track_parameters ? find(track, index) : nullptr;
    const bool turned = track == turned_track && index == turned_index && gui_ticks - turned_tick < kTurnTicks;
    if (a && !turned) return a->written;
    if (playing && !a) return live(track, index);
    return stock<i32(u32, u32, u32)>(0x8d5ac910)(project, track, index);
}

// A machine on the CV track keeps its own LFO whatever track is selected. Stock's
// LFO telemetry (ids 0x16/0x17) is one pair with no track in it, published only
// for the selected track. So the aux evaluator's publish (8d50f422) also puts this
// track's amount in id 25, which stock neither writes nor reads: a tag, the track,
// the slot and the amount's top 16 bits. The telemetry table (080376e4, one on each
// board) carries it to the GUI, where the displayed-value getter uses it for this
// track in place of the pair. One word carries one track.
namespace {
const u32 kTelemetry = 0x080376e4, kHeldId = 25, kHeldTrack = 11, kHeldTag = 0xa0000000u | kHeldTrack << 24;
}

extern "C" i32 sdk_lfo_held_amount(u32 track, u32 index, i32 stock_amount) {
    if (track != kHeldTrack || !hosts[track]) return stock_amount;
    if (!hosts[track]->shared_lfo) return 0;
    const u32 held = word(kTelemetry + kHeldId * 4);
    if ((held & 0xff000000u) != kHeldTag) return stock_amount;
    if (!byte(stock<u32(u32)>(0x8d587708)(track) + 0x24) || ((held >> 16) & 0xff) != index) return 0;
    return static_cast<i32>(held << 16);
}

extern "C" void sdk_param_edit_set(u32 project, u32 track, u32 index, i32 value) {
    turned_track = track;
    turned_index = index;
    turned_tick = gui_ticks;
    if (capture_track < kNoTrack && track < control::tracks && track != capture_track &&
        index < control::track_parameters) {
        const u32 block = stock<u32(u32)>(0x8d587708)(track);
        Original* known = nullptr;
        for (u32 i = 0; i < original_count && !known; ++i)
            if (originals[i].edit.track == track && originals[i].edit.index == index && originals[i].block == block)
                known = &originals[i];
        if (known && (known->engine != track_engine(track) || base(track, index) != known->last)) {
            // A new engine/preset or another writer replaced the captured base.
            // Further edits now restore to that replacement, not the earlier one.
            known->engine = track_engine(track);
            known->edit.value = base(track, index);
        }
        if (!known && original_count < kOriginals) {
            known = &originals[original_count++];
            known->block = block;
            known->engine = track_engine(track);
            control::Entry& o = known->edit;
            o.track = static_cast<u8>(track);
            o.index = static_cast<u8>(index);
            o.reserved = 0;
            o.value = base(track, index);
        }
        if (known) known->last = value < 0 ? 0 : value;
        if (known && pending_count < control::edit_capacity) {
            control::Entry& e = pending[pending_count++];
            e.track = static_cast<u8>(track);
            e.index = static_cast<u8>(index);
            e.reserved = 0;
            e.value = value < 0 ? 0 : value;
        }
    }
    param_set(project, track, index, value);
}

// Called by project load/new-project handling before another GUI tick. It
// must not dereference stock's project while deserialization is in progress.
extern "C" void sdk_control_forget() {
    sdk_synth_queue_page_key(kNoTrack, 0);
    original_count = pending_count = applied_count = wanted_count = 0;
    capture_track = kNoTrack;
    for (u32 t = 0; t < control::all_tracks; ++t) {
        hosts[t] = nullptr;
        timed[t] = marking[t] = false;
        for (u32 i = 0; i < control::display_words; ++i) shown_words[t][i] = 0;
    }
}

extern "C" void sdk_gui_tick() {
    stock<void()>(0x8d580074)();
    ++gui_ticks;
    sdk_synth_pages();
    if (!sdk_control_machine_count && !applied_count) return;
    if (sdk_aux.tick) sdk_aux.tick();

    // A value that differs from the one written is stock's: an edit, a lock or a step.
    u32 retained = 0;
    for (u32 i = 0; i < applied_count; ++i) {
        Applied a = applied[i];
        if (a.block != stock<u32(u32)>(0x8d587708)(a.track)) continue;
        const i32 value = live(a.track, a.index);
        if (value != a.written) a.underlying = value;
        a.wanted = 0;
        applied[retained++] = a;
    }
    applied_count = retained;

    // Every track's ready control machine runs, in track order:
    // each copy of a machine with its own state, except a unique machine that
    // more than one track holds, which none runs.
    const u32 now = opxy::benchmark::cycles(), us = (now - last_cycles) / kCyclesPerMicrosecond;
    last_cycles = now;
    wanted_count = 0;
    bool loaded = false, kept = false;
    u32 asking = kNoTrack;
    for (u32 t = 0; t < control::all_tracks; ++t) {
        const ControlMachine* m = owner(t);
        if (m && m->unique && sdk_control_copies(t) > 1) m = nullptr;
        const u32 copy = m && !m->unique ? t : 0;
        if (m != hosts[t]) {
            if (capture_track == t) end_capture();
            hosts[t] = m;
            timed[t] = false;
            if (m) m->reset(copy);
        }
        marking[t] = false;
        for (u32 i = 0; i < control::display_words; ++i) shown_words[t][i] = 0;
        if (!m) continue;
        if (!loaded) {
            for (u32 u = 0; u < control::all_tracks; ++u)
                for (u32 p = 0; p < control::track_parameters; ++p) input.values[u][p] = live(u, p);
            for (u32 u = 0; u < control::all_tracks; ++u) {
                i32& last = input.values[u][control::track_parameters - 1];
                if (last == kAuxMark || last == kAuxRelease) last = base(u, control::track_parameters - 1);
            }
            for (u32 i = 0; i < applied_count; ++i)
                input.values[applied[i].track][applied[i].index] = applied[i].underlying;
            input.transport = opxy::Transport{project_bpm_x10(), word(kTicks96), byte(kPlaying) || byte(kPlaying + 1)};
            loaded = true;
        }
        // The host's own controls as its page shows them, LFO included.
        i32 own[kEngineControls];
        for (u32 p = 0; p < kEngineControls; ++p) {
            own[p] = input.values[t][p];
            input.values[t][p] = shown(t, p);
        }
        input.track = t;
        input.page_key = sdk_synth_page_key(t);
        input.muted = muted(t);
        input.capturing = capture_track == t;
        input.edit_count = input.capturing ? pending_count : 0;
        for (u32 i = 0; i < input.edit_count; ++i) input.edits[i] = pending[i];
        if (input.capturing) pending_count = 0;
        input.elapsed_us = timed[t] ? (us < 1000000 ? us : 1000000) : 0;
        timed[t] = true;
        overlay.count = 0;
        overlay.capture = 0;
        overlay.mark = 0;
        for (u32 i = 0; i < control::display_words; ++i) overlay.display[i] = 0;
        m->control(copy, &input, &overlay);
        for (u32 p = 0; p < kEngineControls; ++p) input.values[t][p] = own[p];
        if (overlay.count > control::overlay_capacity) overlay.count = control::overlay_capacity;
        if (input.muted) overlay.count = overlay.capture = overlay.mark = 0;
        if (overlay.capture) {
            if (t == capture_track) kept = true;
            else if (asking == kNoTrack) asking = t;
        }
        marking[t] = overlay.mark != 0;
        for (u32 i = 0; i < control::display_words; ++i) shown_words[t][i] = overlay.display[i];
        merge(t);
    }

    // The copy capturing keeps on while it asks; otherwise the first asking
    // starts. Ending a capture sets its base values back before this tick's overlays.
    if (!kept) {
        if (capture_track < kNoTrack) end_capture();
        capture_track = asking;
        capture_project = word(0x840a0524);
    }

    // Mark what the machines still overlay; restore and drop the rest.
    for (u32 i = 0; i < wanted_count; ++i)
        if (Applied* a = find(wanted[i].track, wanted[i].index)) a->wanted = 1;
    u32 count = 0;
    for (u32 i = 0; i < applied_count; ++i) {
        const Applied& a = applied[i];
        if (a.wanted) applied[count++] = a;
        else if (live(a.track, a.index) == a.written) live_set(a.track, a.index, a.underlying);
    }
    applied_count = count;

    // Write the overlays.
    for (u32 i = 0; i < wanted_count; ++i) {
        const Wanted& w = wanted[i];
        Applied* a = find(w.track, w.index);
        if (!a) {
            if (applied_count >= kAppliedCapacity) continue;
            a = &applied[applied_count++];
            a->track = w.track;
            a->index = w.index;
            a->block = stock<u32(u32)>(0x8d587708)(w.track);
            a->underlying = live(w.track, w.index);
        }
        a->wanted = 1;
        a->owner = w.owner;
        a->written = w.value;
        if (live(w.track, w.index) != w.value) live_set(w.track, w.index, w.value);
    }
}
