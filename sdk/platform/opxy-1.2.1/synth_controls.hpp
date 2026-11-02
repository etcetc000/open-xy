// Owned synth/FX controls: generated metadata for alternate selection,
// continuous fine turns, enum steps and synth captions; GUI state lives in
// synth_controls.cpp.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// A synth or effect. Pressing a synth encoder moves its
// position from slot s to next[s]; each position starts on its own slot 0..3,
// and a slot no position reaches maps to itself. A paged synth's press moves
// all four positions, switching between its two pages. steps[s] is an enum
// slot's choice count (2..255; 0 for other slots and larger enums): each
// detent moves one choice. In a browser kit an unselected machine's ready
// resolves to a function returning 0.
struct SynthControls {
    const char* id;  // absolute symbol: the engine or effect ID, supplied at link time
    int (*ready)();
    u8 next[parameter_slots];
    u32 continuous;
    u8 steps[parameter_slots];
    bool (*format)(u32 slot, i32 value, char* text);
    bool effect;
    bool page;
    u8 hidden_pages;  // stock instrument pages the synth hides, by page index (bit 1 envelope, 2 filter)
    u8 absent;  // bit s: slot s is an empty placeholder, shown blank and never edited
    // An effect's shift row (SDK 1.7, shift_row.hpp): its control count, each
    // one's choice count, and titles; formats caption it as slots 8..11.
    u8 shift;
    u8 shift_choices[4];
    const char* const* shift_titles;
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
};
// An owned effect on send track 14 or 15 as its own page shows it (SDK 1.7):
// each position's shown slot, value, title and caption, every control's
// current value, and its shift row. While Shift is held on an effect with a
// shift row, the titles and captions are the row's. GUI only.
struct SendView {
    u8 shown[page_controls];
    i32 values[page_controls];
    i32 slots[parameter_slots];
    const char* titles[page_controls];
    char texts[page_controls][16];
    bool shift;
    u8 choices[4];
};
// GUI-only, bounded one-decimal formatting. Caller provides 16 bytes.
void control_tenths(char* text, u32 tenths, const char* suffix);
u32 control_ratio(i32 value, const u32* anchors, u32 count);
// The choice of `count` a stored enum value selects, as stock's stepped edits
// store it; and that choice's label (at most 15 characters).
u32 control_choice(i32 value, u32 count);
void control_label(char* text, i32 value, const char* const* labels, u32 count);
// Stock delay's name for the note length a division control selects.
void control_division(char* text, i32 value);
}}

extern "C" {
extern const opxy::platform::SynthControls sdk_synth_controls[] __asm__("sdk_synth_controls");
extern const opxy::u32 sdk_synth_control_count __asm__("sdk_synth_control_count");
// The parameter slot each of a track's four page positions edits and shows:
// 0..3 unless an owned synth's encoder press selected an alternate. GUI only.
void sdk_synth_shown(opxy::u32 track, opxy::u8* shown) __asm__("sdk_synth_shown");
bool sdk_synth_value(opxy::u32 track, opxy::u32 slot, opxy::i32 value, char* text) __asm__("sdk_synth_value");
// The slot shown at page position `index` while engine is the selected track's
// owned synth, as last refreshed; other indices and engines map to themselves.
// For stock control titles.
opxy::u32 sdk_synth_title_slot(opxy::u32 engine, opxy::u32 index) __asm__("sdk_synth_title_slot");
// Once per GUI tick (synth_pages.cpp, the control service): when the selected
// instrument track's owned synth hides the stock page shown, shows the page shown
// before it instead.
void sdk_synth_pages() __asm__("sdk_synth_pages");
// The hidden page key (2..4 for M2..M4) last pressed on `track` while it was
// selected, once, or 0.
opxy::u32 sdk_synth_page_key(opxy::u32 track) __asm__("sdk_synth_page_key");
void sdk_synth_queue_page_key(opxy::u32 track, opxy::u32 key) __asm__("sdk_synth_queue_page_key");
// The send page's value text for position `index` of an owned effect, through
// the hook in screen_hooks.s; every other caption reaches stock unchanged.
opxy::u32 sdk_fx_value_text(opxy::u32 widget, const char* text, opxy::u32 index, opxy::i32 value)
    __asm__("sdk_fx_value_text");
// Stock's step count for a parameter's encoder edits, through the hook at
// 8d5ad306; an owned machine's enum slots answer their choice count.
opxy::u32 sdk_param_steps(opxy::u32 track, opxy::u32 index, opxy::u32* scale) __asm__("sdk_param_steps");
// Once a frame on the GUI (from the compositor): an owned effect on a send
// gets its controls 5..8 stored and sent to the audio side as stock's current
// values hold them. Stock stores and sends only the first four of an effect's
// defaults when it is put on a send; every later edit it stores and sends itself.
void sdk_send_refresh() __asm__("sdk_send_refresh");
// The machine holding an aux track, as its engine, or 0: set by track control's
// aux tracks (aux_tracks.cpp) in builds that link them, null in others.
extern opxy::u32 (*sdk_aux_engine)(opxy::u32) __asm__("sdk_aux_engine");
extern bool (*sdk_aux_owns_page)(opxy::u32) __asm__("sdk_aux_owns_page");
// The send page's title for position `index` of the effect on the selected send
// track: the name of the slot it shows, which a press may have moved to an alternate.
const char* sdk_fx_title(opxy::u32 effect, opxy::u32 index) __asm__("sdk_fx_title");
// The view of the owned effect on send track 14 or 15; false for any other track or effect.
bool sdk_send_view(opxy::u32 track, opxy::platform::SendView* view) __asm__("sdk_send_view");
}
