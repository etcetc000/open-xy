// Stock OS 1.2.1 instrument-track encoders for synths with alternate controls.
// Each hook replaces a CALL.A of a C function, so stock calls it with the C
// ABI. A press of an owned synth's encoder selects the position's next
// parameter; a turn edits the selected one, an enum one choice per detent; the
// track menu's scramble also randomizes the alternates. Everything else reaches stock as before.
#include "synth_controls.hpp"
#include <opxy/effect.hpp>
#include "stock.hpp"
#include "drum_machines.hpp"
#include "drum_storage.hpp"

extern "C" {
void sdk_synth_press(opxy::u32) __asm__("sdk_synth_press");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
opxy::u32 sdk_effect_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_effect_turn");
opxy::u32 sdk_synth_scramble(opxy::u32, opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_synth_scramble");
const char* sdk_fx_title(opxy::u32, opxy::u32) __asm__("sdk_fx_title");
const char* sdk_synth_title(opxy::u32, opxy::u32) __asm__("sdk_synth_title");
}

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
// engine. Engine 0 is never a synth, so zeroed state starts on slots 0..3.
struct Track { u32 engine; u8 slots[opxy::page_controls]; };
Track tracks[kTracks];
// The same for the effects on the two send tracks, and whether an owned effect
// newly on a send still has its controls 5..8 to store (sdk_send_refresh).
Track sends[2];
bool unstored[2];

Track* state(u32 track, u32 engine) {
    if (track >= kTracks) return nullptr;
    Track& t = tracks[track];
    if (t.engine != engine) {
        t.engine = engine;
        for (u32 i = 0; i < opxy::page_controls; ++i) t.slots[i] = static_cast<u8>(i);
    }
    return &t;
}

// Whether the track's drum kit, if it is one, names a slot in 4..7 for the
// selected key's model. Any other machine has its second row.
bool second_row(u32 engine, u32 track) {
    const DrumMachine* drum = sdk_drum_machine(engine);
    u8 model, values[kDrumControlCount];
    if (!drum || !sdk_drum_values(track, &model, values)) return true;
    for (u32 slot = opxy::page_controls; slot < 2 * opxy::page_controls; ++slot)
        if (drum->used(model, slot)) return true;
    return false;
}

// The owned machine on the track and its state, or null. Any other engine
// clears the track's selection.
const SynthControls* machine(u32 track, Track*& t) {
    if (track >= kTracks) return nullptr;
    const u32 engine = track_engine(track);
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

// The owned synth on instrument track 0..7, or the owned effect on send track
// 14 or 15: the machines whose parameters stock edits by slot.
const SynthControls* editor(u32 track) {
    if (track < kTracks) return synth(track);
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
opxy::i32 stored_value(u32 track, u32 slot) {
    return *reinterpret_cast<const volatile opxy::i32*>(kProject + 0x4080 + track * 0xe8 + slot * 4);
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
    if (track >= kTracks || index >= opxy::page_controls || !engine || tracks[track].engine != engine) return index;
    return tracks[track].slots[index];
}

// 8d5466f6: only the engine screen's four titles follow alternate selection.
// The record callback itself keeps real slot names for modulation destinations.
const char* sdk_synth_title(u32 engine, u32 index) {
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
    if (controller == kInstrumentController && word(kInstrumentPage) == 0 && (m = machine(byte(kSelectedTrack), t))) {
        // A paged synth's or a drum kit's press moves the whole row; a slot the
        // selected key's model leaves unnamed shows blank. A key whose model has
        // no second row stays on its first.
        if (!second_row(m->engine(), byte(kSelectedTrack))) return;
        for (u32 i = 0; i < opxy::page_controls; ++i) {
            if (i != encoder && !m->page) continue;
            const u32 next = m->next[t->slots[i]];
            if (next < opxy::parameter_slots) t->slots[i] = static_cast<u8>(next);
        }
    } else if (controller == kAuxController && (m = send_effect(selected_track(), t))) {
        // An owned effect's send page: the click selects the position's next parameter.
        const u32 next = m->next[t->slots[encoder]];
        if (next < opxy::parameter_slots) t->slots[encoder] = static_cast<u8>(next);
    }
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
    const u32 original_index = index;
    if (fine_turn) index -= 4;
    const SynthControls* m = index < opxy::page_controls ? machine(track, t) : nullptr;
    if (m) {
        index = t->slots[index];
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
// Ownership is checked against the FX registry, not the synth registry.
u32 sdk_effect_turn(u32 project, u32 track, u32 index, u32 event) {
    Track* t;
    const SynthControls* m = index < opxy::page_controls ? send_effect(track, t) : nullptr;
    if (m) index = t->slots[index];  // the position's shown parameter
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
// shows an alternate, the call for position 0 returns a stand-in vector of the
// shown words, which the hook stub puts in P5 for the rest of that draw, so the
// bars follow the alternates too. Zero keeps stock's vector, which is never written.
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
    u32 stand_in = 0;
    if (m && index == 0) {
        bool alternate = false;
        for (u32 i = 0; i < opxy::page_controls; ++i) {
            const opxy::i32 word = current_value(track, t->slots[i]);
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
    if (m && m->engine() == effect) index = t->slots[index];
    return stock<const char*(u32, u32)>(0x8d506c90)(effect, index);
}

// 8c0b541c: the track menu's scramble (hold the track key) randomizes the
// selected track's parameters 0..3 and its other preset groups, but never 4..7,
// where an owned synth keeps its alternates; this is its last write, through
// stock's set. After it, give every alternate (a slot 4..7 that
// a press reaches, so next[s] != s) except an empty placeholder a value from
// stock's own generator (8c0b5230), as stock does for slots 0..3.
u32 sdk_synth_scramble(u32 project, u32 track, u32 index, opxy::i32 value) {
    const u32 result = set_parameter(project, track, index, value);
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
    return m && m->format(slot, value, text);
}

// Once a frame on the GUI: when an owned effect is new on a send, its controls
// 5..8 that stock has not stored, through stock's set. Only then: a parameter
// lock may later move a current value away
// from the stored one on purpose.
void sdk_send_refresh() {
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
u32 control_choice(i32 value, u32 count) {
    return value < 0 ? 0 : ((static_cast<u32>(value) >> 16) * count) >> 15;
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
