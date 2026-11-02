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
// Edits: 8d5ad564, the encoder edit stock's controllers reach through
// 8d5ad5bc, reads the base value at track data + 0x3864 and, unless a step is
// held for a lock, calls the setter 8d5acf28 with the new value. That call at
// 8d5ad5ae routes through sdk_param_edit_set, which notes the parameter turned
// (Display, below) and makes the call.
//
// Locks win: the sequencer writes a step's parameter locks to the live values in
// lock_apply_step (8d5936c0), plain or sliding toward the next lock, with three
// calls of 8d5acea8 (8d5937b4, 8d5938c2, 8d59396a), which route through
// sdk_param_step_set. It makes the call as stock would, so the lock plays at its
// own value, and notes the parameter as locked. While it is, no machine's overlay
// is written over it. The lock ends when the live value is no longer the lock's
// (stock's next step sets it back); when the song has moved on a step (24 ticks of
// 96 per quarter note) and the live value is the track's own, which a lock of that
// same value leaves; when the sequencer stops; or with the project. The next tick
// overlays it again.
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
// Stored writes (control::store): after a machine's control() returns, each
// write sets the stored value at track data + 0x3864 first (stock's setter
// stores it only on synth tracks), then calls the setter an encoder edit ends in,
// 8d5acf28, which publishes it; beneath an overlay it becomes stock's value,
// which the overlay's release restores. While a step's lock holds the parameter,
// only the stored value is written: the lock plays on, and stock's next step
// plays the stored value.
//
// Loading: while stock loads a project (the word at 840a0528), the tick runs
// stock's own call, the hidden-pages check and the aux service's marks only. The
// load hook has already given machines the new project's data and the aux service
// its kept controls, so neither machines nor the aux service's restore run against
// the old project until it is replaced.
//
// Mute: stock keeps it per scene, in the scene records at g_project (the word
// at 840a0524) + 0x98, 33 bytes each: each track's pattern, then each track's
// flags, in which 0x02 is muted, then one more byte; the current scene is the
// byte at g_project + 6. Aux + track key and the mixer both set it; a new
// scene copies the current one's (measured on the device with a memory probe).
// While a machine's track is muted, as with stock's brain, none of its
// overlays apply; it still runs and is told (Input.muted).
//
// Notes: stock's live note-on 8d595738 and note-off 8d595420, called with the
// arguments of its preview call sites (8c0a6410): source 1, flags 0, no event,
// so stock plays them on the track as a keyboard note and never records them.
// The SDK holds every note it starts and makes its note-off.
//
// Own turns: the turn hooks (synth_controls.cpp) offer each detent of a
// track's encoders to sdk_control_turn before stock's editor sees it.
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
const u32 kProjectLoading = 0x840a0528;

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
// The parameters a step's lock holds now, two words a track, and each one's
// value and song position (ticks96) when last written.
u32 locked[control::tracks][2];
i32 lock_value[control::tracks][control::track_parameters];
u32 lock_at[control::tracks][control::track_parameters];
const u32 kStepTicks = 24;
void unlock(u32 track, u32 index) { locked[track][index / 32] &= ~(1u << (index % 32)); }
bool lock_holds(u32 track, u32 index) {
    return track < control::tracks && index < control::track_parameters && (locked[track][index / 32] >> (index % 32) & 1);
}
// What each track's machine passed to its display last tick.
i32 shown_words[control::all_tracks][control::display_words];

// The notes the SDK started and has yet to end: track, pitch, the track of the
// machine that played it and the time it has left.
const u32 kController = 0x860c6adc;
const u32 kNoteSource = 1;
struct Held { u8 track, pitch, host, reserved; u32 remaining_us; };
Held held[control::held_capacity];
u32 held_count;
bool was_playing;

// The tracks whose machine takes its own turns, and the turns of the one
// track that had some since its machine last ran.
bool owning[control::all_tracks];
u32 turn_track = kNoTrack;
control::Turn turns[control::turn_capacity];
u32 turn_count;
}

opxy::platform::AuxService sdk_aux;

// A machine that takes over aux tracks runs there only: on tracks 1-8 (from a
// project or preset that names its engine) it is not a control machine and does
// not count as a copy.
u32 opxy::platform::control_engine(u32 track) {
    if (track >= control::tracks) return sdk_aux.engine ? sdk_aux.engine(track) : 0;
    const u32 engine = track_engine(track);
    for (u32 i = 0; engine && i < sdk_control_machine_count; ++i)
        if (sdk_control_machines[i].aux && sdk_control_machines[i].engine() == engine) return 0;
    return engine;
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
Applied* find(u32 track, u32 index) {
    for (u32 i = 0; i < applied_count; ++i)
        if (applied[i].track == track && applied[i].index == index) return &applied[i];
    return nullptr;
}

// Applies the stored writes of the machine on `host`: in range, not on its own
// track, in order.
void store(u32 host) {
    for (u32 i = 0; i < overlay.stored_count; ++i) {
        const control::Entry& e = overlay.stored[i];
        if (e.track >= control::tracks || e.index >= control::track_parameters || e.track == host) continue;
        const i32 value = e.value < 0 ? 0 : e.value;
        *at<i32>(stock<u32(u32)>(0x8d587708)(e.track) + 0x3864 + e.index * 4) = value;
        if (lock_holds(e.track, e.index)) continue;
        param_set(kProject, e.track, e.index, value);
        opxy::platform::control_stock_set(e.track, e.index, value);
    }
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

void note_on(u32 track, u32 pitch, u32 velocity) {
    const __UINT64_TYPE__ now = stock<__UINT64_TYPE__(u32)>(0x8d59357c)(kController);
    stock<void(u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, const void*)>(0x8d595738)(
        kController, track, pitch, 0xf0, 0, velocity, 0, kNoteSource, static_cast<u32>(now),
        static_cast<u32>(now >> 32), 0, nullptr);
}
// Ends held note `i`; the last held note takes its place.
void release(u32 i) {
    stock<void(u32, u32, u32, u32, u32, const void*)>(0x8d595420)(kController, held[i].track, held[i].pitch,
                                                                  kNoteSource, 0, nullptr);
    held[i] = held[--held_count];
}
void release_host(u32 host) {
    for (u32 i = held_count; i-- > 0;)
        if (held[i].host == host) release(i);
}
void age_notes(u32 us) {
    for (u32 i = held_count; i-- > 0;) {
        if (held[i].remaining_us <= us) release(i);
        else held[i].remaining_us -= us;
    }
}
// Starts the notes of the machine on `host`.
void play_notes(u32 host) {
    const u32 count = overlay.note_count < control::note_capacity ? overlay.note_count : control::note_capacity;
    for (u32 n = 0; n < count; ++n) {
        const control::Note& note = overlay.notes[n];
        if (note.track >= control::tracks || note.pitch > 127 || !note.velocity || note.velocity > 127 ||
            !note.length_us)
            continue;
        for (u32 i = 0; i < held_count; ++i) {
            if (held[i].host == host && held[i].track == note.track && held[i].pitch == note.pitch) {
                release(i);
                break;
            }
        }
        if (held_count >= control::held_capacity) continue;
        note_on(note.track, note.pitch, note.velocity);
        Held& h = held[held_count++];
        h.track = note.track;
        h.pitch = note.pitch;
        h.host = static_cast<u8>(host);
        h.reserved = 0;
        h.remaining_us = note.length_us < control::longest_note_us ? note.length_us : control::longest_note_us;
    }
}

}

extern "C" bool sdk_control_turn_offer(u32 track, u32 slot, const i32* event) {
    if (track >= control::all_tracks || !owning[track] || slot >= kEngineControls) return false;
    const i32 step = event ? event[0] : 0;
    if (!step) return true;
    if (turn_track != track) {
        turn_track = track;
        turn_count = 0;
    }
    if (turn_count < control::turn_capacity) {
        control::Turn& turn = turns[turn_count++];
        turn.slot = static_cast<u8>(slot);
        turn.reserved = 0;
        turn.direction = static_cast<opxy::i16>(step > 0 ? 1 : -1);
    }
    return true;
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
    // Not published yet: stock's pair is this track's only while it is selected.
    if ((held & 0xff000000u) != kHeldTag) return selected_track() == track ? stock_amount : 0;
    if (!byte(stock<u32(u32)>(0x8d587708)(track) + 0x24) || ((held >> 16) & 0xff) != index) return 0;
    return static_cast<i32>(held << 16);
}

// A machine that keeps data with projects is reset here, before control_load, and
// on arrival only when no load has reset it since startup: its state is the
// project's, through every pattern change. By table index (at most 31, the chooser's).
u32 started_machines;
void opxy::platform::control_loaded(const ControlMachine& machine) {
    if (machine.reset) machine.reset(0);
    const u32 index = static_cast<u32>(&machine - sdk_control_machines);
    if (index < 32) started_machines |= 1u << index;
}

void opxy::platform::control_stock_set(u32 track, u32 index, i32 value) {
    if (Applied* a = track < control::tracks && index < control::track_parameters ? find(track, index) : nullptr)
        a->underlying = value;
}

extern "C" void sdk_param_step_set(u32 project, u32 track, u32 index, i32 value) {
    stock<void(u32, u32, u32, i32)>(0x8d5acea8)(project, track, index, value);
    if (track >= control::tracks || index >= control::track_parameters) return;
    locked[track][index / 32] |= 1u << (index % 32);
    lock_value[track][index] = value;
    lock_at[track][index] = word(kTicks96);
    // The lock is stock's value beneath any overlay, even when it equals the
    // overlay's, so releasing the overlay leaves the lock playing.
    if (Applied* a = find(track, index)) a->underlying = value;
}

extern "C" void sdk_param_edit_set(u32 project, u32 track, u32 index, i32 value) {
    turned_track = track;
    turned_index = index;
    turned_tick = gui_ticks;
    param_set(project, track, index, value);
}

// Called by project load/new-project handling before another GUI tick. It
// must not dereference stock's project while deserialization is in progress.
extern "C" void sdk_control_forget() {
    sdk_synth_queue_page_key(kNoTrack, 0);
    applied_count = wanted_count = 0;
    turned_track = kNoTrack;
    for (u32 t = 0; t < control::tracks; ++t) locked[t][0] = locked[t][1] = 0;
    for (u32 t = 0; t < control::all_tracks; ++t) {
        hosts[t] = nullptr;
        timed[t] = false;
        for (u32 i = 0; i < control::display_words; ++i) shown_words[t][i] = 0;
    }
}

extern "C" void sdk_gui_tick() {
    stock<void()>(0x8d580074)();
    ++gui_ticks;
    sdk_synth_pages();
    if (!sdk_control_machine_count && !applied_count) return;
    // The turn hooks (synth_controls.cpp) reach this service through a pointer,
    // set here so it is set only where the service is linked.
    sdk_control_turn = sdk_control_turn_offer;
    // The aux service still keeps a held track's mark while a project loads.
    if (sdk_aux.tick) sdk_aux.tick();
    if (word(kProjectLoading)) return;

    // A value that differs from the one written is stock's: an edit, a lock or a step.
    // A track now on another pattern drops its overlays; one stock left playing
    // (a song's scene change between patterns of one engine) gives way to the
    // new pattern's own value.
    u32 retained = 0;
    for (u32 i = 0; i < applied_count; ++i) {
        Applied a = applied[i];
        if (a.block != stock<u32(u32)>(0x8d587708)(a.track)) {
            if (live(a.track, a.index) == a.written && !lock_holds(a.track, a.index))
                live_set(a.track, a.index, base(a.track, a.index));
            continue;
        }
        const i32 value = live(a.track, a.index);
        if (value != a.written) a.underlying = value;
        a.wanted = 0;
        applied[retained++] = a;
    }
    applied_count = retained;

    // A lock ends once stock has set its parameter to something else, a step
    // after it was written when the track plays its own value, or on stop.
    const bool playing = byte(kPlaying) || byte(kPlaying + 1);
    const u32 position = word(kTicks96);
    for (u32 t = 0; t < control::tracks; ++t)
        if (locked[t][0] | locked[t][1])
            for (u32 index = 0; index < control::track_parameters; ++index)
                if (lock_holds(t, index) &&
                    (!playing || live(t, index) != lock_value[t][index] ||
                     (position - lock_at[t][index] >= kStepTicks && live(t, index) == base(t, index))))
                    unlock(t, index);

    // Every track's ready control machine runs, in track order:
    // each copy of a machine with its own state, except a unique machine that
    // more than one track holds, which none runs.
    const u32 now = opxy::benchmark::cycles(), us = (now - last_cycles) / kCyclesPerMicrosecond;
    last_cycles = now;
    age_notes(us);
    if (was_playing && !playing)
        while (held_count) release(held_count - 1);
    was_playing = playing;
    wanted_count = 0;
    bool loaded = false;
    for (u32 t = 0; t < control::all_tracks; ++t) {
        const ControlMachine* m = owner(t);
        if (m && m->unique && sdk_control_copies(t) > 1) m = nullptr;
        const u32 copy = m && !m->unique ? t : 0;
        if (m != hosts[t]) {
            release_host(t);
            if (turn_track == t) turn_count = 0;
            hosts[t] = m;
            timed[t] = false;
            const u32 index = m ? static_cast<u32>(m - sdk_control_machines) : 32;
            const bool started = m && m->load && index < 32 && (started_machines >> index & 1);
            if (m && !started) m->reset(copy);
            if (m && m->load && index < 32) started_machines |= 1u << index;
        }
        owning[t] = false;
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
            input.transport = opxy::Transport{project_bpm_x10(), word(kTicks96), playing};
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
        input.shift = shift_held();
        input.turn_count = turn_track == t ? turn_count : 0;
        for (u32 i = 0; i < input.turn_count; ++i) input.turns[i] = turns[i];
        if (turn_track == t) turn_count = 0;
        input.elapsed_us = timed[t] ? (us < 1000000 ? us : 1000000) : 0;
        timed[t] = true;
        overlay.count = overlay.stored_count = 0;
        overlay.note_count = 0;
        overlay.own_turns = 0;
        for (u32 i = 0; i < control::display_words; ++i) overlay.display[i] = 0;
        m->control(copy, &input, &overlay);
        owning[t] = overlay.own_turns != 0;
        if (input.muted) {
            overlay.note_count = 0;
            release_host(t);
        }
        play_notes(t);
        for (u32 p = 0; p < kEngineControls; ++p) input.values[t][p] = own[p];
        if (overlay.count > control::overlay_capacity) overlay.count = control::overlay_capacity;
        if (overlay.stored_count > control::store_capacity) overlay.stored_count = control::store_capacity;
        if (input.muted) overlay.count = overlay.stored_count = 0;
        store(t);
        for (u32 i = 0; i < control::display_words; ++i) shown_words[t][i] = overlay.display[i];
        merge(t);
    }

    // A lock plays as written: no overlay over a parameter it holds.
    u32 kept_wanted = 0;
    for (u32 i = 0; i < wanted_count; ++i)
        if (!lock_holds(wanted[i].track, wanted[i].index)) wanted[kept_wanted++] = wanted[i];
    wanted_count = kept_wanted;

    // Keep what the machines still overlay; restore and drop the rest.
    for (u32 i = 0; i < wanted_count; ++i)
        if (Applied* a = find(wanted[i].track, wanted[i].index)) a->wanted = 1;
    u32 count = 0;
    for (u32 i = 0; i < applied_count; ++i) {
        const Applied& a = applied[i];
        if (a.wanted) applied[count++] = a;
        else if (live(a.track, a.index) == a.written && a.underlying != a.written) live_set(a.track, a.index, a.underlying);
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
