#pragma once
#include "types.hpp"

namespace opxy { namespace draw {
// Retained drawing: the one display contract. The machine declares its frame's
// budget in definition.json (resources.display) and names the same shape here:
//   using Frame = opxy::draw::Frame<2, 64, 28, 1, 16>;  // paths, points, nodes, texts, palette
// The SDK renderers bind each slot to the artwork element of the same index
// (<prefix>_node_<i>, <prefix>_text_<i>, <prefix>_palette_<i>) and apply the
// frame to it. Coordinates, scales and unit vectors are Q10; palette selectors
// index the artwork's palette entries. Characters, dials, keyboards and other
// patterns are the machine's own code filling nodes, not separate contracts.
const i32 unit = q10_unit;
// Bounds in pixels. Path points are relative to their node; node and text
// positions cover the screen plus a margin, so an element can be parked out of view.
// hide() parks at the bottom-right corner, 160 pixels right of and 100 below the
// 480 x 220 screen: only an element reaching further both left and up still shows.
const i32 path_extent = 64;
const i32 node_left = -64, node_right = 640, node_top = -64, node_bottom = 320;
const i32 hidden_x = node_right, hidden_y = node_bottom;
// Terminated text: a text slot holds at most 23 characters, a header value 7.
const u32 text_capacity = 24, header_capacity = 8;
enum Paint : u32 { keep = 0, stroke = 1, fill_and_stroke = 2,
                   rgba_stroke = 3, rgba_fill_and_stroke = 4, rgba_fill = 5, hidden = 6 };
// Call after transform/paint setters. A later ordinary frame restores authored visibility.
template <class F> inline void conceal(F& frame, u32 index) {
    if (index < F::node_count) { frame.nodes[index].paint = hidden; frame.nodes[index].shade = 0; }
}
struct Point { i32 x, y; };
// A clockwise rotation as a Q10 unit vector; {unit, 0} is no rotation.
struct Turn { i32 cos, sin; };
template <u32 Points> struct Path { u32 key; Point points[Points]; };
enum PathVerb : u32 { move_to = 1, line_to = 2, quadratic_to = 3 };
struct QuadraticCommand { PathVerb kind; Point control, end; };
template <u32 Commands> struct QuadraticPath { u32 key, count; QuadraticCommand commands[Commands]; };
template <u32 Points> struct CountedPath { u32 key, count; Point points[Points]; };
template <u32 Size, bool Quadratic, bool Counted> struct PathStorage { using type = Path<Size>; };
template <u32 Size> struct PathStorage<Size, true, false> { using type = QuadraticPath<Size>; };
template <u32 Size> struct PathStorage<Size, false, true> { using type = CountedPath<Size>; };
// Every frame sets each node's whole transform: its origin at (x, y), with a
// uniform scale, turned by {cos, sin}. An element authored at its place on
// screen, with no transform, stays there at (0, 0), unit scale and no turn.
// Paint selects palette entry `shade` for its stroke, or its fill and stroke;
// keep leaves the paint as it is.
struct Node { i32 x, y, scale, cos, sin; u32 shade; Paint paint; };
// Opt-in affine nodes preserve nonuniform scale, shear and reflection. Matrix
// words are Q10 and match SVG matrix(a,b,c,d,x,y); immutable paint uses keep.
struct AffineNode { i32 a, b, c, d, x, y; u32 shade; Paint paint; };
template <bool Affine> struct NodeKind { using type = Node; };
template <> struct NodeKind<true> { using type = AffineNode; };
// A text slot: its characters, placed like a node without turning.
struct Text { i32 x, y, scale; char chars[text_capacity]; };
// Paths are nodes 0..Paths-1: mutable outlines of exactly Points points.
// Without paths or text slots, their array keeps one unused element, so every
// frame is standard C++ with the layout the renderers expect.
// header replaces the stock value shown for each control position; an empty
// string keeps the stock value.
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette, bool Affine = false,
          bool Cubic = false, u32 OpenPaths = 0, u32 SegmentPaths = 0, bool Quadratic = false, bool Counted = false>
struct Frame {
    static const u32 glyph_count = 0, glyph_definition_count = 0, glyph_start = 0;
    static const bool rgba = false;
    static const bool affine = Affine;
    static const bool cubic = Cubic;
    static const bool quadratic = Quadratic;
    static const bool counted = Counted;
    static const u32 open_paths = OpenPaths;
    static const u32 segment_paths = SegmentPaths;
    static_assert(Paths <= 32 && Points <= 256 &&
                  (Counted ? Paths * (8 + 8 * Points) <= 2816 : Paths * Points * (Quadratic ? 2 : 1) <= 256),
                  "Retained path storage budget");
    static_assert(!Counted || (Affine && Paths > 0 && Points >= 2 && !Cubic && !Quadratic),
                  "Counted paths require affine polylines");
    static_assert(!Quadratic || (Affine && Paths > 0 && Points >= 2 && !Cubic && !OpenPaths && !SegmentPaths), "Quadratic command paths require affine nodes and no topology masks");
    static_assert(!Cubic || (Paths > 0 && Points == 4 && !OpenPaths && !SegmentPaths), "Cubic paths contain start, two controls and endpoint");
    static_assert(Paths == 32 || (OpenPaths >> (Paths % 32)) == 0, "Open path mask exceeds path count");
    static_assert(Paths == 32 || (SegmentPaths >> (Paths % 32)) == 0, "Segment path mask exceeds path count");
    static_assert(!(OpenPaths & SegmentPaths), "Path topology masks overlap");
    using node_type = typename NodeKind<Affine>::type;
    static const u32 path_count = Paths;
    static const u32 point_count = Quadratic ? 0 : Points;
    static const u32 command_capacity = Quadratic ? Points : 0;
    static const u32 node_count = Nodes;
    static const u32 text_count = Texts;
    static const u32 palette_count = Palette;
    using path_type = typename PathStorage<Points, Quadratic, Counted>::type;
    path_type paths[Paths > 0 ? Paths : 1];
    node_type nodes[Nodes];
    Text texts[Texts > 0 ? Texts : 1];
    char header[page_controls][header_capacity];
};
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette, u32 OpenPaths = 0, u32 SegmentPaths = 0>
using AffineFrame = Frame<Paths, Points, Nodes, Texts, Palette, true, false, OpenPaths, SegmentPaths>;
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette>
using CubicFrame = Frame<Paths, Points, Nodes, Texts, Palette, false, true>;
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette>
using AffineCubicFrame = Frame<Paths, Points, Nodes, Texts, Palette, true, true>;
template <u32 Paths, u32 Commands, u32 Nodes, u32 Texts, u32 Palette>
using AffineQuadraticFrame = Frame<Paths, Commands, Nodes, Texts, Palette, true, false, 0, 0, true>;
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette, u32 OpenPaths = 0, u32 SegmentPaths = 0>
using AffineCountedFrame = Frame<Paths, Points, Nodes, Texts, Palette, true, false, OpenPaths, SegmentPaths, false, true>;
// Opt-in color capability wraps any retained frame without changing its layout.
template <class Base> struct RgbaFrame : Base {
    static_assert(Base::affine && Base::palette_count == 0, "RGBA requires affine nodes and palette zero");
    static const bool rgba = true;
};
// Bounded immutable outlines selected into two owned path leaves per instance.
// The Frame prefix is repeated, preserving standard-layout data and every offset.
template <class Base, u32 Instances, u32 Definitions, u32 Start> struct GlyphFrame {
    static const bool rgba = Base::rgba, affine = Base::affine, cubic = Base::cubic, quadratic = Base::quadratic, counted = Base::counted;
    static const u32 open_paths = Base::open_paths, segment_paths = Base::segment_paths;
    static const u32 path_count = Base::path_count, point_count = Base::point_count,
        command_capacity = Base::command_capacity, node_count = Base::node_count,
        text_count = Base::text_count, palette_count = Base::palette_count;
    static const u32 glyph_count = Instances, glyph_definition_count = Definitions, glyph_start = Start;
    static_assert(Base::glyph_count == 0 && affine && Instances > 0 && Instances <= 64 &&
        Definitions > 0 && Definitions <= 16 && Start >= path_count && Start <= node_count &&
        2 * Instances <= node_count - Start, "Glyph atlas exceeds owned node budget");
    using path_type = typename Base::path_type;
    using node_type = typename Base::node_type;
    path_type paths[path_count > 0 ? path_count : 1];
    node_type nodes[node_count];
    Text texts[text_count > 0 ? text_count : 1];
    char header[page_controls][header_capacity];
    u32 glyphs[Instances];
};
template <class F> inline void glyph(F& frame, u32 instance, u32 definition) {
    if (instance < F::glyph_count) frame.glyphs[instance] = definition;
}
template <class F, u32 Count = F::glyph_count> struct GlyphValidation {
    static bool valid(const F& frame) {
        for (u32 i = 0; i < Count; ++i) if (frame.glyphs[i] >= F::glyph_definition_count) return false;
        return true;
    }
};
template <class F> struct GlyphValidation<F, 0> { static bool valid(const F&) { return true; } };
inline u32 color(u8 red, u8 green, u8 blue, u8 alpha = 255) {
    return u32(red) | u32(green) << 8 | u32(blue) << 16 | u32(alpha) << 24;
}
template <class F> inline void rgba(F& frame, u32 index, u32 color, Paint paint = rgba_stroke) {
    if (index < F::node_count) { frame.nodes[index].shade = color; frame.nodes[index].paint = paint; }
}
struct Input {
    i32 parameters[page_controls];
    i32 elapsed;             // Q10 seconds; the author clamps late/invalid frames.
    u32 track;               // Track identity, not a device address.
    union { u32 note_serial; u32 note_on_serial; }; // Same note-start counter; wraps modulo 2^32.
    u32 reduced_motion;
    u8 shown[page_controls];  // Declared parameter shown at each position: its own or an alternate.
    i32 slots[parameter_slots];  // Every declared parameter's value, 0 beyond the declared ones.
    u32 held_notes;          // Distinct gated MIDI pitches, 0..128; notes_unavailable if unknown.
    u32 note_off_serial;     // Gate closures, excluding duplicate releases and audio release tails.
    // An effect's page (SDK 1.7): 1 while Shift is held on an effect with a
    // shift row, and each shift control's choice, 0 beyond the declared ones.
    // A synth's page reads 0 in both.
    u32 shift;
    u8 choices[4];
};
const u32 notes_unavailable = 0xffffffffu;
// Opt-in audio telemetry input. A zero count means no audio snapshot is
// available; it is distinct from a valid silent/zero-valued snapshot.
struct AudioInput : Input { u32 audio_words[16]; u32 audio_count; };
static_assert(sizeof(AudioInput) == 152, "Audio drawing input layout");
static_assert(sizeof(Input) == 84 && __builtin_offsetof(Input, elapsed) == 16 && __builtin_offsetof(Input, track) == 20 &&
              __builtin_offsetof(Input, note_serial) == 24 && __builtin_offsetof(Input, reduced_motion) == 28 &&
              __builtin_offsetof(Input, shown) == 32 && __builtin_offsetof(Input, slots) == 36 &&
              __builtin_offsetof(Input, held_notes) == 68 && __builtin_offsetof(Input, note_off_serial) == 72 &&
              __builtin_offsetof(Input, shift) == 76 && __builtin_offsetof(Input, choices) == 80,
              "Drawing input layout (display.mjs)");
static_assert(sizeof(Node) == 28 && sizeof(Text) == 36, "Drawing frame layout (display.mjs)");

// Store a zero-initialized NoteTracker in DisplayState. Events describe changes
// between frames, not a queue. Reset/track changes establish a baseline, even
// when notes are already held. An unavailable snapshot also clears the baseline.
struct NoteEvents { bool hold_started, hold_ended, note_released; };
struct NoteTracker {
    u32 initialized, track, held_notes, note_off_serial;
    void reset() { initialized = 0; }
    NoteEvents update(const Input& input) {
        NoteEvents events = {};
        if (input.held_notes > 128) { initialized = 0; return events; }
        if (initialized && track == input.track) {
            events.hold_started = !held_notes && input.held_notes;
            events.hold_ended = held_notes && !input.held_notes;
            events.note_released = note_off_serial != input.note_off_serial;
        }
        initialized = 1;
        track = input.track;
        held_notes = input.held_notes;
        note_off_serial = input.note_off_serial;
        return events;
    }
};

// Defined floor division by 1024 on both compilers, including negative values.
// Callers keep products inside int32. No floating point or runtime division.
inline i32 floor_q10(i32 value) {
    return value / unit - (value < 0 && value % unit != 0);
}
inline i32 clamp(i32 value, i32 low, i32 high) {
    return value < low ? low : value > high ? high : value;
}
// Places a node unturned; turn() rotates it afterwards.
template <class F>
inline void place(F& frame, u32 node, i32 x, i32 y, i32 scale, u32 shade, Paint paint) {
    if (node >= F::node_count) return;
    Node& destination = frame.nodes[node];
    destination.x = x;
    destination.y = y;
    destination.scale = scale;
    destination.cos = unit;
    destination.sin = 0;
    destination.shade = shade;
    destination.paint = paint;
}
// Positive unit scale avoids singular matrices in the stock renderer.
template <class F>
inline void hide(F& frame, u32 node) {
    place(frame, node, hidden_x * unit, hidden_y * unit, unit, 0, keep);
}
// Paints a node without moving it.
template <class F>
inline void shade(F& frame, u32 node, u32 shade, Paint paint) {
    if (node >= F::node_count) return;
    frame.nodes[node].shade = shade;
    frame.nodes[node].paint = paint;
}
template <class F>
inline void turn(F& frame, u32 node, Turn turn) {
    if (node >= F::node_count) return;
    frame.nodes[node].cos = turn.cos;
    frame.nodes[node].sin = turn.sin;
}
// Copies terminated text, truncating to the capacity and clearing the rest.
inline void copy(char* output, u32 capacity, const char* input) {
    u32 i = 0;
    for (; i + 1 < capacity && input[i]; ++i) output[i] = input[i];
    for (; i < capacity; ++i) output[i] = 0;
}
template <class F>
inline void text(F& frame, u32 slot, i32 x, i32 y, i32 scale, const char* input) {
    if (slot >= F::text_count) return;
    Text& destination = frame.texts[slot];
    destination.x = x;
    destination.y = y;
    destination.scale = scale;
    copy(destination.chars, text_capacity, input);
}
template <class F>
inline void header(F& frame, u32 position, const char* input) {
    if (position < page_controls) copy(frame.header[position], header_capacity, input);
}

// sin of 0..90 degrees, Q10.
static const u16 sine_q10[91] = {
    0, 18, 36, 54, 71, 89, 107, 125, 143, 160, 178, 195, 213, 230, 248, 265, 282, 299, 316, 333, 350, 367, 384,
    400, 416, 433, 449, 465, 481, 496, 512, 527, 543, 558, 573, 587, 602, 616, 630, 644, 658, 672, 685, 698, 711,
    724, 737, 749, 761, 773, 784, 796, 807, 818, 828, 839, 849, 859, 868, 878, 887, 896, 904, 912, 920, 928, 935,
    943, 949, 956, 962, 968, 974, 979, 984, 989, 994, 998, 1002, 1005, 1008, 1011, 1014, 1016, 1018, 1020, 1022,
    1023, 1023, 1024, 1024};
// sin of a tenth-degree angle, Q10, interpolated between whole degrees.
inline i32 sine(i32 tenths) {
    tenths %= 3600;
    if (tenths < 0) tenths += 3600;
    const i32 sign = tenths >= 1800 ? -1 : 1;
    tenths %= 1800;
    if (tenths > 900) tenths = 1800 - tenths;
    const i32 degree = tenths / 10, fraction = tenths % 10;
    const i32 low = sine_q10[degree], high = sine_q10[degree < 90 ? degree + 1 : 90];
    return sign * (low + (high - low) * fraction / 10);
}
// A clockwise rotation by tenths of a degree.
inline Turn rotation(i32 tenths) {
    const Turn turn = {sine(tenths + 900), sine(tenths)};
    return turn;
}
// A control's Q31 value mapped onto a sweep of +/- `half` tenths of a degree
// either side of upright, at most a full turn (1800). 32-bit arithmetic only:
// firmware links no 64-bit helpers.
inline Turn sweep(i32 value, i32 half) {
    if (value < 0) value = 0;
    if (half < 0) half = 0;
    if (half > 1800) half = 1800;
    return rotation(-half + (value >> 16) * 2 * half / 32767);
}

inline bool terminated(const char* text, u32 capacity) {
    for (u32 i = 0; i < capacity; ++i)
        if (!text[i]) return true;
    return false;
}
inline bool placement(i32 x, i32 y, i32 scale) {
    return x >= node_left * unit && x <= node_right * unit && y >= node_top * unit && y <= node_bottom * unit &&
           scale > 0 && scale <= 2 * unit;
}
// A rotation, never a scale or skew: a unit vector within Q10 rounding.
inline bool rotation_valid(i32 cos, i32 sin) {
    if (cos < -unit || cos > unit || sin < -unit || sin > unit) return false;
    const i32 length = cos * cos + sin * sin;
    return length >= unit * unit / 16 * 15 && length <= unit * unit / 16 * 17;
}
inline bool node_valid(const Node& node) {
    return placement(node.x, node.y, node.scale) && rotation_valid(node.cos, node.sin);
}
inline bool node_valid(const AffineNode& node) {
    const i32 limit = 16 * unit, translation = 8192 * unit;
    return node.a >= -limit && node.a <= limit && node.b >= -limit && node.b <= limit &&
           node.c >= -limit && node.c <= limit && node.d >= -limit && node.d <= limit &&
           node.x >= -translation && node.x <= translation && node.y >= -translation && node.y <= translation;
}
struct Matrix { i32 a, b, c, d, x, y; };
inline Matrix matrix(const Node& node) {
    const i32 a = floor_q10(node.scale * node.cos), b = floor_q10(node.scale * node.sin);
    return Matrix{a, b, -b, a, node.x, node.y};
}
inline Matrix matrix(const AffineNode& node) { return Matrix{node.a, node.b, node.c, node.d, node.x, node.y}; }
template <class F>
inline void affine(F& frame, u32 index, Matrix value, u32 shade = 0, Paint paint = keep) {
    if (index < F::node_count)
        frame.nodes[index] = AffineNode{value.a, value.b, value.c, value.d, value.x, value.y, shade, paint};
}
inline bool path_point_valid(Point point, i32 extent) {
    return point.x >= -extent * unit && point.x <= extent * unit &&
           point.y >= -extent * unit && point.y <= extent * unit;
}
template <u32 Points> inline bool path_valid(const Path<Points>& path, bool extended) {
    if (!path.key) return false;
    for (u32 i = 0; i < Points; ++i)
        if (!path_point_valid(path.points[i], extended ? 8192 : path_extent)) return false;
    return true;
}
template <u32 Commands> inline bool path_valid(const QuadraticPath<Commands>& path, bool) {
    if (!path.key || path.count < 2 || path.count > Commands) return false;
    for (u32 i = 0; i < path.count; ++i) {
        const QuadraticCommand& command = path.commands[i];
        if ((i == 0 ? command.kind != move_to : command.kind != line_to && command.kind != quadratic_to) ||
            !path_point_valid(command.end, 8192)) return false;
        if (command.kind == quadratic_to && !path_point_valid(command.control, 8192)) return false;
    }
    return true;
}
template <u32 Points> inline bool path_valid(const CountedPath<Points>& path, bool) {
    if (!path.key || path.count < 2 || path.count > Points) return false;
    for (u32 i = 0; i < path.count; ++i)
        if (!path_point_valid(path.points[i], 8192)) return false;
    return true;
}
template <u32 Points> inline u32 active_pairs(const Path<Points>&) { return Points; }
template <u32 Points> inline u32 active_pairs(const CountedPath<Points>& path) { return path.count; }
template <u32 Commands> inline u32 active_pairs(const QuadraticPath<Commands>& path) { return 2 * path.count; }
// Both backends check the complete frame before applying it.
template <class F>
inline bool valid(const F& frame) {
    if (!GlyphValidation<F>::valid(frame)) return false;
    u32 active = 0;
    for (u32 p = 0; p < F::path_count; ++p) {
        if (!path_valid(frame.paths[p], F::affine || F::cubic)) return false;
        if (F::counted && (active += active_pairs(frame.paths[p])) > 256) return false;
    }
    for (u32 i = 0; i < F::node_count; ++i) {
        const auto& node = frame.nodes[i];
        if (!node_valid(node)) return false;
        if (node.paint == hidden) {
            if (node.shade != 0) return false;
            continue;
        }
        if (F::rgba) {
            if (F::palette_count || (node.paint != keep &&
                (node.paint < rgba_stroke || node.paint > rgba_fill)) ||
                (node.paint == keep && node.shade != 0)) return false;
        } else if (node.paint > fill_and_stroke ||
            (F::palette_count ? node.shade >= F::palette_count : node.paint != keep || node.shade != 0)) return false;
    }
    for (u32 i = 0; i < F::text_count; ++i) {
        const Text& text = frame.texts[i];
        if (!placement(text.x, text.y, text.scale) || !terminated(text.chars, text_capacity)) return false;
    }
    for (u32 i = 0; i < page_controls; ++i)
        if (!terminated(frame.header[i], header_capacity)) return false;
    return true;
}
}} // namespace opxy::draw
