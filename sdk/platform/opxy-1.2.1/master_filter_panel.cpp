#include "master_filter.hpp"
#include "screen.hpp"
#include <opxy/q31.hpp>
using namespace opxy;
using namespace opxy::screen;
namespace opxy { namespace platform { namespace master_filter {
namespace {
struct Object {
    Base base;
    PathNode *paths[master::max_paths];
    Address geometry[master::max_paths];
    PathNode *color;
    u32 grays[master::max_grays];
    bool ready;
};
Object object;
master::Input input = {512, 0, 0, 0, 0};
const master::Panel *panel;
bool applying;
bool constructed;
bool bound;
void init(Object *o) {
    clear(o);
    if (!o->base.svg)
        return;
    for (u32 i = 0; i < panel->path_count; ++i) {
        PathNode *node = path_node(find(&o->base, panel->paths[i].id));
        if (!node || !ordered(node->commands))
            return;
        const Vector &v = node->commands;
        const u32 count = panel->paths[i].points;
        if ((v.begin & 3) || v.end - v.begin != 32 * (count + (panel->paths[i].closed ? 1 : 0)))
            return;
        const Command *c = reinterpret_cast<const Command *>(v.begin);
        for (u32 j = 0; j < count; ++j)
            if (c[j].type != (j ? 2 : 1))
                return;
        if (panel->paths[i].closed && c[count].type != 5)
            return;
        o->paths[i] = node;
        o->geometry[i] = v.begin;
    }
    o->color = path_node(find(&o->base, panel->gray_target));
    if (!o->color)
        return;
    const Document *svg = o->base.svg;
    if (!ordered(svg->fills))
        return;
    u32 prefix = 0;
    while (prefix < 24 && panel->gray_prefix[prefix])
        ++prefix;
    if (prefix == 24)
        return;
    for (u32 gray = 0; gray < panel->gray_count; ++gray) {
        char id[28];
        u32 n = 0;
        for (; n < prefix; ++n)
            id[n] = panel->gray_prefix[n];
        if (gray >= 100)
            id[n++] = static_cast<char>('0' + gray / 100);
        if (gray >= 10)
            id[n++] = static_cast<char>('0' + gray / 10 % 10);
        id[n++] = static_cast<char>('0' + gray % 10);
        id[n] = 0;
        PathNode *donor = path_node(find(&o->base, id));
        if (!donor || !donor->fill || 4u * donor->fill > svg->fills.end - svg->fills.begin)
            return;
        o->grays[gray] = donor->fill;
    }
    o->ready = true;
}
void unused_frame(Object *, i32) {
}
const VTable<Object> methods = {&destroy<Object>,
                                &destroy<Object>,
                                reinterpret_cast<const void *>(0x8d51fcd4),
                                unused_frame,
                                init,
                                reinterpret_cast<const void *>(0x8c0867f0)};
bool set_path(void *context, u32 index, const master::Point *points, u32 count, bool show) {
    Object *o = static_cast<Object *>(context);
    if (!applying || !o->ready || index >= panel->path_count || !points ||
        count != panel->paths[index].points)
        return false;
    PathNode *node = o->paths[index];
    if (node->commands.begin != o->geometry[index]) {
        o->ready = false;
        return false;
    }
    for (u32 j = 0; j < count; ++j)
        if (points[j].x < -1024 * 1024 || points[j].x > 1024 * 1024 || points[j].y < -1024 * 1024 ||
            points[j].y > 1024 * 1024)
            return false;
    if (show && panel->paths[index].closed) {
        i64 area = 0;
        for (u32 j = 0; j < count; ++j) {
            const u32 next = j + 1 < count ? j + 1 : 0;
            area += q31::wide(points[j].x, points[next].y) - q31::wide(points[next].x, points[j].y);
        }
        if (!area)
            show = false; // Do not feed a collapsed filled polygon to stock.
    }
    const u32 at[2] = {6, 7};
    const i32 offset[2] = {show ? 0 : 640 * 1024, show ? 0 : 320 * 1024};
    set_words(&node->leaf, at, offset);
    if (!show)
        return true;
    Command *commands = reinterpret_cast<Command *>(o->geometry[index]);
    bool changed = false;
    for (u32 j = 0; j < count; ++j) {
        changed = changed || commands[j].x != points[j].x || commands[j].y != points[j].y;
        commands[j].x = points[j].x;
        commands[j].y = points[j].y;
    }
    if (changed)
        invalidate(node);
    return true;
}
bool set_gray(void *context, u32 shade) {
    Object *o = static_cast<Object *>(context);
    if (!applying || !o->ready || shade >= panel->gray_count)
        return false;
    if (o->color->fill != o->grays[shade]) {
        set_fill(o->color, o->grays[shade]);
        invalidate(o->color);
    }
    return true;
}
void blank() {
    i32 begin[2] = {-4096, 110 * 1024}, end[2] = {484 * 1024, 110 * 1024};
    const u32 black = 0xff000000;
    stock<void(i32 *, i32 *, i32, const u32 *)>(0x8d5fe884)(begin, end, 224 * 1024, &black);
}
} // namespace
void frame(u32 sweep, i32 elapsed) {
    const auto *filter = descriptor();
    if (!filter || !filter->panel) {
        blank();
        return;
    }
    panel = filter->panel;
    if (!panel->paths || !panel->gray_target || !panel->gray_prefix || !panel->frame || !panel->path_count ||
        panel->path_count > master::max_paths || !panel->gray_count ||
        panel->gray_count > master::max_grays) {
        blank();
        return;
    }
    for (u32 i = 0; i < panel->path_count; ++i)
        if (!panel->paths[i].id || panel->paths[i].points < 2 ||
            panel->paths[i].points > master::max_points) {
            blank();
            return;
        }
    if (!constructed) {
        // ID zero is stock's explicit unregistered mode. Never occupy EQ slot33.
        base_construct(&object.base, 0, asset());
        object.base.vtable = &methods;
        constructed = true;
    }
    if (!bound) {
        if (!object.base.svg)
            stock<void(Base *)>(0x8d520ce0)(&object.base);
        if (object.base.svg) {
            init(&object);
            bound = true;
        }
    }
    if (!object.ready) {
        blank();
        return;
    }
    input.sweep = sweep;
    input.elapsed = elapsed;
    const master::Scene scene = {&object, set_path, set_gray};
    applying = true;
    panel->frame(scene, input);
    applying = false;
    if (object.ready)
        draw(&object.base);
    else
        blank();
}
void reset_visual() {
    ++input.reset_serial;
}
void follow_visual(u32) {
    ++input.turn_serial;
}
void reset_project() {
    ++input.project_serial;
}
}}} // namespace opxy::platform::master_filter
