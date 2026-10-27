// Stock OS 1.2.0 instrument-track encoders for synths with alternate controls.
// Both hooks replace a CALL.A of a C function, so stock calls them with the C
// ABI. A press of an owned synth's encoder selects the position's next
// parameter; a turn edits the selected one. Everything else reaches stock as before.
#include "synth_controls.hpp"
#include <opxy/effect.hpp>
#include "stock.hpp"
#include "drum_machines.hpp"
#include "drum_storage.hpp"

extern "C" {
void sdk_synth_press(opxy::u32) __asm__("sdk_synth_press");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
opxy::u32 sdk_effect_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_effect_turn");
const char* sdk_synth_title(opxy::u32, opxy::u32) __asm__("sdk_synth_title");
}

namespace {
using opxy::u32;
using opxy::u8;
using opxy::platform::SynthControls;
using namespace opxy::platform;

// Tracks 0..7 show the instrument track controller (GUI screen 0); its page
// word is 0 on the engine page and 1..3 on the other three pages.
const u32 kInstrumentController = 0x8400b208;  // its vtable
const u32 kInstrumentPage = 0x08023c74;
// The controller on top of the GUI stack, which receives key events.
u32 top_controller_vtable() { return *stock<const u32*()>(0x8c095008)(); }

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

Track* state(u32 track, u32 engine) {
    if (track >= kTracks) return nullptr;
    Track& t = tracks[track];
    if (t.engine != engine) {
        t.engine = engine;
        for (u32 i = 0; i < opxy::page_controls; ++i) t.slots[i] = static_cast<u8>(i);
    }
    return &t;
}

// The owned machine on the track and its state, or null. Any other engine
// clears the track's selection.
const SynthControls* machine(u32 track, Track*& t) {
    if (track >= kTracks) return nullptr;
    const u32 engine = track_engine(track);
    const SynthControls* m = owned(engine);
    t = state(track, m ? engine : 0);
    if (m) {
        const DrumMachine* drum = sdk_drum_machine(engine);
        u8 model, values[kDrumControlCount];
        if (drum && sdk_drum_values(track, &model, values)) {
            const u32 count = drum->count(model);
            // A different pad/model can have fewer parameters than the one
            // that selected this alternate. Return that position to primary.
            for (u32 i = 0; i < opxy::page_controls; ++i)
                if (t->slots[i] >= count) t->slots[i] = static_cast<u8>(i);
        }
    }
    return m;
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

// 8d545b2e: only the engine screen's four titles follow alternate selection.
// The record callback itself keeps real slot names for modulation destinations.
const char* sdk_synth_title(u32 engine, u32 index) {
    return stock<const char*(u32, u32)>(0x8d5065fc)(engine, sdk_synth_title_slot(engine, index));
}

// 8c0b66d2: the GUI's key dispatcher records the click of encoder 0..3 (key
// codes 10..13, released without a turn and without shift) before passing it
// to the top controller; the instrument controller's engine page ignores it
// for every engine with alternates. On that page of an owned synth the click
// also selects the position's next parameter.
void sdk_synth_press(u32 encoder) {
    stock<void(u32)>(0x8d51e634)(encoder);
    Track* t;
    const SynthControls* m;
    if (encoder < opxy::page_controls && top_controller_vtable() == kInstrumentController &&
        word(kInstrumentPage) == 0 && (m = machine(byte(kSelectedTrack), t))) {
        const u32 next = m->next[t->slots[encoder]];
        const DrumMachine* drum = sdk_drum_machine(m->engine());
        u8 model, values[kDrumControlCount];
        if (drum && (!sdk_drum_values(byte(kSelectedTrack), &model, values) || next >= drum->count(model))) return;
        if (next < opxy::parameter_slots) t->slots[encoder] = static_cast<u8>(next);
    }
}

// 8c0b210a: the instrument controller's encoder turn edits parameter `index`
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
        if (event && (m->continuous & (1u << index)) && fine_turn) {
            // Stock consumes all three words synchronously; keep detents and
            // acceleration intact for its parameter-lock/quantized paths.
            const opxy::i32* source = reinterpret_cast<const opxy::i32*>(event);
            const opxy::i32 fine[3] = {source[0] / 10, source[1], source[2]};
            return stock<u32(u32, u32, u32, const opxy::i32*)>(0x8d5ab8ec)(project, track, index, fine);
        }
    } else index = original_index;
    return stock<u32(u32, u32, u32, u32)>(0x8d5ab8ec)(project, track, index, event);
}

// 8c090636: auxiliary-track page, including send FX tracks 14/15. Its controller
// folds Shift into the same 0..3 positions before this call, unlike instruments.
// Ownership is checked against the FX registry, not the synth registry.
u32 sdk_effect_turn(u32 project, u32 track, u32 index, u32 event) {
    const SynthControls* m = (track == 14 || track == 15) && index < 4 ? owned(track_engine(track), true) : nullptr;
    if (m && event && (m->continuous & (1u << index)) && shift_held()) {
        const opxy::i32* source = reinterpret_cast<const opxy::i32*>(event);
        const opxy::i32 fine[3] = {source[0] / 10, source[1], source[2]};
        return stock<u32(u32, u32, u32, const opxy::i32*)>(0x8d5ab8ec)(project, track, index, fine);
    }
    return stock<u32(u32, u32, u32, u32)>(0x8d5ab8ec)(project, track, index, event);
}

// 8d53aa6e: the send page sets each position's value text (stock's "%02i" of
// the word) with 0x8d5ff080(widget, text). An owned effect on a send track may
// name the value instead; the text is copied before this returns.
u32 sdk_fx_value_text(u32 widget, const char* text, u32 index, opxy::i32 value) {
    static char caption[16];
    const u32 track = selected_track();
    const SynthControls* m = (track == 14 || track == 15) && index < 4 ? owned(track_engine(track), true) : nullptr;
    if (m && m->format(index, value, caption)) text = caption;
    return stock<u32(u32, const char*)>(0x8d5ff080)(widget, text);
}

// Stock steps a control by its engine's choice count, which it knows only for
// stock engines (8c0b2fc8); for every other engine it steps about 1% a detent,
// so a five-way enum took twenty detents a choice. As stock's stepped controls
// and the LFO selector do: a choice a detent, two on an accelerated turn.
// Drum kits step their own integers (drum_synthesis_stock.cpp).
bool sdk_synth_step(u32 track, u32 slot, opxy::i32 value, const opxy::i32* event, opxy::i32& stepped) {
    if (!event || track >= kTracks || slot >= opxy::parameter_slots) return false;
    const SynthControls* m = owned(track_engine(track));
    if (!m || sdk_drum_machine(m->engine())) return false;
    const u32 count = m->choices[slot];
    if (count < 2 || count > 128) return false;
    opxy::i32 delta = event[1];
    if (delta > 8) delta = 8;
    if (delta < -8) delta = -8;
    opxy::i32 choice = static_cast<opxy::i32>(((value < 0 ? 0u : static_cast<u32>(value) >> 16) * count) >> 15);
    choice += delta * (event[2] > 0 ? 2 : 1);
    if (choice < 0) choice = 0;
    if (choice >= static_cast<opxy::i32>(count)) choice = static_cast<opxy::i32>(count) - 1;
    stepped = drum_model_q31(static_cast<u32>(choice), count);
    return true;
}

bool sdk_synth_value(u32 track, u32 slot, opxy::i32 value, char* text) {
    Track* t;
    const SynthControls* m = machine(track, t);
    return m && m->format(slot, value, text);
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
u32 control_ratio(i32 value, const u32* anchors, u32 count) {
    const u32 q15 = value < 0 ? 0 : static_cast<u32>(value) >> 16;
    const u32 position = q15 * (count - 1), index = position >> 15;
    return anchors[index] + ((anchors[index + 1] - anchors[index]) * (position & 32767) + 16384) / 32768;
}
}}
