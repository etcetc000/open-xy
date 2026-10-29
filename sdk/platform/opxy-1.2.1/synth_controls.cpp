// Stock OS 1.2.1 instrument-track encoders for synths with alternate controls.
// Both hooks replace a CALL.A of a C function, so stock calls them with the C
// ABI. A press of an owned synth's encoder selects the position's next
// parameter; a turn edits the selected one, an enum one choice per detent. Everything else reaches stock as before.
#include "synth_controls.hpp"
#include <opxy/effect.hpp>
#include "shift_row.hpp"
#include "stock.hpp"

extern "C" {
void sdk_synth_press(opxy::u32) __asm__("sdk_synth_press");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
opxy::u32 sdk_effect_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_effect_turn");
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
    if (encoder >= opxy::page_controls) return;
    const u32 controller = top_controller_vtable();
    if (controller == kInstrumentController && word(kInstrumentPage) == 0 && (m = machine(byte(kSelectedTrack), t))) {
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
    Track* t;
    const SynthControls* m = index < opxy::page_controls ? send_effect(track, t) : nullptr;
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
    Track* t;
    const u32 track = selected_track();
    const SynthControls* m = index < opxy::page_controls ? send_effect(track, t) : nullptr;
    if (m && m->shift && shift_held()) {
        // While Shift is held, the shift row's choices.
        u32 fields[shift_row::kControls];
        shift_row::decode(current_value(track, shift_row::kWord), m->shift_choices, m->shift, fields);
        caption[0] = 0;
        if (index < m->shift)
            m->format(8 + index, shift_row::midpoint(fields[index], m->shift_choices[index]), caption);
        return stock<u32(u32, const char*)>(0x8d6011f0)(widget, caption);
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
    return stock<u32(u32, const char*)>(0x8d6011f0)(widget, text);
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
    for (u32 slot = 0; slot < opxy::parameter_slots; ++slot) view->slots[slot] = current_value(track, slot);
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

bool sdk_synth_value(u32 track, u32 slot, opxy::i32 value, char* text) {
    Track* t;
    const SynthControls* m = machine(track, t);
    return m && m->format(slot, value, text);
}

// Once a frame on the GUI: when an owned effect is new on a send, its controls
// 5..8 that stock has not stored, through stock's set (0x8d5acf28), which
// stores the word and sends it to the audio side (0x8d5acd9c) as it does for
// every edit. Only then: a parameter lock may later move a current value away
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
                stock<void(u32, u32, u32, opxy::i32)>(0x8d5acf28)(kProject, track, slot, current);
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
