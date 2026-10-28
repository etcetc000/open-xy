// Stock OS 1.2.1 drawing-instrument encoders (synth and sample) with alternates.
// Both hooks replace a CALL.A of a C function, so stock calls them with the C
// ABI. A press of an owned instrument's encoder selects the position's next
// parameter; a turn edits the selected one, an enum one choice per detent. Everything else reaches stock as before.
#include "synth_controls.hpp"
#include <opxy/effect.hpp>
#include "stock.hpp"

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
const u32 kInstrumentController = 0x8400dff8;  // its vtable
const u32 kInstrumentPage = 0x080238bc;
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
    return m;
}

// The owned synth on instrument track 0..7, or the owned effect on send track
// 14 or 15: the machines whose parameters stock edits by slot.
const SynthControls* editor(u32 track) {
    if (track < kTracks) return owned(track_engine(track));
    return track == 14 || track == 15 ? owned(track_engine(track), true) : nullptr;
}

u32 shown(u32 track, u32 position) {
    Track* t;
    return position < opxy::page_controls && machine(track, t) ? t->slots[position] : position;
}
}

void sdk_synth_shown(u32 track, u8* slots) {
    for (u32 i = 0; i < opxy::page_controls; ++i) slots[i] = static_cast<u8>(shown(track, i));
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
    if (encoder < opxy::page_controls && top_controller_vtable() == kInstrumentController &&
        word(kInstrumentPage) == 0 && (m = machine(byte(kSelectedTrack), t))) {
        for (u32 i = 0; i < opxy::page_controls; ++i) {
            if (i != encoder && !m->page) continue;
            const u32 next = m->next[t->slots[i]];
            if (next < opxy::parameter_slots) t->slots[i] = static_cast<u8>(next);
        }
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
    const SynthControls* m = (track == 14 || track == 15) && index < 4 ? owned(track_engine(track), true) : nullptr;
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
u32 sdk_fx_value_text(u32 widget, const char* text, u32 index, opxy::i32 value) {
    static char caption[16];
    const u32 track = selected_track();
    const SynthControls* m = (track == 14 || track == 15) && index < 4 ? owned(track_engine(track), true) : nullptr;
    if (m && m->format(index, value, caption)) text = caption;
    return stock<u32(u32, const char*)>(0x8d6011f0)(widget, text);
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
