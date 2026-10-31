// Host doubles for stock SVG objects; the real drawing renderer is included.
#pragma once
#include <opxy/draw.hpp>
#include <path_geometry.hpp>
namespace opxy { namespace binding {
using DisplayInput = draw::Input;
const bool full_canvas = false;
inline void audio_screen_reset() {}
inline void snapshot_audio(DisplayInput&) {}
inline void snapshot_encoders(DisplayInput&) {}
inline void finish_audio(DisplayInput&) {}
struct DisplayState { u32 ticks; };
using Frame = draw::Frame<1, 2, 1, 0, 1>;
static unsigned resets, renders;
inline void display_reset(DisplayState* state, Frame* frame) { ++resets; state->ticks = 0; *frame = {}; }
inline void display_render(DisplayState* state, const draw::Input*, Frame*) { ++renders; ++state->ticks; }
// Keep this boundary test away from stock object addresses and SVG setters.
inline bool display_valid(const Frame*) { return false; }
}}
namespace opxy { namespace screen { namespace {
struct Vector { u32 begin, end, capacity; };
struct Leaf {};
struct PathNode { Leaf leaf; Vector commands; u16 fill, stroke; };
struct Document { Vector fills, strokes; };
struct Base { Document* svg; u32 page; Leaf* values[4]; };
struct Controls { bool valid; u32 track; u8 shown[4]; i32 values[4], slots[8]; u32 shift; u8 choices[4]; };
inline PathNode* path_node(void* raw) { return static_cast<PathNode*>(raw); }
template<class T> void clear(T*) {}
inline void bind_controls(Base*) {}
inline void* find(Base*, const char*) { return nullptr; }
inline Leaf* text_node(void*) { return nullptr; }
inline u32 palette_ids(void*, const void*) { return 0; }
inline void invalidate(PathNode*) {}
template<unsigned N> void set_words(Leaf*, const u32 (&)[N], const i32 (&)[N]) {}
inline void paint(void*, u32, bool) {}
inline void set_text(Leaf*, const char*) {}
inline u32 note_serial(u32) { return 0; }
inline bool refresh_controls(Base*, Controls&, bool = false) { return false; }
inline void draw(Base*) {}
}}}
