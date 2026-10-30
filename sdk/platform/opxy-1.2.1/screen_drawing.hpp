// Retained drawing renderer: applies the machine's validated draw::Frame to its
// artwork's path outlines, node transforms and paint, text slots and header
// values. Include after the generated display_binding.hpp, which names the
// frame shape.
#pragma once
#include "screen.hpp"
#include "note_activity.hpp"

extern "C" opxy::u32 sdk_screen_activation(opxy::u32, opxy::u32) __asm__("sdk_screen_activation");

namespace opxy { namespace screen { namespace {
// One typed GUI state, input and frame; never accessed by audio callbacks.
binding::DisplayState display_state;
u32 display_activation;
draw::Input display_input;
binding::Frame display_frame;

// A drum kit's display is struck only by its selected pad's notes. Audio and
// GUI are separate roles, so this reads only GUI state: when stock's note
// counter moves, the pitches that started since the last frame (newly held,
// or released since) are mapped to their pads. A start no pitch accounts for
// strikes as before, so the screen never misses a hit it cannot place.
struct PadNotes { u32 track, valid, notes, serial; u8 held[128], released[128], now[128], now_released[128]; };
PadNotes pad_notes;
u32 display_note_serial(const Controls& controls) {
    if (controls.track >= 8) return 0;
    const u32 notes = note_serial(controls.track);
    if (!controls.pad) {
        pad_notes.valid = 0;
        return notes;
    }
    PadNotes& s = pad_notes;
    sdk_note_pitches(controls.track, s.now, s.now_released);
    if (s.valid && s.track == controls.track && notes != s.notes) {
        bool placed = false, struck = false;
        for (u32 p = 0; p < 128 && !struck; ++p) {
            const bool released = s.now_released[p] != s.released[p];
            if (!(s.now[p] && !s.held[p]) && !(released && (s.now[p] || !s.held[p]))) continue;
            placed = true;
            struck = sdk_drum_pad(controls.track, p) == controls.pad;
        }
        if (struck || !placed) ++s.serial;
    }
    for (u32 p = 0; p < 128; ++p) {
        s.held[p] = s.now[p];
        s.released[p] = s.now_released[p];
    }
    s.track = controls.track;
    s.notes = notes;
    s.valid = 1;
    return s.serial;
}

// Machine: ids[] names the artwork's nodes (the first are the mutable paths),
// then its text slots, then its palette entries.
template <class Machine> struct Drawing {
    typedef binding::Frame Frame;
    enum { path_count = Frame::path_count, point_count = Frame::point_count, node_count = Frame::node_count,
           text_count = Frame::text_count, palette_count = Frame::palette_count };
    static_assert(sizeof(Machine::ids) / sizeof(Machine::ids[0]) == node_count + text_count + palette_count,
                  "Drawing artwork IDs");
    struct Object {
        Base base;
        PathNode* nodes[node_count];  // path leaves, or 0 when missing or foreign
        Leaf* texts[text_count > 0 ? text_count : 1];  // text leaves, or 0
        char shown[text_count > 0 ? text_count : 1][draw::text_capacity];  // each slot's applied text
        u32 palette[palette_count];    // packed palette entry IDs, or 0
        i32 delta;               // elapsed time of the current frame, Q10 seconds
        u32 geometry[path_count > 0 ? path_count : 1];  // each path's command vector as validated at init
        u32 keys[path_count > 0 ? path_count : 1];      // each path's last applied key
    };

    // The path's command vector begin if it is exactly M, (points - 1) L, Z, or 0.
    static u32 geometry(void* raw) {
        const PathNode* node = path_node(raw);
        if (!node) return 0;
        const Vector& v = node->commands;
        if (!v.begin || v.begin & 3 || v.end < v.begin || v.capacity < v.end) return 0;
        if (v.end - v.begin != 32 * (point_count + 1) || v.capacity - v.begin > 4096) return 0;
        const Command* command = reinterpret_cast<const Command*>(v.begin);
        for (u32 i = 0; i < point_count; ++i)
            if (command[i].type != (i ? 2 : 1)) return 0;
        return command[point_count].type == 5 ? v.begin : 0;
    }

    static void init(Object* o) {
        clear(o);
        display_activation = 0;
        binding::display_reset(&display_state, &display_frame);
        if (!o->base.svg) return;
        bind_controls(&o->base);
        for (u32 i = 0; i < node_count; ++i) o->nodes[i] = path_node(find(&o->base, Machine::ids[i]));
        for (u32 i = 0; i < text_count; ++i) {
            o->texts[i] = text_node(find(&o->base, Machine::ids[node_count + i]));
            o->shown[i][0] = 1;  // apply every slot on the first frame
        }
        for (u32 i = 0; i < palette_count; ++i)
            o->palette[i] = palette_ids(find(&o->base, Machine::ids[node_count + text_count + i]), o->base.svg);
        for (u32 i = 0; i < path_count; ++i) o->geometry[i] = geometry(o->nodes[i]);
    }

    // Copies a changed path's coordinates only while its topology is still the
    // one validated at init; types and vector metadata stay intact.
    static void apply_path(Object* o, u32 p) {
        const typename Frame::path_type& path = display_frame.paths[p];
        if (path.key == o->keys[p]) return;
        const u32 begin = geometry(o->nodes[p]);
        if (!begin || begin != o->geometry[p]) return;
        Command* command = reinterpret_cast<Command*>(begin);
        for (u32 i = 0; i < point_count; ++i) {
            command[i].x = path.points[i].x;
            command[i].y = path.points[i].y;
        }
        invalidate(o->nodes[p]);
        o->keys[p] = path.key;
    }

    // Each node's matrix is its scale times its turn, at (x, y).
    static void apply_node(Object* o, u32 i) {
        const draw::Node& node = display_frame.nodes[i];
        if (node.paint != draw::keep) paint(o->nodes[i], o->palette[node.shade], node.paint == draw::fill_and_stroke);
        if (!o->nodes[i]) return;
        const i32 a = draw::floor_q10(node.scale * node.cos), b = draw::floor_q10(node.scale * node.sin);
        const u32 at[6] = {0, 1, 3, 4, 6, 7};
        const i32 value[6] = {a, b, -b, a, node.x, node.y};
        set_words(&o->nodes[i]->leaf, at, value);
    }

    static void apply_text(Object* o, u32 i) {
        const draw::Text& text = display_frame.texts[i];
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

    static void animate(Object* o, const Controls& controls) {
        if (!controls.valid) return;
        const u32 activation = sdk_screen_activation(o->base.page, controls.track);
        if (activation && activation != display_activation) {
            binding::display_reset(&display_state, &display_frame);
            // A reset may reuse earlier nonzero geometry keys with new points.
            for (u32 p = 0; p < path_count; ++p) o->keys[p] = 0;
            display_activation = activation;
        }
        for (u32 i = 0; i < page_controls; ++i) {
            display_input.parameters[i] = controls.values[i];
            display_input.shown[i] = controls.shown[i];
        }
        for (u32 i = 0; i < parameter_slots; ++i) display_input.slots[i] = controls.slots[i];
        display_input.model = controls.model;
        display_input.shift = controls.choosing ? 1 : 0;
        for (u32 i = 0; i < 4; ++i) display_input.choices[i] = controls.choices[i];
        display_input.elapsed = o->delta;
        display_input.track = controls.track;
        display_input.note_serial = display_note_serial(controls);
        sdk_display_note_activity(controls.track, &display_input);
        display_input.reduced_motion = 0;
        binding::display_render(&display_state, &display_input, &display_frame);
        if (!binding::display_valid(&display_frame)) return;
        for (u32 p = 0; p < path_count; ++p) apply_path(o, p);
        for (u32 i = 0; i < node_count; ++i) apply_node(o, i);
        for (u32 i = 0; i < text_count; ++i) apply_text(o, i);
        // Stock wrote every value this frame; a header string replaces its
        // own, except on a drum kit's Shift page, whose values stay blank.
        for (u32 i = 0; i < page_controls; ++i)
            if (!controls.choosing && display_frame.header[i][0])
                if (Leaf* value = text_node(o->base.values[i])) set_text(value, display_frame.header[i]);
    }

    // Incoming elapsed time is the dispatcher's, in Q10 seconds.
    static void frame(Object* o, i32 elapsed) {
        o->delta = elapsed;
        Controls controls;
        if (!refresh_controls(&o->base, controls)) return;
        animate(o, controls);
        draw(&o->base);
    }
};
}}} // namespace opxy::screen::<unnamed>
