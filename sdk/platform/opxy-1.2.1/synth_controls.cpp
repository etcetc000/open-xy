// Stock OS 1.2.1 instrument-track encoders for synths with alternate controls.
// Each hook replaces a CALL.A of a C function, so stock calls it with the C
// ABI. A press of an owned synth's encoder selects the position's next
// parameter; a turn edits the selected one, an enum one choice per detent; the
// track menu's scramble also randomizes the alternates. Everything else reaches stock as before.
#include "synth_controls.hpp"
#include <opxy/effect.hpp>
#include "shift_row.hpp"
#include "synth_shift.hpp"
#include <opxy/synth_shift.hpp>
#include "stock.hpp"
#include "drum_machines.hpp"
#include "drum_storage.hpp"
#include "drum_locks.hpp"
#include "drum_lock_store.hpp"
#include "drumline.hpp"

extern "C" {
void sdk_synth_press(opxy::u32) __asm__("sdk_synth_press");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
opxy::u32 sdk_effect_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_effect_turn");
opxy::u32 sdk_synth_scramble(opxy::u32, opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_synth_scramble");
const char* sdk_fx_title(opxy::u32, opxy::u32) __asm__("sdk_fx_title");
const char* sdk_synth_title(opxy::u32, opxy::u32) __asm__("sdk_synth_title");
}
opxy::u32 (*sdk_aux_engine)(opxy::u32) = nullptr;
bool (*sdk_aux_owns_page)(opxy::u32) = nullptr;
bool (*sdk_control_turn)(opxy::u32, opxy::u32, const opxy::i32*) = nullptr;

namespace {
using opxy::u32;
using opxy::u8;
using opxy::platform::SynthControls;
using namespace opxy::platform;

// Tracks 0..7 show the instrument track controller (GUI screen 0); its page
// word is 0 on the engine page and 1..3 on the other three pages.
const u32 kInstrumentController = 0x8400dff8;  // its vtable
const u32 kInstrumentPage = 0x080238bc;
// The auxiliary-track controller, which shows a send track's FX page: its
// vtable, laid out as the instrument controller's (its turn handler at entry 6).
const u32 kAuxController = 0x0803fb6c;
const u32 kSendTracks = 14;  // send tracks 14 and 15
const u32 kAllTracks = 16;
u32 aux_engine(u32 track) { return sdk_aux_engine ? sdk_aux_engine(track) : 0; }
// The controller on top of the GUI stack, which receives key events.
u32 top_controller_vtable() { return *stock<const u32*()>(0x8c0944b0)(); }

const SynthControls* owned(u32 engine, bool effect = false) {
    for (u32 i = 0; i < sdk_synth_control_count; ++i) {
        const SynthControls& m = sdk_synth_controls[i];
        if (m.engine() == engine && m.effect == effect) return m.ready() == 1 ? &m : nullptr;
    }
    return nullptr;
}

// The slot each position shows, per track, while the track keeps an owned
// engine: an instrument track's, or the machine holding an aux track. Engine 0
// is never a synth, so zeroed state starts on slots 0..3.
struct Track { u32 engine; u8 slots[opxy::page_controls]; };
Track tracks[kAllTracks];
// The same for the effects on the two send tracks, and whether an owned effect
// newly on a send still has its controls 5..8 to store (sdk_send_refresh).
Track sends[2];
bool unstored[2];

Track* state(u32 track, u32 engine) {
    if (track >= kAllTracks) return nullptr;
    Track& t = tracks[track];
    if (t.engine != engine) {
        t.engine = engine;
        for (u32 i = 0; i < opxy::page_controls; ++i) t.slots[i] = static_cast<u8>(i);
    }
    return &t;
}

// Whether the track's drum kit, if it is one, names a slot in 4..7 for the
// selected key's model. Any other machine has its second row, and so has every
// key of a padMix kit: its second Shift row holds the key's pad fields.
bool second_row(u32 engine, u32 track) {
    const DrumMachine* drum = sdk_drum_machine(engine);
    u8 model, values[kDrumControlCount];
    if (!drum || drum->second_shift_row() || !sdk_drum_values(track, &model, values)) return true;
    for (u32 slot = opxy::page_controls; slot < 2 * opxy::page_controls; ++slot)
        if (drum->used(model, slot)) return true;
    return false;
}

// The owned machine on the track and its state, or null. Any other engine
// clears the track's selection.
const SynthControls* machine(u32 track, Track*& t) {
    if (track >= kAllTracks) return nullptr;
    const u32 engine = track < kTracks ? track_engine(track) : aux_engine(track);
    if (track >= kTracks && !engine) return nullptr;
    const SynthControls* m = owned(engine);
    t = state(track, m ? engine : 0);
    // A drum key whose model names nothing in slots 4..7 has one row: its
    // encoders stay on (or return to) slots 0..3.
    if (m && t->slots[0] >= opxy::page_controls && !second_row(engine, track))
        for (u32 i = 0; i < opxy::page_controls; ++i) t->slots[i] = static_cast<u8>(i);
    return m;
}

// The owned synth on instrument track 0..7, or null.
const SynthControls* synth(u32 track) { return track < kTracks ? owned(track_engine(track)) : nullptr; }

// A padMix kit on instrument track 0..7 (its Shift fields lock per hit, drum_locks.hpp), or null.
const DrumMachine* pad_kit(u32 track) {
    const DrumMachine* kit = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    return kit && kit->pad_mix ? kit : nullptr;
}
// Whether a step is held for parameter locks (8d5b7e34: its step word is not -1).
bool step_held() { return stock<u32()>(0x8d5b7e34)() != 0; }
// What the selected key's send `send` plays, as a send lock starts from: 1 + its code.
opxy::i32 key_send(u32 track, u32 send) {
    opxy::i32 values[kDrumPadFields];
    sdk_drum_pad_values(track, values);
    return 1 + values[kDrumKitPadFields + send];
}
}  // namespace

// Whether Shift on a track's own screen gives an encoder to the stock sampler
// page (stockShift). Here rather than with the sample page's objects, because
// every screen asks it (screen.hpp), an effect's and a synth's included.
bool sdk_sample_stock_shift(u32 track) {
    const SynthControls* m = synth(track);
    return m && (m->stock_shift[0] | m->stock_shift[1] | m->stock_shift[2] | m->stock_shift[3]);
}

namespace {

// The owned synth on instrument track 0..7 or holding an aux track, or the
// owned effect on send track 14 or 15: the machines whose parameters stock
// edits by slot.
const SynthControls* editor(u32 track) {
    if (track < kTracks) return synth(track);
    if (const u32 engine = aux_engine(track)) return owned(engine);
    return track == 14 || track == 15 ? owned(track_engine(track), true) : nullptr;
}

// The owned effect on a send track and its selection, or null. Any other
// effect clears the selection.
const SynthControls* send_effect(u32 track, Track*& t) {
    if (track != kSendTracks && track != kSendTracks + 1) return nullptr;
    const u32 effect = track_engine(track);
    const SynthControls* m = owned(effect, true);
    t = &sends[track - kSendTracks];
    if (t->engine != (m ? effect : 0)) {
        t->engine = m ? effect : 0;
        for (u32 i = 0; i < opxy::page_controls; ++i) t->slots[i] = static_cast<u8>(i);
        unstored[track - kSendTracks] = m != nullptr;
    }
    return m;
}

// A send track's current value of a control: stock's editor reads and writes
// these (0x8d5ad5bc), all eight words of every track. GUI only: the track
// object's getter follows the GUI's pattern state.
opxy::i32 current_value(u32 track, u32 slot) {
    return *reinterpret_cast<const volatile opxy::i32*>(stock<u32(u32)>(0x8d587708)(track) + 0x3864 + slot * 4);
}
// The GUI's project, as the send page's controller passes it to stock's editor
// (8c08fad6), and a track's stored word in it: what a project saves.
const u32 kProject = 0x86bd027c;
const u32 kProjectLoading = 0x840a0528;  // nonzero while stock loads a project
opxy::i32 stored_value(u32 track, u32 slot) {
    return static_cast<opxy::i32>(word(kProject + 0x4080 + track * 0xe8 + slot * 4));
}

// A detent the track's machine takes as its own (track control), which then
// edits nothing.
bool taken(u32 track, u32 slot, u32 event) {
    return sdk_control_turn && sdk_control_turn(track, slot, reinterpret_cast<const opxy::i32*>(event));
}

// A synth's Shift row (SDK 1.13, synth_shift.hpp), on instrument tracks 0..7 only.
namespace row = opxy::platform::synth_shift;
const SynthShift* shift_of(const SynthControls* m, u32 track) {
    return m && !m->effect && track < kTracks ? m->synth_shift : nullptr;
}
// The page the track's encoders are on: a paged synth's second once a press has
// moved any position to its alternate.
u32 shift_page(const Track& t) {
    for (u32 i = 0; i < opxy::page_controls; ++i)
        if (t.slots[i] != i) return 1;
    return 0;
}
// Whether the encoders turn the row: Shift held on the selected instrument
// track's engine page.
bool shift_shown(u32 track) {
    return track == byte(kSelectedTrack) && top_controller_vtable() == kInstrumentController &&
           word(kInstrumentPage) == 0 && shift_held();
}
// An entry's field, as the track's stored word (its selected pattern's) holds it.
u32 shift_field(const SynthShift& s, u32 track, u32 entry) {
    return row::bounded(row::field(current_value(track, row::word_of(entry)), entry, s.defaults), s.choices[entry]);
}
// While a step is held, the field that step plays: its lock (a step-wide entry
// of its step-lock handle, lock_handles.hpp), else the track's.
const u32 kStepWide = opxy::platform::step_locks::kStepWide;
bool shift_lock(u32 track, u32 entry, u32& field) {
    opxy::i32 value;
    if (!step_held() || !sdk_project_locks_held_value(track, kStepWide, entry, &value) || value < 0) return false;
    field = static_cast<u32>(value);
    return true;
}
u32 shift_played(const SynthShift& s, u32 track, u32 entry) {
    u32 locked;
    return shift_lock(track, entry, locked) ? row::bounded(locked, s.choices[entry]) : shift_field(s, track, entry);
}
// The row's entry under a position on the page the encoders are on, or count.
u32 shift_entry(const SynthShift& s, const Track& t, u32 position) {
    const u32 entry = opxy::page_controls * shift_page(t) + position;
    return position < opxy::page_controls && entry < s.count ? entry : s.count;
}
// A Shift turn of a position: its entry moves by the turn's detents (event[1]),
// an amount (speed + 1) a detent up to four (event[2]), a choice one. With a step
// held, the turn locks the entry at that step, as the kit's Shift fields lock
// (lock_handles.hpp): a step-wide entry of the step's handle, field = the entry,
// unlocked when it is turned back to the track's own value. Otherwise the word
// goes to the track's stored words, which projects save, and live through
// 8d5acea8, which sends it to the audio side; not through stock's set, which
// while recording would record it as a lock, and a lock keeps only a word's top
// half.
u32 shift_turn(u32 project, u32 track, const SynthShift& s, const Track& t, u32 position, u32 event) {
    const u32 entry = shift_entry(s, t, position);
    if (!event || entry >= s.count || s.choices[entry] == row::kAbsent) return 0;
    const opxy::i32* turn = reinterpret_cast<const opxy::i32*>(event);
    opxy::i32 delta = turn[1] > 127 ? 127 : turn[1] < -127 ? -127 : turn[1];
    if (s.choices[entry] == row::kContinuous) delta *= turn[2] < 0 ? 1 : turn[2] > 3 ? 4 : turn[2] + 1;
    const opxy::i32 own = static_cast<opxy::i32>(shift_field(s, track, entry));
    const bool held = step_held();
    const opxy::i32 now = held ? static_cast<opxy::i32>(shift_played(s, track, entry)) : own;
    const opxy::i32 top = static_cast<opxy::i32>(row::top(s.choices[entry]));
    const opxy::i32 next = now + delta < 0 ? 0 : now + delta > top ? top : now + delta;
    if (next == now) return 0;
    if (held) return sdk_project_locks_held_set(track, kStepWide, entry, next != own, next) ? 1 : 0;
    const u32 index = row::word_of(entry);
    const opxy::i32 value = row::with(current_value(track, index), entry, static_cast<u32>(next), s.defaults);
    *reinterpret_cast<volatile opxy::i32*>(stock<u32(u32)>(0x8d587708)(track) + 0x3864 + index * 4) = value;
    stock<void(u32, u32, u32, opxy::i32)>(0x8d5acea8)(project, track, index, value);
    return 1;
}
// While Shift shows the row on the selected track, its title for a page
// position: the entry's, or empty beyond the row. Null otherwise.
// Whether the build has a synth with a Shift row, from the table alone (no stock
// call), so builds and tracks without one take exactly their earlier paths.
bool any_shift() {
    for (u32 i = 0; i < sdk_synth_control_count; ++i)
        if (sdk_synth_controls[i].synth_shift) return true;
    return false;
}
const char* shift_title(u32 engine, u32 position) {
    if (!any_shift()) return nullptr;
    Track* t;
    const u32 track = byte(kSelectedTrack);
    const SynthControls* m = track < kTracks ? machine(track, t) : nullptr;
    const SynthShift* s = shift_of(m, track);
    if (!s || m->engine() != engine || position >= opxy::page_controls || !shift_shown(track)) return nullptr;
    const u32 entry = shift_entry(*s, *t, position);
    return entry < s->count ? s->titles[entry] : "";
}
}

void sdk_synth_shown(u32 track, u8* slots) {
    Track* t;
    const SynthControls* m = machine(track, t);
    for (u32 i = 0; i < opxy::page_controls; ++i) slots[i] = m ? t->slots[i] : static_cast<u8>(i);
}

// Reads only the selection the screen shell refreshed for this frame.
u32 sdk_synth_title_slot(u32 engine, u32 index) {
    const u32 track = byte(kSelectedTrack);
    if (track >= kAllTracks || index >= opxy::page_controls || !engine || tracks[track].engine != engine) return index;
    return tracks[track].slots[index];
}

// 8d5466f6: only the engine screen's four titles follow alternate selection.
// The record callback itself keeps real slot names for modulation destinations.
// Stock asks for the selected track's engine, which on an aux track a machine
// holds is the machine's.
const char* sdk_synth_title(u32 engine, u32 index) {
    if (const u32 held = aux_engine(byte(kSelectedTrack))) engine = held;
    if (const char* title = shift_title(engine, index)) return title;
    return stock<const char*(u32, u32)>(0x8d506c4c)(engine, sdk_synth_title_slot(engine, index));
}

// 8c0b5b72: the GUI's key dispatcher records the click of encoder 0..3 (key
// codes 10..13, released without a turn and without shift) before passing it
// to the top controller; the instrument controller's engine page ignores it
// for every engine with alternates. On that page of an owned synth the click
// also selects the position's next parameter, or every position's on a paged synth.
void sdk_synth_press(u32 encoder) {
    stock<void(u32)>(0x8d51f1fc)(encoder);
    Track* t;
    const SynthControls* m;
    if (encoder >= opxy::page_controls) return;
    const u32 controller = top_controller_vtable();
    // Stock Drumline with a drumline_page extension: the press switches its
    // pages (drumline.hpp). Stock's own click there does nothing (8c0b2a6c, page
    // 0, a continuous word: it only clears a field, 8c0b2cca).
    if (controller == kInstrumentController && word(kInstrumentPage) == 0 && drumline_track(byte(kSelectedTrack))) {
        drumline_service()->press(byte(kSelectedTrack), encoder);
        return;
    }
    if (controller == kInstrumentController && word(kInstrumentPage) == 0 && (m = machine(byte(kSelectedTrack), t))) {
        if (m->press) {
            // A saved encoder-press action edits the track's own words; validate
            // the complete edit before publishing any word through stock.
            const u32 track = byte(kSelectedTrack);
            opxy::i32 parameters[opxy::parameter_slots];
            for (u32 i = 0; i < opxy::parameter_slots; ++i) parameters[i] = stored_value(track, i);
            m->press(encoder, parameters);
            for (u32 i = 0; i < opxy::parameter_slots; ++i)
                if (parameters[i] < 0 || (i >= opxy::page_controls && parameters[i] != stored_value(track, i))) return;
            for (u32 i = 0; i < opxy::parameter_slots; ++i)
                if (parameters[i] != stored_value(track, i))
                    stock<void(u32, u32, u32, opxy::i32)>(0x8d5acf28)(kProject, track, i, parameters[i]);
            return;
        }
        // A paged synth's or a drum kit's press moves the whole row; a slot the
        // selected key's model leaves unnamed shows blank. A key whose model has
        // no second row stays on its first.
        if (!second_row(m->engine(), byte(kSelectedTrack))) return;
        for (u32 i = 0; i < opxy::page_controls; ++i) {
            if (i != encoder && !m->page) continue;
            const u32 next = m->next[t->slots[i]];
            if (next < opxy::parameter_slots) t->slots[i] = static_cast<u8>(next);
        }
    } else if (controller == kAuxController && sdk_aux_owns_page && sdk_aux_owns_page(byte(kSelectedTrack)) &&
               (m = machine(byte(kSelectedTrack), t))) {
        // The page of a machine holding an aux track, as on an instrument track.
        for (u32 i = 0; i < opxy::page_controls; ++i) {
            if (i != encoder && !m->page) continue;
            const u32 next = m->next[t->slots[i]];
            if (next < opxy::parameter_slots) t->slots[i] = static_cast<u8>(next);
        }
    } else if (controller == kAuxController && (m = send_effect(selected_track(), t))) {
        // An owned effect's send page: the click selects the position's next
        // parameter, or every position's on a paged effect.
        for (u32 i = 0; i < opxy::page_controls; ++i) {
            if (i != encoder && !m->page) continue;
            const u32 next = m->next[t->slots[i]];
            if (next < opxy::parameter_slots) t->slots[i] = static_cast<u8>(next);
        }
    }
}

// A Shift press never reaches sdk_synth_press: stock's key layer sends a
// click released while Shift is held to the controller's shifted key handler
// (8c0b5c90 -> 8c0b5b80 -> vtable +16, 8c0b28a8), which for codes 10..13 runs
// stock's Shift-press action 8c0b270e (drum_controls_stock.cpp's hook at
// 8c0b2730 calls this). On a kit with a second Shift row it moves the whole
// row, as an unshifted press does, so Shift and a press reach the other row.
// Stock's Shift-press action does nothing for an owned kit (it acts only for
// the samplers, 2, 3 and 30, and Drumline, 4).
bool sdk_drum_shift_press(u32 track, u32 encoder) {
    Track* t;
    const SynthControls* m;
    if (track >= kTracks || track != byte(kSelectedTrack) || encoder >= opxy::page_controls) return false;
    const DrumMachine* drum = sdk_drum_machine(track_engine(track));
    if (!drum || !drum->second_shift_row() || top_controller_vtable() != kInstrumentController ||
        word(kInstrumentPage) != 0 || !(m = machine(track, t)))
        return false;
    for (u32 i = 0; i < opxy::page_controls; ++i) {
        const u32 next = m->next[t->slots[i]];
        if (next < opxy::parameter_slots) t->slots[i] = static_cast<u8>(next);
    }
    return true;
}

// 8d5ad306: stock's encoder step 8d5ad2e8, which plain and lock edits share,
// asks 8c0b2468(track, index, &scale) for a parameter's step count, or -1 when
// it is continuous. It then moves scale * count buckets, one per detent (two
// when turned fast), and stores the bucket's centre. An owned machine's enum
// slot answers its choice count with scale 1: one choice per detent.
u32 sdk_param_steps(u32 track, u32 index, u32* scale) {
    const SynthControls* m = index < opxy::parameter_slots ? editor(track) : nullptr;
    if (m && m->steps[index]) {
        *scale = 1;
        return m->steps[index];
    }
    // Stock's step table covers a send's four shown positions only, so a turn
    // would not move an owned effect's continuous slots 4..7: answer
    // "continuous" (-1) with scale left at its preset (tested on hardware).
    if (m && index >= opxy::page_controls && (track == kSendTracks || track == kSendTracks + 1))
        return 0xffffffffu;
    return stock<u32(u32, u32, u32*)>(0x8c0b2468)(track, index, scale);
}

// 8c0b15aa: the instrument controller's encoder turn edits parameter `index`
// of the selected track through stock, which owns stepping, parameter locks
// and publication. Indices 0..3 come only from the engine page's unshifted
// encoders, so an owned synth's position edits the parameter it shows.
u32 sdk_synth_turn(u32 project, u32 track, u32 index, u32 event) {
    Track* t;
    // The instrument controller maps Shift's engine-page row to slots 4..7.
    // Reinterpret it as fine editing of the same visible position, including
    // an alternate selected by a click. Other pages keep stock routing.
    const bool fine_turn = index >= 4 && index < 8 && word(kInstrumentPage) == 0 && shift_held();
    // A padMix kit shows its Shift row there (DrumMachine::shift). With a step
    // held (only then does stock route a kit's Shift turn here), PAN, LEVEL,
    // the kit's pad controls, FX I and FX II lock the step's hits
    // (drum_locks.hpp) in the lock store (drum_lock_store.hpp); the model and
    // the choke group lock nothing, so their turns do nothing.
    if (fine_turn && step_held())
        if (const DrumMachine* kit = pad_kit(track))
            if (machine(track, t)) {
                const u32 field = drum_lock(*kit, kit->shift(index - 4, t->slots[0] >= opxy::page_controls));
                if (field == kDrumNoLock) return 0;
                const opxy::i32 start = field >= kHitSend1 ? key_send(track, field - kHitSend1) : 0;
                return drum_lock_store_edit(project, track, *kit, field, event, start);
            }
    // Stock Drumline's page 2 (a drumline_page extension): with a step held, an unshifted
    // encoder locks the page's parameter for the step's hits (drumline.hpp).
    if (index < opxy::page_controls && !fine_turn && word(kInstrumentPage) == 0 && step_held() &&
        drumline_track(track) && drumline_service()->lock(track, index, event ? at<const u32>(event) : nullptr))
        return 0;
    const u32 original_index = index;
    if (fine_turn) index -= 4;
    const SynthControls* m = index < opxy::page_controls ? machine(track, t) : nullptr;
    // A synth with a Shift row (SDK 1.13): Shift turns the row, not fine steps.
    if (m && fine_turn)
        if (const SynthShift* s = shift_of(m, track)) return shift_turn(project, track, *s, *t, index, event);
    if (m) {
        index = t->slots[index];
        if (taken(track, index, event)) return 1;
        // An absent slot (an empty placeholder) turns nothing: stock gets a turn of no detents.
        if (event && index < opxy::parameter_slots && (m->absent & (1u << index))) {
            const opxy::i32 still[3] = {0, 0, 0};
            return stock<u32(u32, u32, u32, const opxy::i32*)>(0x8d5ad5bc)(project, track, index, still);
        }
        if (event && (m->continuous & (1u << index)) && fine_turn) {
            // Stock consumes all three words synchronously; keep detents and
            // acceleration intact for its parameter-lock/quantized paths.
            const opxy::i32* source = reinterpret_cast<const opxy::i32*>(event);
            const opxy::i32 fine[3] = {source[0] / 10, source[1], source[2]};
            return stock<u32(u32, u32, u32, const opxy::i32*)>(0x8d5ad5bc)(project, track, index, fine);
        }
    } else index = original_index;
    return stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(project, track, index, event);
}

// 8c08fade: auxiliary-track page, including send FX tracks 14/15. Its controller
// folds Shift into the same 0..3 positions before this call, unlike instruments.
// Ownership is checked against the FX registry, not the synth registry, except
// for a machine holding an aux track, whose page (tab 0) edits it as a synth.
u32 sdk_effect_turn(u32 project, u32 track, u32 index, u32 event) {
    Track* t;
    const SynthControls* m = nullptr;
    if (index < opxy::page_controls) m = aux_engine(track) ? machine(track, t) : send_effect(track, t);
    if (m && !m->effect && taken(track, t->slots[index], event)) return 1;
    // With Shift, an effect's shift row: one choice per detent, packed into
    // the send track's shift-row word through stock's set.
    if (m && m->shift && shift_held()) {
        if (index >= m->shift || !event) return 1;
        const opxy::i32 delta = reinterpret_cast<const opxy::i32*>(event)[0];
        u32 fields[shift_row::kControls];
        shift_row::decode(current_value(track, shift_row::kWord), m->shift_choices, m->shift, fields);
        if (delta > 0 && fields[index] + 1 < m->shift_choices[index]) ++fields[index];
        if (delta < 0 && fields[index] > 0) --fields[index];
        stock<void(u32, u32, u32, opxy::i32)>(0x8d5acf28)(project, track, shift_row::kWord,
                                                          shift_row::encode(fields, m->shift_choices, m->shift));
        return 1;
    }
    if (m) index = t->slots[index];  // the position's shown parameter
    // On an aux track, as on tracks 1-8, an absent slot turns nothing.
    if (m && event && aux_engine(track) && index < opxy::parameter_slots && (m->absent & (1u << index))) {
        const opxy::i32 still[3] = {0, 0, 0};
        return stock<u32(u32, u32, u32, const opxy::i32*)>(0x8d5ad5bc)(project, track, index, still);
    }
    if (m && event && (m->continuous & (1u << index)) && shift_held()) {
        const opxy::i32* source = reinterpret_cast<const opxy::i32*>(event);
        const opxy::i32 fine[3] = {source[0] / 10, source[1], source[2]};
        return stock<u32(u32, u32, u32, const opxy::i32*)>(0x8d5ad5bc)(project, track, index, fine);
    }
    return stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(project, track, index, event);
}

// 8d53b636: the send page sets each position's value text (stock's "%02i" of
// the word) with 0x8d6011f0(widget, text). An owned effect on a send track may
// name the value instead; the text is copied before this returns.
// The page then draws its bars from the vector at [P5 + 4]. While any position
// shows an alternate, or Shift shows a shift row, the call for position 0 returns
// a stand-in vector of the shown words, which the hook stub puts in P5 for the
// rest of that draw, so the bars follow them too. Zero keeps stock's vector, which
// is never written.
namespace {
struct ShownVector { u32 allocator, begin, end, capacity; };
opxy::i32 shown_words[opxy::parameter_slots];
ShownVector shown_vector;
}
u32 sdk_fx_value_text(u32 widget, const char* text, u32 index, opxy::i32 value) {
    static char caption[16];
    Track* t;
    const u32 track = selected_track();
    const SynthControls* m = index < opxy::page_controls ? send_effect(track, t) : nullptr;
    // While Shift is held, the shift row's choices, in the captions and the bars.
    const bool row = m && m->shift && shift_held();
    u32 fields[shift_row::kControls];
    if (row) shift_row::decode(current_value(track, shift_row::kWord), m->shift_choices, m->shift, fields);
    u32 stand_in = 0;
    if (m && index == 0) {
        bool alternate = row;
        for (u32 i = 0; i < opxy::page_controls; ++i) {
            const opxy::i32 word = !row ? current_value(track, t->slots[i])
                                 : i < m->shift ? shift_row::midpoint(fields[i], m->shift_choices[i]) : 0;
            shown_words[i] = shown_words[i + opxy::page_controls] = word;
            alternate = alternate || t->slots[i] != i;
        }
        if (alternate) {
            shown_vector.allocator = 0;
            shown_vector.begin = static_cast<u32>(reinterpret_cast<Address>(shown_words));
            shown_vector.end = shown_vector.capacity = shown_vector.begin + sizeof(shown_words);
            stand_in = static_cast<u32>(reinterpret_cast<Address>(&shown_vector));
        }
    }
    if (row) {
        caption[0] = 0;
        if (index < m->shift)
            m->format(8 + index, shift_row::midpoint(fields[index], m->shift_choices[index]), caption);
        // The stub takes a nonzero result as the vector, so never the text call's own.
        stock<u32(u32, const char*)>(0x8d6011f0)(widget, caption);
        return stand_in;
    }
    if (m) {
        // A position showing an alternate reads its current value.
        const u32 slot = t->slots[index];
        if (slot != index) value = current_value(track, slot);
        if (m->format(slot, value, caption)) {
            text = caption;
        } else if (slot != index) {
            // Stock's caption: the word as 00..99.
            const u32 n = value < 0 ? 0 : (static_cast<u32>(value) >> 8) * 100 >> 23;
            caption[0] = static_cast<char>('0' + n / 10 % 10);
            caption[1] = static_cast<char>('0' + n % 10);
            caption[2] = 0;
            text = caption;
        }
    }
    stock<u32(u32, const char*)>(0x8d6011f0)(widget, text);
    return stand_in;
}


// 8d53b642: the send page names each position through the effect record's
// parameter-name callback (0x8d506c90). An owned effect's position names its
// shown parameter; the record itself keeps naming raw slots.
const char* sdk_fx_title(u32 effect, u32 index) {
    Track* t;
    const SynthControls* m = index < opxy::page_controls ? send_effect(selected_track(), t) : nullptr;
    if (m && m->engine() == effect && m->shift && shift_held()) return index < m->shift ? m->shift_titles[index] : "";
    if (m && m->engine() == effect) index = t->slots[index];
    return stock<const char*(u32, u32)>(0x8d506c90)(effect, index);
}

bool sdk_send_view(u32 track, SendView* view) {
    Track* t;
    const SynthControls* m = send_effect(track, t);
    if (!m) return false;
    // Stock's send page (8d53b594) draws a send's four controls from
    // 8d4d72a8(track), as a synth's page does: each word from
    // 8d5ac9dc(project, track, i, 0), as an LFO modulates it (tracks up to 7
    // read 8d4d7a38 instead). The page follows them too; the alternates, which
    // no LFO reaches, and a missing vector read stock's editor's current values.
    struct Vector { u32 owner, begin, end; };
    const Vector* live = stock<const Vector*(u32)>(0x8d4d72a8)(track);
    const bool whole = live && live->begin && !(live->begin & 3) && live->end >= live->begin + 4 * opxy::page_controls;
    for (u32 slot = 0; slot < opxy::parameter_slots; ++slot)
        view->slots[slot] = whole && slot < opxy::page_controls ? reinterpret_cast<const opxy::i32*>(live->begin)[slot]
                                                                 : current_value(track, slot);
    u32 fields[shift_row::kControls] = {};
    if (m->shift) shift_row::decode(current_value(track, shift_row::kWord), m->shift_choices, m->shift, fields);
    for (u32 i = 0; i < shift_row::kControls; ++i) view->choices[i] = static_cast<u8>(fields[i]);
    view->shift = m->shift && shift_held();
    for (u32 i = 0; i < opxy::page_controls; ++i) {
        const u32 slot = t->slots[i];
        char* text = view->texts[i];
        view->shown[i] = static_cast<u8>(slot);
        view->values[i] = view->slots[slot];
        text[0] = 0;
        if (view->shift) {
            view->titles[i] = i < m->shift ? m->shift_titles[i] : "";
            if (i < m->shift) m->format(8 + i, shift_row::midpoint(fields[i], m->shift_choices[i]), text);
            continue;
        }
        view->titles[i] = stock<const char*(u32, u32)>(0x8d506c90)(m->engine(), slot);
        if (!m->format(slot, view->values[i], text)) {
            // Stock's caption: the word as 00..99.
            const opxy::i32 value = view->values[i];
            const u32 n = value < 0 ? 0 : (static_cast<u32>(value) >> 8) * 100 >> 23;
            text[0] = static_cast<char>('0' + n / 10 % 10);
            text[1] = static_cast<char>('0' + n % 10);
            text[2] = 0;
        }
    }
    return true;
}

// 8c0b541c: the track menu's scramble (hold the track key) randomizes the
// selected track's parameters 0..3 and its other preset groups, but never 4..7,
// where an owned synth keeps its alternates; this is its last write, through
// stock's set. After it, give every alternate (a slot 4..7 that
// a press reaches, so next[s] != s) except an empty placeholder a value from
// stock's own generator (8c0b5230), as stock does for slots 0..3.
u32 sdk_synth_scramble(u32 project, u32 track, u32 index, opxy::i32 value) {
    const u32 result = set_parameter(project, track, index, value);
    // A padMix kit's per-hit lock words keep a base with no lock (stock's
    // scramble, 8c0b52fc, leaves 36..41 alone; this keeps it so whatever it does).
    if (pad_kit(track)) drum_lock_store_neutral(project, track);
    const SynthControls* m = synth(track);
    if (!m) return result;
    for (u32 s = opxy::page_controls; s < opxy::parameter_slots; ++s)
        if (m->next[s] != s && !(m->absent & (1u << s)))
            set_parameter(project, track, s, stock<opxy::i32(u32, u32)>(0x8c0b5230)(s, 0));
    return result;
}

bool sdk_synth_value(u32 track, u32 slot, opxy::i32 value, char* text) {
    Track* t;
    const SynthControls* m = machine(track, t);
    // While Shift shows a synth's row, the position showing `slot` captions its
    // entry (slot 8 + entry of the machine's format); beyond the row it is blank.
    if (const SynthShift* s = shift_of(m, track)) {
        if (shift_shown(track)) {
            u32 position = 0;
            while (position < opxy::page_controls && t->slots[position] != slot) ++position;
            const u32 entry = shift_entry(*s, *t, position);
            text[0] = 0;
            if (entry < s->count && s->choices[entry] != row::kAbsent)
                m->format(8 + entry, row::q31(shift_played(*s, track, entry), s->choices[entry]), text);
            return true;
        }
    }
    return m && m->format(slot, value, text);
}

// The synth's Shift row as its display reads it (opxy/synth_shift.hpp). GUI only.
extern "C" const opxy::draw::ShiftRow* opxy_synth_shift(u32 track) {
    static opxy::draw::ShiftRow view;
    view.held = 0;
    view.line = 0;
    for (u32 i = 0; i < opxy::draw::shift_entries; ++i) view.values[i] = 0;
    Track* t;
    const SynthControls* m = track < kTracks ? machine(track, t) : nullptr;
    const SynthShift* s = shift_of(m, track);
    if (!s) return &view;
    view.line = shift_page(*t);
    view.held = shift_shown(track) ? 1 : 0;
    for (u32 i = 0; i < s->count && i < opxy::draw::shift_entries; ++i)
        view.values[i] = row::q31(shift_played(*s, track, i), s->choices[i]);
    return &view;
}

// Whether an engine is an owned synth with a Shift row (the drums service's preset
// hooks, drum_machines.cpp sdk_preset_alias, and the step-lock client, drum_locks.cpp).
bool sdk_synth_shift_present() { return any_shift(); }
bool sdk_synth_shift_engine(u32 engine) {
    const SynthControls* m = owned(engine);
    return m && m->synth_shift;
}
// The preset sites' class (drum_hooks.s): an owned kit, and a synth whose Shift row a
// preset keeps in its first pad record, as Drumline (4).
u32 sdk_preset_alias(u32 engine) { return sdk_drum_machine(engine) || sdk_synth_shift_engine(engine) ? 4 : engine; }

namespace {
// Presets keep the row as a drum kit's preset keeps a key (drum_storage.hpp): the
// pattern's first pad record (key 53), its model byte marked type 7 and its four
// parameter words holding the eight 7-bit fields, which stock's preset writer
// exports as a region's `model` and `params` (top halves) for a Drumline-classified
// track and its import restores (<< 16). The drums service's preset hooks classify a
// synth with a Shift row as Drumline at those sites only (drum_hooks.s), never at
// import's neutral track offsets (8d4e39c0), so its eight controls load as before.
unsigned char* shift_record(Address pattern) { return at<unsigned char>(pattern + kDrumPatternPadsAt); }
// The pattern's machine is read through its pointer, as the kit glue reads it
// (drum_synthesis_stock.cpp), never truncated to a stock address word.
const SynthShift* pattern_shift(Address pattern) {
    const SynthControls* m = pattern ? owned(*at<const volatile u32>(pattern + kDrumPatternMachine)) : nullptr;
    return m && !m->effect ? m->synth_shift : nullptr;
}
}

// 8d4e74b0 (drum_hooks.s): the preset writer about to choose a pattern's regions.
// A synth with a Shift row gets its row in the first pad record, from the stored
// words, before stock serializes it; then it is classified as Drumline (4).
u32 sdk_synth_shift_export(u32 engine, u32 object) {
    const Address pattern = static_cast<Address>(object);
    const SynthShift* s = pattern_shift(pattern);
    if (!s || *at<const volatile u32>(pattern + kDrumPatternMachine) != engine) return engine;
    const volatile opxy::i32* stored = at<const volatile opxy::i32>(pattern + kDrumLockBase);
    u8 fields[row::kEntries];
    for (u32 i = 0; i < row::kEntries; ++i)
        fields[i] = static_cast<u8>(row::bounded(row::field(stored[row::word_of(i)], i, s->defaults), s->choices[i]));
    unsigned char* record = shift_record(pattern);
    opxy::i32 words[kDrumStorageWords];
    if (drum_pack(0, fields, words)) {
        for (u32 i = 0; i < kDrumStorageWords; ++i) reinterpret_cast<opxy::i32*>(record)[1 + i] = words[i];
        record[24] = drum_mark(record[24]);
    }
    return 4;
}

// After stock's preset import (the drums service's hooks of its three callers,
// drum_synthesis_stock.cpp): a synth with a Shift row takes the row its first pad
// record now holds, when marked, into the stored words and live through 8d5acea8.
// A preset without one (saved before SDK 1.13, or on stock) leaves the words import
// cleared, which play the defaults.
bool sdk_synth_shift_import(u32 track) {
    if (track >= kTracks) return false;
    const Address pattern = stock<Address(u32)>(0x8d587708)(track);
    const SynthShift* s = pattern_shift(pattern);
    if (!s) return false;
    const unsigned char* record = shift_record(pattern);
    u8 model, fields[row::kEntries];
    if (!drum_marked(record[24]) || !drum_unpack(reinterpret_cast<const opxy::i32*>(record) + 1, &model, fields)) return true;
    for (u32 w = 0; w < 2; ++w) {
        opxy::i32 value = 0;
        for (u32 i = 0; i < row::kPerWord; ++i)
            value = row::with(value, w * row::kPerWord + i, row::bounded(fields[w * row::kPerWord + i], s->choices[w * row::kPerWord + i]), s->defaults);
        const u32 index = row::kFirstWord + w;
        *at<volatile opxy::i32>(pattern + kDrumLockBase + index * 4) = value;
        stock<void(u32, u32, u32, opxy::i32)>(0x8d5acea8)(kProject, track, index, value);
    }
    return true;
}

// The sequencer's step hook (lock_handles.cpp sdk_lock_step, through the step-lock
// client, drum_locks.cpp), after stock's lock pass for a note's step: the track's
// live words 36 and 37 become the row as that step plays it, each field its lock
// (mask, values by entry) or the track's own, and are published (8d5acea8) before
// the note starts. The pass has just written the handle into live word 37; stock's
// copy of the stored words into the live ones at the next step note (8d5accf8)
// brings back the track's own row.
void sdk_synth_shift_play(u32 project, u32 track, u32 mask, const opxy::i32* values) {
    if (track >= kTracks) return;
    const Address pattern = stock<Address(u32)>(0x8d587708)(track);
    const SynthShift* s = pattern_shift(pattern);
    if (!s) return;
    const volatile opxy::i32* stored = at<const volatile opxy::i32>(pattern + kDrumLockBase);
    for (u32 w = 0; w < 2; ++w) {
        const u32 index = row::kFirstWord + w;
        opxy::i32 value = stored[index];
        for (u32 i = w * row::kPerWord; i < (w + 1) * row::kPerWord; ++i)
            if (mask >> i & 1u)
                value = row::with(value, i, row::bounded(values[i] < 0 ? 0u : static_cast<u32>(values[i]), s->choices[i]), s->defaults);
        stock<void(u32, u32, u32, opxy::i32)>(0x8d5acea8)(project, track, index, value);
    }
}

// Once a frame on the GUI: when an owned effect is new on a send, its controls
// 5..8 that stock has not stored, through stock's set. Only then: a parameter
// lock may later move a current value away
// from the stored one on purpose.
void sdk_send_refresh() {
    // The selected padMix kit's per-hit lock words keep a base with no lock,
    // whatever put another there (a track newly the kit); never while a project loads.
    const u32 selected = byte(kSelectedTrack);
    if (!word(kProjectLoading) && pad_kit(selected)) drum_lock_store_neutral(kProject, selected);
    // A step note's step-lock handle that no note start took expires (lock_handles.cpp).
    sdk_project_locks_frame();
    for (u32 s = 0; s < 2; ++s) {
        Track* t;
        const u32 track = kSendTracks + s;
        if (!send_effect(track, t) || !unstored[s]) continue;
        unstored[s] = false;
        for (u32 slot = opxy::page_controls; slot < opxy::parameter_slots; ++slot) {
            const opxy::i32 current = current_value(track, slot);
            if (stored_value(track, slot) != current)
                set_parameter(kProject, track, slot, current);
        }
    }
}

namespace opxy { namespace platform {
void control_tenths(char* text, u32 tenths, const char* suffix) {
    u32 n = 0, whole = tenths / 10;
    char reverse[10];
    do { reverse[n++] = static_cast<char>('0' + whole % 10); whole /= 10; } while (whole);
    while (n) *text++ = reverse[--n];
    *text++ = '.';
    *text++ = static_cast<char>('0' + tenths % 10);
    while (*suffix) *text++ = *suffix++;
    *text = 0;
}
void control_division(char* text, i32 value) {
    static const char* const names[8] = {"1/32", "1/32 dotted", "1/16", "1/16 dotted",
                                         "1/8", "1/8 dotted", "1/4", "1/2"};
    for (const char* name = names[division(value)]; (*text = *name); ++text, ++name) {}
}
void control_label(char* text, i32 value, const char* const* labels, u32 count) {
    for (const char* label = labels[control_choice(value, count)]; (*text = *label); ++text, ++label) {}
}
u32 control_ratio(i32 value, const u32* anchors, u32 count) {
    const u32 q15 = value < 0 ? 0 : static_cast<u32>(value) >> 16;
    const u32 position = q15 * (count - 1), index = position >> 15;
    return anchors[index] + ((anchors[index + 1] - anchors[index]) * (position & 32767) + 16384) / 32768;
}
}}
