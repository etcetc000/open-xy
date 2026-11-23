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
// The rgba_* paints and hidden belong to extended frames only (see "Extended
// drawing" below); a Frame refuses them.
enum Paint : u32 { keep = 0, stroke = 1, fill_and_stroke = 2,
                   rgba_stroke = 3, rgba_fill_and_stroke = 4, rgba_fill = 5, hidden = 6 };
struct Point { i32 x, y; };
// A clockwise rotation as a Q10 unit vector; {unit, 0} is no rotation.
struct Turn { i32 cos, sin; };
template <u32 Points> struct Path { u32 key; Point points[Points]; };
// Every frame sets each node's whole transform: its origin at (x, y), with a
// uniform scale, turned by {cos, sin}. An element authored at its place on
// screen, with no transform, stays there at (0, 0), unit scale and no turn.
// Paint selects palette entry `shade` for its stroke, or its fill and stroke;
// keep leaves the paint as it is.
struct Node { i32 x, y, scale, cos, sin; u32 shade; Paint paint; };
// A text slot: its characters, placed like a node without turning.
struct Text { i32 x, y, scale; char chars[text_capacity]; };
// Paths are nodes 0..Paths-1: mutable outlines of exactly Points points.
// Without paths or text slots, their array keeps one unused element, so every
// frame is standard C++ with the layout the renderers expect.
// header replaces the stock value shown for each control position; an empty
// string keeps the stock value.
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette>
struct Frame {
    static const u32 path_count = Paths;
    static const u32 point_count = Points;
    static const u32 node_count = Nodes;
    static const u32 text_count = Texts;
    static const u32 palette_count = Palette;
    using path_type = draw::Path<Points>;
    path_type paths[Paths > 0 ? Paths : 1];
    Node nodes[Nodes];
    Text texts[Texts > 0 ? Texts : 1];
    char header[page_controls][header_capacity];
};
// The track's stock amplitude envelope as Q31 words, 0..0x7fffffff, on the
// scale of slots: a larger attack or decay is a longer stage, but a larger
// release is a shorter one (stock reads it mirrored). Sustain is a linear
// gain. These are control positions, not times; stock uses only word >> 24.
struct Envelope { i32 attack, decay, sustain, release; };
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
    // While Shift is held: 1, and each Shift control's choice, 0 beyond the
    // declared ones (an effect's shift row, SDK 1.7; a drum kit's, SDK 1.8).
    // A drum kit's choices are its selected key's model group, its model in
    // the group and its choke group, or without groups its model and choke
    // group; they hold at all times, shift only while Shift is held. A padMix
    // kit gives the same, though its first Shift row differs: read model, pan and
    // level (its second, pad). A padVolume kit's third choice is its key's volume
    // as dB + 30 (0..50, 30 unity), in the choke group's place with groups; a
    // padRandom kit's fourth its key's random in percent (0..100), SDK 1.23.
    u32 shift;
    u8 choices[4];
    u32 model;               // A drum kit's selected key's model (its machine); 0 for other kinds.
    Envelope amp;            // The track's amp envelope (SDK 1.10); every word envelope_unavailable if unknown.
    // A padMix drum kit's selected key (SDK 1.10): its pan, -100..100 with 0
    // centred, and its level in whole dB, -30..0, as it plays; 0 otherwise.
    // line: the row the encoders (and on a padMix kit Shift) edit, 0 the first
    // and 1 the second. The key's choke group stays in choices.
    // selected_key: a drum kit declaring SDK 1.23 or later, its selected key,
    // 0..23 for pad 53 + n; 0 otherwise (before SDK 1.23 a reserved 0).
    i8 pan;
    i8 level;
    u8 line;
    u8 selected_key;
    // A padMix drum kit's selected key's pad fields (SDK 1.10): its pad
    // controls (resources.padControls) in 0 and 1, a signed one negative below
    // 0, then FX I and FX II, 0 when it follows the track's send: for a kit
    // declaring SDK 1.15 or later its percent + 1 (1..101), for an older one
    // its step 1..7 (0, 17, 33, 50, 67, 83, 100; an amount as its nearest); 0
    // when neutral, not declared, and for other machines.
    i8 pad[4];
    // A drum kit declaring SDK 1.21 or later (drawing ABI 7); zero otherwise.
    // Latest played drum pad; zero when unavailable. Read when note_serial changes.
    // Controls reflect the GUI snapshot and current offsets, not past step locks.
    u32 trigger_key, trigger_model;
    i32 trigger_controls[parameter_slots];
};
const u32 notes_unavailable = 0xffffffffu;
const i32 envelope_unavailable = -1;
static inline bool envelope_known(const Envelope& envelope) {
    return envelope.attack >= 0 && envelope.decay >= 0 && envelope.sustain >= 0 && envelope.release >= 0;
}
static_assert(sizeof(Input) == 152 && __builtin_offsetof(Input, pan) == 104 && __builtin_offsetof(Input, level) == 105 &&
              __builtin_offsetof(Input, line) == 106 && __builtin_offsetof(Input, selected_key) == 107 &&
              __builtin_offsetof(Input, pad) == 108 &&
              __builtin_offsetof(Input, elapsed) == 16 && __builtin_offsetof(Input, track) == 20 &&
              __builtin_offsetof(Input, note_serial) == 24 && __builtin_offsetof(Input, reduced_motion) == 28 &&
              __builtin_offsetof(Input, shown) == 32 && __builtin_offsetof(Input, slots) == 36 &&
              __builtin_offsetof(Input, held_notes) == 68 && __builtin_offsetof(Input, note_off_serial) == 72 &&
              __builtin_offsetof(Input, shift) == 76 && __builtin_offsetof(Input, choices) == 80 &&
              __builtin_offsetof(Input, model) == 84 && __builtin_offsetof(Input, amp) == 88 &&
              __builtin_offsetof(Input, trigger_key) == 112 && __builtin_offsetof(Input, trigger_model) == 116 &&
              __builtin_offsetof(Input, trigger_controls) == 120,
              "Drawing input layout (display.mjs)");
static_assert(sizeof(Node) == 28 && sizeof(Text) == 36, "Drawing frame layout (display.mjs)");

// Optional shared GUI lifecycle callback; ADL finds the display owner's overload.
// Existing displays need not implement it. Never called by an audio callback.
namespace detail {
template<class S> static inline auto parameter_reset(S& state,u32 slot,int)
    -> decltype(display_parameter_reset(state,slot),void()) { display_parameter_reset(state,slot); }
template<class S> static inline void parameter_reset(S&,u32,...) {}
}
template<class S> static inline void parameter_reset(S& state,u32 slot) { detail::parameter_reset(state,slot,0); }

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

// The helpers below are static: each file that uses one keeps its own copy, so two
// machines in one firmware never define the same symbol (docs/toolchain.md).
// Defined floor division by 1024 on both compilers, including negative values.
// Callers keep products inside int32. No floating point or runtime division.
static inline i32 floor_q10(i32 value) {
    return value / unit - (value < 0 && value % unit != 0);
}
static inline i32 clamp(i32 value, i32 low, i32 high) {
    return value < low ? low : value > high ? high : value;
}
// Places a node unturned; turn() rotates it afterwards.
template <class F>
static inline void place(F& frame, u32 node, i32 x, i32 y, i32 scale, u32 shade, Paint paint) {
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
static inline void hide(F& frame, u32 node) {
    place(frame, node, hidden_x * unit, hidden_y * unit, unit, 0, keep);
}
// Paints a node without moving it.
template <class F>
static inline void shade(F& frame, u32 node, u32 shade, Paint paint) {
    if (node >= F::node_count) return;
    frame.nodes[node].shade = shade;
    frame.nodes[node].paint = paint;
}
template <class F>
static inline void turn(F& frame, u32 node, Turn turn) {
    if (node >= F::node_count) return;
    frame.nodes[node].cos = turn.cos;
    frame.nodes[node].sin = turn.sin;
}
// Copies terminated text, truncating to the capacity and clearing the rest.
static inline void copy(char* output, u32 capacity, const char* input) {
    u32 i = 0;
    for (; i + 1 < capacity && input[i]; ++i) output[i] = input[i];
    for (; i < capacity; ++i) output[i] = 0;
}
template <class F>
static inline void text(F& frame, u32 slot, i32 x, i32 y, i32 scale, const char* input) {
    if (slot >= F::text_count) return;
    Text& destination = frame.texts[slot];
    destination.x = x;
    destination.y = y;
    destination.scale = scale;
    copy(destination.chars, text_capacity, input);
}
template <class F>
static inline void header(F& frame, u32 position, const char* input) {
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
static inline i32 sine(i32 tenths) {
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
static inline Turn rotation(i32 tenths) {
    const Turn turn = {sine(tenths + 900), sine(tenths)};
    return turn;
}
// A control's Q31 value mapped onto a sweep of +/- `half` tenths of a degree
// either side of upright, at most a full turn (1800). 32-bit arithmetic only:
// firmware links no 64-bit helpers.
static inline Turn sweep(i32 value, i32 half) {
    if (value < 0) value = 0;
    if (half < 0) half = 0;
    if (half > 1800) half = 1800;
    return rotation(-half + (value >> 16) * 2 * half / 32767);
}

static inline bool terminated(const char* text, u32 capacity) {
    for (u32 i = 0; i < capacity; ++i)
        if (!text[i]) return true;
    return false;
}
static inline bool placement(i32 x, i32 y, i32 scale) {
    return x >= node_left * unit && x <= node_right * unit && y >= node_top * unit && y <= node_bottom * unit &&
           scale > 0 && scale <= 2 * unit;
}
// A rotation, never a scale or skew: a unit vector within Q10 rounding.
static inline bool rotation_valid(i32 cos, i32 sin) {
    if (cos < -unit || cos > unit || sin < -unit || sin > unit) return false;
    const i32 length = cos * cos + sin * sin;
    return length >= unit * unit / 16 * 15 && length <= unit * unit / 16 * 17;
}
// Both backends check the complete frame before applying it.
template <class F>
static inline bool valid(const F& frame) {
    for (u32 p = 0; p < F::path_count; ++p) {
        if (!frame.paths[p].key) return false;
        for (u32 i = 0; i < F::point_count; ++i) {
            const Point& point = frame.paths[p].points[i];
            if (point.x < -path_extent * unit || point.x > path_extent * unit ||
                point.y < -path_extent * unit || point.y > path_extent * unit) return false;
        }
    }
    for (u32 i = 0; i < F::node_count; ++i) {
        const Node& node = frame.nodes[i];
        if (!placement(node.x, node.y, node.scale) || !rotation_valid(node.cos, node.sin) ||
            node.shade >= F::palette_count || node.paint > fill_and_stroke) return false;
    }
    for (u32 i = 0; i < F::text_count; ++i) {
        const Text& text = frame.texts[i];
        if (!placement(text.x, text.y, text.scale) || !terminated(text.chars, text_capacity)) return false;
    }
    for (u32 i = 0; i < page_controls; ++i)
        if (!terminated(frame.header[i], header_capacity)) return false;
    return true;
}

// Extended drawing (docs/display.md#extended-drawing): opt-in frame
// shapes for synth and effect screens. A display declaring none of the
// extended keys keeps Frame, its renderer and its generated code unchanged.
//
// An affine node carries a whole 2 x 3 matrix, Q10, as SVG matrix(a,b,c,d,x,y):
// non-uniform scale, shear and reflection. With rgba, shade is a packed colour
// (color()); with a palette, a palette entry as for Node.
struct AffineNode { i32 a, b, c, d, x, y; u32 shade; Paint paint; };
struct Matrix { i32 a, b, c, d, x, y; };
// A quadratic path is a command stream: a move, then lines and quadratic
// curves; the first count of its reserved commands are drawn.
enum PathVerb : u32 { move_to = 1, line_to = 2, quadratic_to = 3 };
struct QuadraticCommand { PathVerb kind; Point control, end; };
template <u32 Commands> struct QuadraticPath { u32 key, count; QuadraticCommand commands[Commands]; };
// A counted path draws the first count of its reserved points.
template <u32 Points> struct CountedPath { u32 key, count; Point points[Points]; };
template <u32 Size, bool Quadratic, bool Counted> struct PathStorage { typedef Path<Size> type; };
template <u32 Size> struct PathStorage<Size, true, false> { typedef QuadraticPath<Size> type; };
template <u32 Size> struct PathStorage<Size, false, true> { typedef CountedPath<Size> type; };
template <bool Affine> struct NodeKind { typedef Node type; };
template <> struct NodeKind<true> { typedef AffineNode type; };
// Extended path budgets: up to 32 paths and 256 points (coordinate pairs) in
// all; counted paths reserve at most 2816 bytes of path storage.
const u32 extended_paths = 32, extended_points = 256, counted_path_bytes = 2816;
// Affine and cubic path coordinates, and affine translations: +/- 8192 pixels.
const i32 extended_extent = 8192;
// Paths, Points (for a quadratic frame, commands per path), Nodes, Texts and
// Palette as for Frame, then the options the display declares. The layout is
// Frame's, with these path and node types.
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette, bool Affine, bool Cubic = false,
          u32 OpenPaths = 0, u32 SegmentPaths = 0, bool Quadratic = false, bool Counted = false>
struct ExtendedFrame {
    static const bool extended = true;
    static const bool affine = Affine, cubic = Cubic, quadratic = Quadratic, counted = Counted, rgba = false;
    static const u32 open_paths = OpenPaths, segment_paths = SegmentPaths;
    static const u32 glyph_count = 0, glyph_definition_count = 0, glyph_start = 0;
    static_assert(Paths <= extended_paths && Points <= extended_points &&
                  (Counted ? Paths * (8 + 8 * Points) <= counted_path_bytes
                           : Paths * Points * (Quadratic ? 2 : 1) <= extended_points),
                  "Extended path budget");
    static_assert(!Counted || (Affine && Paths > 0 && Points >= 2 && !Cubic && !Quadratic),
                  "Counted paths are affine polylines");
    static_assert(!Quadratic || (Affine && Paths > 0 && Points >= 2 && !Cubic && !OpenPaths && !SegmentPaths),
                  "Quadratic paths are affine, without topology masks");
    static_assert(!Cubic || (Paths > 0 && Points == 4 && !OpenPaths && !SegmentPaths),
                  "A cubic path holds its start, two controls and its end");
    static_assert(Paths == 32 || ((OpenPaths >> (Paths % 32)) == 0 && (SegmentPaths >> (Paths % 32)) == 0),
                  "A topology mask names a path beyond the path count");
    static_assert(!(OpenPaths & SegmentPaths), "Path topology masks overlap");
    typedef typename NodeKind<Affine>::type node_type;
    typedef typename PathStorage<Points, Quadratic, Counted>::type path_type;
    static const u32 path_count = Paths;
    static const u32 point_count = Quadratic ? 0 : Points;
    static const u32 command_capacity = Quadratic ? Points : 0;
    static const u32 node_count = Nodes;
    static const u32 text_count = Texts;
    static const u32 palette_count = Palette;
    path_type paths[Paths > 0 ? Paths : 1];
    node_type nodes[Nodes];
    Text texts[Texts > 0 ? Texts : 1];
    char header[page_controls][header_capacity];
};
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette, u32 OpenPaths = 0, u32 SegmentPaths = 0>
using AffineFrame = ExtendedFrame<Paths, Points, Nodes, Texts, Palette, true, false, OpenPaths, SegmentPaths>;
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette>
using CubicFrame = ExtendedFrame<Paths, Points, Nodes, Texts, Palette, false, true>;
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette>
using AffineCubicFrame = ExtendedFrame<Paths, Points, Nodes, Texts, Palette, true, true>;
template <u32 Paths, u32 Commands, u32 Nodes, u32 Texts, u32 Palette>
using AffineQuadraticFrame = ExtendedFrame<Paths, Commands, Nodes, Texts, Palette, true, false, 0, 0, true>;
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette, u32 OpenPaths = 0, u32 SegmentPaths = 0>
using AffineCountedFrame = ExtendedFrame<Paths, Points, Nodes, Texts, Palette, true, false, OpenPaths, SegmentPaths, false, true>;
// Frame's nodes and closed outlines, with more than four paths or 64 points.
template <u32 Paths, u32 Points, u32 Nodes, u32 Texts, u32 Palette, u32 OpenPaths = 0, u32 SegmentPaths = 0>
using LargeFrame = ExtendedFrame<Paths, Points, Nodes, Texts, Palette, false, false, OpenPaths, SegmentPaths>;
// Direct colour: each node's shade is a color() and its paint an rgba_* paint.
// Same layout as its base, which is affine and has no palette.
template <class Base> struct RgbaFrame : Base {
    static_assert(Base::extended && Base::affine && Base::palette_count == 0,
                  "RGBA needs affine nodes and no palette");
    static const bool rgba = true;
};
// A glyph atlas: Instances glyphs, each two owned path nodes from node Start
// on, each showing one of Definitions immutable outlines from the artwork.
// The base's layout, then one definition index per instance.
template <class Base, u32 Instances, u32 Definitions, u32 Start> struct GlyphFrame {
    static const bool extended = true;
    static const bool rgba = Base::rgba, affine = Base::affine, cubic = Base::cubic,
                      quadratic = Base::quadratic, counted = Base::counted;
    static const u32 open_paths = Base::open_paths, segment_paths = Base::segment_paths;
    static const u32 path_count = Base::path_count, point_count = Base::point_count,
                     command_capacity = Base::command_capacity, node_count = Base::node_count,
                     text_count = Base::text_count, palette_count = Base::palette_count;
    static const u32 glyph_count = Instances, glyph_definition_count = Definitions, glyph_start = Start;
    static_assert(Base::extended && Base::glyph_count == 0 && affine && Instances > 0 && Instances <= 64 &&
                  Definitions > 0 && Definitions <= 16 && Start >= path_count && Start <= node_count &&
                  2 * Instances <= node_count - Start, "Glyph atlas exceeds the declared nodes");
    typedef typename Base::path_type path_type;
    typedef typename Base::node_type node_type;
    path_type paths[path_count > 0 ? path_count : 1];
    node_type nodes[node_count];
    Text texts[text_count > 0 ? text_count : 1];
    char header[page_controls][header_capacity];
    u32 glyphs[Instances];
};

// The extended frames' setters live in their own namespace, which opxy::draw
// nominates below: draw::affine and calls inside opxy::draw find them, but
// argument-dependent lookup ignores the nomination, so a machine's own helper of
// the same name, called unqualified on draw types, stays unambiguous.
namespace extended_helpers {
// Hides a node until a later frame paints it again; call after its transform
// and paint setters. Extended frames only.
template <class F> static inline void conceal(F& frame, u32 index) {
    if (index < F::node_count) { frame.nodes[index].paint = hidden; frame.nodes[index].shade = 0; }
}
// Shows outline `definition` at glyph instance `instance`.
template <class F> static inline void glyph(F& frame, u32 instance, u32 definition) {
    if (instance < F::glyph_count) frame.glyphs[instance] = definition;
}
// A packed colour: red, green, blue and alpha bytes.
static inline u32 color(u8 red, u8 green, u8 blue, u8 alpha = 255) {
    return u32(red) | u32(green) << 8 | u32(blue) << 16 | u32(alpha) << 24;
}
template <class F> static inline void rgba(F& frame, u32 index, u32 packed, Paint paint = rgba_stroke) {
    if (index < F::node_count) { frame.nodes[index].shade = packed; frame.nodes[index].paint = paint; }
}
static inline Matrix matrix(const Node& node) {
    const i32 a = floor_q10(node.scale * node.cos), b = floor_q10(node.scale * node.sin);
    const Matrix m = {a, b, -b, a, node.x, node.y};
    return m;
}
static inline Matrix matrix(const AffineNode& node) {
    const Matrix m = {node.a, node.b, node.c, node.d, node.x, node.y};
    return m;
}
template <class F> static inline void affine(F& frame, u32 index, Matrix value, u32 shade = 0, Paint paint = keep) {
    if (index >= F::node_count) return;
    AffineNode& node = frame.nodes[index];
    node.a = value.a; node.b = value.b; node.c = value.c; node.d = value.d;
    node.x = value.x; node.y = value.y; node.shade = shade; node.paint = paint;
}
}
using namespace extended_helpers;

namespace detail {
static inline bool node_valid(const Node& node) {
    return placement(node.x, node.y, node.scale) && rotation_valid(node.cos, node.sin);
}
// Matrix words within +/- 16, translation within the extended extent.
static inline bool node_valid(const AffineNode& node) {
    const i32 limit = 16 * unit, translation = extended_extent * unit;
    return node.a >= -limit && node.a <= limit && node.b >= -limit && node.b <= limit &&
           node.c >= -limit && node.c <= limit && node.d >= -limit && node.d <= limit &&
           node.x >= -translation && node.x <= translation && node.y >= -translation && node.y <= translation;
}
static inline bool point_valid(Point point, i32 extent) {
    return point.x >= -extent * unit && point.x <= extent * unit &&
           point.y >= -extent * unit && point.y <= extent * unit;
}
// Plain paths keep Frame's extent unless the frame is affine or cubic.
template <u32 Points> static inline bool path_valid(const Path<Points>& path, bool wide) {
    if (!path.key) return false;
    for (u32 i = 0; i < Points; ++i)
        if (!point_valid(path.points[i], wide ? extended_extent : path_extent)) return false;
    return true;
}
template <u32 Commands> static inline bool path_valid(const QuadraticPath<Commands>& path, bool) {
    if (!path.key || path.count < 2 || path.count > Commands) return false;
    for (u32 i = 0; i < path.count; ++i) {
        const QuadraticCommand& command = path.commands[i];
        if ((i == 0 ? command.kind != move_to : command.kind != line_to && command.kind != quadratic_to) ||
            !point_valid(command.end, extended_extent)) return false;
        if (command.kind == quadratic_to && !point_valid(command.control, extended_extent)) return false;
    }
    return true;
}
template <u32 Points> static inline bool path_valid(const CountedPath<Points>& path, bool) {
    if (!path.key || path.count < 2 || path.count > Points) return false;
    for (u32 i = 0; i < path.count; ++i)
        if (!point_valid(path.points[i], extended_extent)) return false;
    return true;
}
template <u32 Points> static inline u32 active_points(const CountedPath<Points>& path) { return path.count; }
template <class P> static inline u32 active_points(const P&) { return 0; }
template <class F> static inline bool extended_valid(const F& frame) {
    u32 active = 0;
    for (u32 p = 0; p < F::path_count; ++p) {
        if (!path_valid(frame.paths[p], F::affine || F::cubic)) return false;
        if (F::counted && (active += active_points(frame.paths[p])) > extended_points) return false;
    }
    for (u32 i = 0; i < F::node_count; ++i) {
        const typename F::node_type& node = frame.nodes[i];
        if (!node_valid(node)) return false;
        if (node.paint == hidden) {
            if (node.shade != 0) return false;
        } else if (F::rgba) {
            if (node.paint == keep ? node.shade != 0 : node.paint < rgba_stroke || node.paint > rgba_fill) return false;
        } else if (node.paint > fill_and_stroke ||
                   (F::palette_count ? node.shade >= F::palette_count : node.paint != keep || node.shade != 0)) {
            return false;
        }
    }
    for (u32 i = 0; i < F::text_count; ++i) {
        const Text& text = frame.texts[i];
        if (!placement(text.x, text.y, text.scale) || !terminated(text.chars, text_capacity)) return false;
    }
    for (u32 i = 0; i < page_controls; ++i)
        if (!terminated(frame.header[i], header_capacity)) return false;
    return true;
}
} // namespace detail
// The extended frames' checks, chosen over valid(const F&) as more specialized.
template <u32 P, u32 Q, u32 N, u32 T, u32 C, bool A, bool Cu, u32 O, u32 S, bool Qu, bool Co>
static inline bool valid(const ExtendedFrame<P, Q, N, T, C, A, Cu, O, S, Qu, Co>& frame) {
    return detail::extended_valid(frame);
}
template <class B> static inline bool valid(const RgbaFrame<B>& frame) { return detail::extended_valid(frame); }
template <class B, u32 N, u32 D, u32 S> static inline bool valid(const GlyphFrame<B, N, D, S>& frame) {
    for (u32 i = 0; i < N; ++i)
        if (frame.glyphs[i] >= D) return false;
    return detail::extended_valid(frame);
}
}} // namespace opxy::draw
