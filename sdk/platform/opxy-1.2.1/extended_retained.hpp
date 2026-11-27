// Extended drawing's retained renderer (docs/display.md#extended-drawing): applies an
// extended frame (draw.hpp ExtendedFrame, RgbaFrame, GlyphFrame) to the artwork of a synth's, an
// effect's or a drum kit's screen (extended_drawing.hpp) or a note player's own page
// (screen_player_drawing.hpp). It needs no display binding.
#pragma once
#include "screen.hpp"
#include "path_geometry.hpp"
#include "rgba_palette.hpp"
#include "glyph_atlas.hpp"

namespace opxy { namespace screen { namespace {
// The renderer object: the stock screen, then each node's leaf and the
// visibility byte its artwork gave it, the text leaves and their applied text,
// the palette, the frame's elapsed time, each path's validated command vector
// and last applied key (adapters.screen_bytes).
template <u32 N, u32 T, u32 P, u32 Paths> struct ExtendedObject {
    Base base;
    PathNode* nodes[N];
    u8 authored_visibility[N];
    Leaf* texts[T > 0 ? T : 1];
    char shown[T > 0 ? T : 1][draw::text_capacity];
    u32 palette[P > 0 ? P : 1];
    i32 delta;
    u32 geometry[Paths > 0 ? Paths : 1];
    u32 keys[Paths > 0 ? Paths : 1];
};
// A glyph atlas's renderer object: the same, then the atlas (standard layout, as
// OPXY_SCREEN checks). A frame without glyphs keeps ExtendedObject, its type and
// its code unchanged.
template <u32 N, u32 T, u32 P, u32 Paths, u32 Glyphs, u32 Definitions> struct ExtendedGlyphObject {
    Base base;
    PathNode* nodes[N];
    u8 authored_visibility[N];
    Leaf* texts[T > 0 ? T : 1];
    char shown[T > 0 ? T : 1][draw::text_capacity];
    u32 palette[P > 0 ? P : 1];
    i32 delta;
    u32 geometry[Paths > 0 ? Paths : 1];
    u32 keys[Paths > 0 ? Paths : 1];
    glyph_detail::State<Glyphs, Definitions> glyph_state;
};
template <u32 N, u32 T, u32 P, u32 Paths, u32 Glyphs, u32 Definitions> struct ExtendedObjectOf {
    typedef ExtendedGlyphObject<N, T, P, Paths, Glyphs, Definitions> type;
};
template <u32 N, u32 T, u32 P, u32 Paths> struct ExtendedObjectOf<N, T, P, Paths, 0, 0> {
    typedef ExtendedObject<N, T, P, Paths> type;
};
// Stock path_draw reads this byte of a path leaf before it draws the path.
const u32 visibility_byte = 92;

template <class Frame> struct ExtendedRetained {
    enum { path_count = Frame::path_count, point_count = Frame::point_count, node_count = Frame::node_count,
           text_count = Frame::text_count, palette_count = Frame::palette_count };
    typedef typename ExtendedObjectOf<node_count, text_count, palette_count, path_count, Frame::glyph_count,
                                      Frame::glyph_definition_count>::type Object;

    static PathKind kind(u32 path) {
        return Frame::cubic ? cubic_curve : (Frame::segment_paths & (u32(1) << path)) ? separate_segments :
               (Frame::open_paths & (u32(1) << path)) ? open_polyline : closed_polyline;
    }
    // The path's command vector begin while its command types are still the
    // ones its artwork declared (initial: as authored, filling its reservation), or 0.
    static u32 geometry(void* raw, u32 path, bool initial = false) {
        const PathNode* node = path_node(raw);
        if (!node) return 0;
        const Vector& v = node->commands;
        if (!v.begin || v.begin & 3 || v.end < v.begin || v.capacity < v.end) return 0;
        if (v.capacity - v.begin > 16384 || (v.end - v.begin) % 32) return 0;
        const PathCommand* command = reinterpret_cast<const PathCommand*>(v.begin);
        if (Frame::quadratic) {
            if (v.capacity - v.begin < 32 * Frame::command_capacity) return 0;
            return quadratic_types(command, (v.end - v.begin) / 32, Frame::command_capacity, initial) ? v.begin : 0;
        }
        if (Frame::counted) {
            if (v.capacity - v.begin < 32 * command_count(point_count, kind(path))) return 0;
            return counted_types(command, (v.end - v.begin) / 32, point_count, kind(path), initial) ? v.begin : 0;
        }
        if (v.end - v.begin != 32 * command_count(point_count, kind(path))) return 0;
        return command_types(command, point_count, kind(path)) ? v.begin : 0;
    }

    // Copies a changed path's coordinates while its vector is still the one
    // validated at bind. Quadratic and counted paths also set their command
    // types and active end, within the reservation; nothing is reallocated.
    static void apply_path(Object* o, const Frame& frame, u32 p) {
        const typename Frame::path_type& path = frame.paths[p];
        if (path.key == o->keys[p]) return;
        const u32 begin = geometry(o->nodes[p], p);
        if (!begin || begin != o->geometry[p]) return;
        PathCommand* command = reinterpret_cast<PathCommand*>(begin);
        const u32 count = path_coordinates(command, path, kind(p));
        if (Frame::quadratic || Frame::counted) o->nodes[p]->commands.end = begin + 32 * count;
        invalidate(o->nodes[p]);
        o->keys[p] = path.key;
    }

    // A direct colour replaces the node's own paint record, never a shared one.
    static void apply_rgba(Object* o, u32 i, u32 color, draw::Paint mode) {
        PathNode* node = o->nodes[i];
        if (!node || !o->base.svg) return;
        bool changed = false;
        if (mode != draw::rgba_fill && rgba_detail::unique(o->nodes, node_count, i, false)) {
            const Vector& v = o->base.svg->strokes;
            changed = rgba_detail::set_color(v.begin, v.end, v.capacity, 20, node->stroke, color);
        }
        if (mode != draw::rgba_stroke && static_cast<__INT16_TYPE__>(node->fill) > 0 &&
            rgba_detail::unique(o->nodes, node_count, i, true)) {
            const Vector& v = o->base.svg->fills;
            changed = rgba_detail::set_color(v.begin, v.end, v.capacity, 4, node->fill, color) || changed;
        }
        if (changed) invalidate(node);
    }

    static void apply_node(Object* o, const Frame& frame, u32 i) {
        const typename Frame::node_type& node = frame.nodes[i];
        if (!o->nodes[i]) return;
        reinterpret_cast<u8*>(o->nodes[i])[visibility_byte] = node.paint == draw::hidden ? 0 : o->authored_visibility[i];
        if (node.paint == draw::hidden) return;
        if (Frame::rgba && node.paint >= draw::rgba_stroke) apply_rgba(o, i, node.shade, node.paint);
        else if (node.paint != draw::keep) paint(o->nodes[i], o->palette[node.shade], node.paint == draw::fill_and_stroke);
        const draw::Matrix m = draw::matrix(node);
        const u32 at[6] = {0, 1, 3, 4, 6, 7};
        const i32 value[6] = {m.a, m.b, m.c, m.d, m.x, m.y};
        set_words(&o->nodes[i]->leaf, at, value);
    }

    static void apply_text(Object* o, const Frame& frame, u32 i) {
        const draw::Text& text = frame.texts[i];
        Leaf* leaf = o->texts[i];
        if (!leaf) return;
        const u32 at[6] = {0, 1, 3, 4, 6, 7};
        const i32 value[6] = {text.scale, 0, 0, text.scale, text.x, text.y};
        set_words(leaf, at, value);
        char* shown = o->shown[i];
        u32 n = 0;
        while (n < draw::text_capacity && shown[n] == text.chars[n] && text.chars[n]) ++n;
        if (n < draw::text_capacity && shown[n] == text.chars[n]) return;
        for (n = 0; n < draw::text_capacity; ++n) shown[n] = text.chars[n];
        set_text(leaf, shown);
    }

    // Machine::ids names the nodes (the first are the mutable paths), then the text slots, then
    // the palette entries.
    template <class Machine> static void bind(Object* o) {
        for (u32 i = 0; i < node_count; ++i) {
            o->nodes[i] = path_node(find(&o->base, Machine::ids[i]));
            o->authored_visibility[i] = o->nodes[i] ? reinterpret_cast<u8*>(o->nodes[i])[visibility_byte] : 0;
        }
        for (u32 i = 0; i < text_count; ++i) {
            o->texts[i] = text_node(find(&o->base, Machine::ids[node_count + i]));
            o->shown[i][0] = 1;
        }
        for (u32 i = 0; i < palette_count; ++i)
            o->palette[i] = palette_ids(find(&o->base, Machine::ids[node_count + text_count + i]), o->base.svg);
        for (u32 i = 0; i < path_count; ++i) {
            o->geometry[i] = geometry(o->nodes[i], i, true);
            o->keys[i] = 0;
        }
        glyph_detail::Binder<Frame>::template init<Machine>(o);
    }
    // The frame was validated by display_valid before this.
    static void apply(Object* o, const Frame& frame) {
        if (!glyph_detail::Binder<Frame>::apply(o, frame)) return;
        for (u32 p = 0; p < path_count; ++p) apply_path(o, frame, p);
        for (u32 i = 0; i < node_count; ++i) apply_node(o, frame, i);
        for (u32 i = 0; i < text_count; ++i) apply_text(o, frame, i);
    }
};
}}} // namespace opxy::screen::<unnamed>
