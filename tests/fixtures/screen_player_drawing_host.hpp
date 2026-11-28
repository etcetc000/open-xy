// Host doubles for the stock objects around a player's drawn page; the real
// renderer (screen_player_drawing.hpp) is included. Copied in place of each
// header it includes, so it guards itself.
#ifndef SCREEN_PLAYER_DRAWING_HOST
#define SCREEN_PLAYER_DRAWING_HOST
#include <opxy/draw.hpp>
#include <opxy/player.hpp>
#include <cstring>
namespace opxy {
namespace platform {
u32 player_row(u32 track);
void* player_object(u32 track, const void* const* vtable);
inline void amp_envelope(u32, draw::Envelope& amp) { amp.attack = amp.decay = amp.sustain = amp.release = 7; }
}
namespace screen { namespace {
struct Leaf { char text[32]; u32 writes; };
struct Base { const void* svg; u32 page; Leaf* values[4]; Leaf* titles[4]; };
inline Leaf* text_node(void* p) { return static_cast<Leaf*>(p); }
inline void set_text(Leaf* node, const char* text) { std::strncpy(node->text, text, 31); ++node->writes; }
static u32 fixture_track, fixture_serial, fixture_draws;
inline u32 selected_track() { return fixture_track; }
inline u32 note_serial(u32) { return fixture_serial; }
inline void bind_controls(Base*) {}
inline void draw(Base*) { ++fixture_draws; }
template <class T> void clear(T* o) { o->delta = 0; }
static u32 fixture_binds, fixture_applies;
template <class Frame> struct RetainedDrawing {
    struct Object { Base base; i32 delta; u32 keys[Frame::path_count > 0 ? Frame::path_count : 1]; };
    static void bind(Object*, const char* const*) { ++fixture_binds; }
    static void apply(Object*, const Frame&) { ++fixture_applies; }
};
}}
}
extern "C" inline void sdk_display_note_activity(opxy::u32, opxy::draw::Input* input) {
    input->held_notes = 3;
    input->note_off_serial = 9;
}
#endif
