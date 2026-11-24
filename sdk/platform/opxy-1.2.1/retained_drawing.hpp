#pragma once
#include "screen.hpp"
namespace opxy {
namespace screen {
namespace {
template <class Frame> struct RetainedDrawing {
    enum {
        path_count = Frame::path_count,
        point_count = Frame::point_count,
        node_count = Frame::node_count,
        text_count = Frame::text_count,
        palette_count = Frame::palette_count
    };
    struct Object {
        Base base;
        PathNode *nodes[node_count];                  // path leaves, or 0 when missing or foreign
        Leaf *texts[text_count > 0 ? text_count : 1]; // text leaves, or 0
        char shown[text_count > 0 ? text_count : 1][draw::text_capacity]; // each slot's applied text
        u32 palette[palette_count];                                       // packed palette entry IDs, or 0
        i32 delta;                                     // elapsed time of the current frame, Q10 seconds
        u32 geometry[path_count > 0 ? path_count : 1]; // each path's command vector
                                                       // as validated at init
        u32 keys[path_count > 0 ? path_count : 1];     // each path's last applied key
    };

    // The path's command vector begin if it is exactly M, (points - 1) L, Z, or
    // 0.
    static u32 geometry(void *raw) {
        const PathNode *node = path_node(raw);
        if (!node)
            return 0;
        const Vector &v = node->commands;
        if (!v.begin || v.begin & 3 || v.end < v.begin || v.capacity < v.end)
            return 0;
        if (v.end - v.begin != 32 * (point_count + 1) || v.capacity - v.begin > 4096)
            return 0;
        const Command *command = reinterpret_cast<const Command *>(v.begin);
        for (u32 i = 0; i < point_count; ++i)
            if (command[i].type != (i ? 2 : 1))
                return 0;
        return command[point_count].type == 5 ? v.begin : 0;
    }

    // Copies a changed path's coordinates only while its topology is still the
    // one validated at init; types and vector metadata stay intact.
    static void apply_path(Object *o, const Frame &frame, u32 p) {
        const typename Frame::path_type &path = frame.paths[p];
        if (path.key == o->keys[p])
            return;
        const u32 begin = geometry(o->nodes[p]);
        if (!begin || begin != o->geometry[p])
            return;
        Command *command = reinterpret_cast<Command *>(begin);
        for (u32 i = 0; i < point_count; ++i) {
            command[i].x = path.points[i].x;
            command[i].y = path.points[i].y;
        }
        invalidate(o->nodes[p]);
        o->keys[p] = path.key;
    }

    // Each node's matrix is its scale times its turn, at (x, y).
    static void apply_node(Object *o, const Frame &frame, u32 i) {
        const draw::Node &node = frame.nodes[i];
        if (node.paint != draw::keep)
            paint(o->nodes[i], o->palette[node.shade], node.paint == draw::fill_and_stroke);
        if (!o->nodes[i])
            return;
        const i32 a = draw::floor_q10(node.scale * node.cos), b = draw::floor_q10(node.scale * node.sin);
        const u32 at[6] = {0, 1, 3, 4, 6, 7};
        const i32 value[6] = {a, b, -b, a, node.x, node.y};
        set_words(&o->nodes[i]->leaf, at, value);
    }

    static void apply_text(Object *o, const Frame &frame, u32 i) {
        const draw::Text &text = frame.texts[i];
        Leaf *leaf = o->texts[i];
        if (!leaf)
            return;
        const u32 at[6] = {0, 1, 3, 4, 6, 7};
        const i32 value[6] = {text.scale, 0, 0, text.scale, text.x, text.y};
        set_words(leaf, at, value);
        char *shown = o->shown[i];
        u32 n = 0;
        while (n < draw::text_capacity && shown[n] == text.chars[n] && text.chars[n])
            ++n;
        if (n < draw::text_capacity && shown[n] == text.chars[n])
            return;
        for (n = 0; n < draw::text_capacity; ++n)
            shown[n] = text.chars[n];
        set_text(leaf, shown);
    }

    static void bind(Object *o, const char *const *ids) {
        for (u32 i = 0; i < node_count; ++i)
            o->nodes[i] = path_node(find(&o->base, ids[i]));
        for (u32 i = 0; i < text_count; ++i) {
            o->texts[i] = text_node(find(&o->base, ids[node_count + i]));
            o->shown[i][0] = 1;
        }
        for (u32 i = 0; i < palette_count; ++i)
            o->palette[i] = palette_ids(find(&o->base, ids[node_count + text_count + i]), o->base.svg);
        for (u32 i = 0; i < path_count; ++i) {
            o->geometry[i] = geometry(o->nodes[i]);
            o->keys[i] = 0;
        }
    }
    static void apply(Object *o, const Frame &frame) {
        if (!draw::valid(frame))
            return;
        for (u32 p = 0; p < path_count; ++p)
            apply_path(o, frame, p);
        for (u32 i = 0; i < node_count; ++i)
            apply_node(o, frame, i);
        for (u32 i = 0; i < text_count; ++i)
            apply_text(o, frame, i);
    }
};
} // namespace
} // namespace screen
} // namespace opxy
