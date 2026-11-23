// Host doubles around the extended renderer (extended_drawing.hpp), which is
// included as it is with kit_screen.hpp; copied in place of each other header it
// includes, so it guards itself. OPXY_TEST_KIT: the screen is a drum kit's;
// OPXY_TEST_CANVAS: its display declares fullCanvas.
#ifndef EXTENDED_KIT_HOST
#define EXTENDED_KIT_HOST
#include <opxy/encoder_activity.hpp>
#include "synth_controls.hpp"
#include "drum_storage.hpp"
#include <cstring>
namespace opxy {
namespace binding {
struct DisplayState { u32 ticks; };
typedef draw::EncoderInput DisplayInput;
typedef draw::Frame<1, 2, 1, 0, 1> Frame;
const bool full_canvas = OPXY_TEST_CANVAS;
static unsigned resets, renders, encoder_reads;
static DisplayInput last_input;
inline void display_reset(DisplayState* state, Frame*) { ++resets; state->ticks = 0; }
inline void display_render(DisplayState* state, const DisplayInput* input, Frame*) {
    ++renders; ++state->ticks; last_input = *input;
}
inline bool display_valid(const Frame*) { return true; }
inline void snapshot_encoders(DisplayInput& input) { ++encoder_reads; input.encoders.available = 1; }
}
namespace platform {
inline u32 drum_send_shown(u32 index, bool) { return index; }
inline void amp_envelope(u32, draw::Envelope& amp) { amp.attack = amp.decay = amp.sustain = amp.release = 7; }
}
namespace screen { namespace {
struct Leaf { char text[32]; u32 writes; };
// A path leaf as the renderer reaches it: its visibility byte at 92.
struct PathNode { unsigned char bytes[128]; };
struct Base { const void* svg; u32 page; Leaf* values[4]; Leaf* titles[4]; };
struct Controls {
    u32 track; bool valid; u8 shown[page_controls]; i32 values[page_controls]; i32 slots[parameter_slots];
    u32 model, pad; bool choosing; u32 shift; u8 choices[4]; i32 pan, level; u32 line;
    i32 pads[platform::kDrumPadFields];
};
static Controls fixture_controls;
static u32 fixture_track, fixture_serial, fixture_binds, fixture_refreshes, fixture_canvas_reads, fixture_draws,
    fixture_strips, fixture_headers, fixture_order;
static PathNode fixture_bar[8];
static u32 fixture_strip_after_draw;
inline u32 selected_track() { return fixture_track; }
inline u32 note_serial(u32) { return fixture_serial; }
template <class T> void clear(T* o) { o->delta = 0; }
inline void bind_controls(Base*) { ++fixture_binds; }
// The bar's eight blocks are the only nodes the renderer looks up itself.
inline void* find(Base*, const char* id) {
    return !std::strncmp(id, "bar_", 4) && id[4] >= '0' && id[4] < '8' && !id[5] ? &fixture_bar[id[4] - '0'] : nullptr;
}
inline PathNode* path_node(void* node) { return static_cast<PathNode*>(node); }
inline Leaf* text_node(void* node) { return static_cast<Leaf*>(node); }
inline void set_text(Leaf* node, const char* text) { std::strncpy(node->text, text, 31); ++node->writes; }
// A kit's page as refresh_controls leaves it: the stock header's texts written, the
// Shift cells while Shift is held (choosing).
inline bool refresh_controls(Base* o, Controls& controls) {
    ++fixture_refreshes;
    controls = fixture_controls;
    for (u32 i = 0; i < 4; ++i) {
        set_text(o->titles[i], controls.choosing ? "SHIFT" : "title");
        set_text(o->values[i], "50");
    }
    return true;
}
inline bool sdk_send_view(u32, platform::SendView*) { return false; }
inline void sdk_synth_shown(u32, u8* shown) { for (u32 i = 0; i < 4; ++i) shown[i] = static_cast<u8>(i); }
typedef u32 Parameters;
inline const Parameters* parameters(u32) { return nullptr; }
inline bool read_shown(const Parameters*, Controls& controls, u32 (&)[parameter_slots]) {
    ++fixture_canvas_reads; controls.valid = true; return true;
}
inline void draw(Base*) { ++fixture_draws; fixture_order = 1; }
inline void draw_hold_strip(u32, i32) { ++fixture_strips; fixture_strip_after_draw = fixture_order; }
template <class Frame> inline void apply_header(Base*, const Frame&) { ++fixture_headers; }
template <class Frame> struct ExtendedRetained {
    struct Object { Base base; i32 delta; u32 keys[1]; };
    template <class Machine> static void bind(Object*) {}
    static void apply(Object*, const Frame&) {}
};
const u32 visibility_byte = 92;
}}
}
extern "C" inline void sdk_display_note_activity(opxy::u32, opxy::draw::Input* input) { input->held_notes = 3; }
#endif
