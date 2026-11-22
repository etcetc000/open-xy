// Extended drawing renderer (docs/display.md#extended-drawing): applies
// an extended frame (draw.hpp ExtendedFrame, RgbaFrame) to a synth's or
// effect's artwork. Only an extended display's generated screen.cpp includes this,
// after its display_binding.hpp; plain displays keep screen_drawing.hpp.
#pragma once
#include "screen.hpp"
#include "screen_header.hpp"
#include "note_activity.hpp"
#include "amp_envelope.hpp"
#include "drum_send.hpp"
#include "path_geometry.hpp"
#include "rgba_palette.hpp"

extern "C" opxy::u32 sdk_screen_activation(opxy::u32, opxy::u32) __asm__("sdk_screen_activation");

namespace opxy { namespace screen { namespace {
// One typed GUI state, input and frame; never accessed by audio callbacks.
binding::DisplayState extended_state;
u32 extended_activation;
binding::DisplayInput extended_input;
binding::Frame extended_frame;

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
// Stock path_draw reads this byte of a path leaf before it draws the path.
const u32 visibility_byte = 92;

template <class Frame> struct ExtendedRetained {
    enum { path_count = Frame::path_count, point_count = Frame::point_count, node_count = Frame::node_count,
           text_count = Frame::text_count, palette_count = Frame::palette_count };
    typedef ExtendedObject<node_count, text_count, palette_count, path_count> Object;

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
    }
    // The frame was validated by display_valid before this.
    static void apply(Object* o, const Frame& frame) {
        for (u32 p = 0; p < path_count; ++p) apply_path(o, frame, p);
        for (u32 i = 0; i < node_count; ++i) apply_node(o, frame, i);
        for (u32 i = 0; i < text_count; ++i) apply_text(o, frame, i);
    }
};

// A whole-screen canvas has no stock title or value leaves: the page's
// controls as refresh_controls reads them for a synth or an effect, without
// writing any stock text.
inline bool canvas_controls(Base* o, Controls& controls) {
    if (!o->svg) return false;
    controls.track = selected_track();
    controls.shift = 0;
    controls.choosing = false;
    controls.model = controls.pad = 0;
    controls.pan = controls.level = 0;
    controls.line = 0;
    for (u32 i = 0; i < platform::kDrumPadFields; ++i) controls.pads[i] = 0;
    for (u32 i = 0; i < 4; ++i) controls.choices[i] = 0;
    platform::SendView view;
    if (sdk_send_view(controls.track, &view)) {
        for (u32 i = 0; i < parameter_slots; ++i) controls.slots[i] = view.slots[i];
        for (u32 i = 0; i < page_controls; ++i) {
            controls.shown[i] = view.shown[i];
            controls.values[i] = view.values[i];
        }
        controls.shift = view.shift;
        for (u32 i = 0; i < 4; ++i) controls.choices[i] = view.choices[i];
        controls.valid = true;
        return true;
    }
    sdk_synth_shown(controls.track, controls.shown);
    controls.line = controls.shown[0] >= page_controls ? 1 : 0;
    for (u32 i = 0; i < page_controls; ++i) controls.values[i] = 0;
    for (u32 i = 0; i < parameter_slots; ++i) controls.slots[i] = 0;
    u32 words[parameter_slots];
    controls.valid = read_shown(parameters(controls.track), controls, words);
    return true;
}

// Machine: ids[] names the artwork's nodes (the first are the mutable paths),
// then its text slots, then its palette entries.
template <class Machine> struct ExtendedDrawing {
    typedef binding::Frame Frame;
    typedef ExtendedRetained<Frame> Renderer;
    typedef typename Renderer::Object Object;
    static_assert(sizeof(Machine::ids) / sizeof(Machine::ids[0]) ==
                  Frame::node_count + Frame::text_count + Frame::palette_count, "Drawing artwork IDs");

    static void init(Object* o) {
        clear(o);
        extended_activation = 0;
        binding::display_reset(&extended_state, &extended_frame);
        if (!o->base.svg) return;
        if (!binding::full_canvas) bind_controls(&o->base);
        Renderer::template bind<Machine>(o);
    }

    static void animate(Object* o, const Controls& controls) {
        if (!controls.valid) return;
        const u32 activation = sdk_screen_activation(o->base.page, controls.track);
        if (activation && activation != extended_activation) {
            binding::display_reset(&extended_state, &extended_frame);
            // A reset may reuse earlier nonzero geometry keys with new points.
            for (u32 p = 0; p < Frame::path_count; ++p) o->keys[p] = 0;
            extended_activation = activation;
        }
        draw::Input& input = extended_input;
        for (u32 i = 0; i < page_controls; ++i) {
            input.parameters[i] = controls.values[i];
            input.shown[i] = controls.shown[i];
        }
        for (u32 i = 0; i < parameter_slots; ++i) input.slots[i] = controls.slots[i];
        input.model = controls.model;
        input.pan = static_cast<i8>(controls.pan);
        input.level = static_cast<i8>(controls.level);
        input.line = static_cast<u8>(controls.line);
        input.reserved = 0;
        const u32 pads = sizeof(controls.pads) / sizeof(controls.pads[0]);
        for (u32 i = 0; i < 4; ++i) input.pad[i] = static_cast<i8>(i < pads ? controls.pads[i] : 0);
        for (u32 i = platform::kDrumKitPadFields; i < 4 && i < pads; ++i)
            input.pad[i] = static_cast<i8>(platform::drum_send_shown(static_cast<u32>(controls.pads[i]), Machine::percent_sends));
        input.shift = controls.choosing ? 1 : controls.shift;
        for (u32 i = 0; i < 4; ++i) input.choices[i] = controls.choices[i];
        input.elapsed = o->delta;
        input.track = controls.track;
        input.note_serial = controls.track < 8 ? note_serial(controls.track) : 0;
        sdk_display_note_activity(controls.track, &input);
        platform::amp_envelope(controls.track, input.amp);
        input.reduced_motion = 0;
        binding::snapshot_encoders(extended_input);
        binding::display_render(&extended_state, &extended_input, &extended_frame);
        if (!binding::display_valid(&extended_frame)) return;
        Renderer::apply(o, extended_frame);
        if (!binding::full_canvas && !controls.choosing) apply_header(&o->base, extended_frame);
    }

    // Incoming elapsed time is the dispatcher's, in Q10 seconds.
    static void frame(Object* o, i32 elapsed) {
        o->delta = elapsed;
        Controls controls;
        if (!(binding::full_canvas ? canvas_controls(&o->base, controls) : refresh_controls(&o->base, controls))) return;
        animate(o, controls);
        draw(&o->base);
    }
};
}}} // namespace opxy::screen::<unnamed>
