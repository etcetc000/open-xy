// Stock OS 1.2.1 screen objects for SDK displays: the stock object layout,
// helpers and the shared renderer shell. Stock calls every method-table entry
// with the C ABI (the object in R0). Each machine's generated screen.cpp
// includes the drawing renderer (screen_drawing.hpp) once, so everything here
// has internal linkage.
#pragma once
#include "opxy/types.hpp"
#include "stock.hpp"
#include "synth_controls.hpp"

namespace opxy { namespace screen { namespace {

using platform::stock;
typedef u32 __attribute__((may_alias)) word;

const u32 kPathLeaf = 0x840c4d74;  // vtable of a stock SVG path leaf
const u32 kTextLeaf = 0x840c5158;  // vtable stored by the stock text constructor 0x8d600e20

// A stock vector of records.
struct Vector { u32 begin, end, capacity; };
inline bool ordered(const Vector& v) { return v.begin && v.end >= v.begin && v.capacity >= v.end; }

// A stock SVG leaf: its 3x3 Q10 matrix is words a b . c d . tx ty .
struct Leaf { u32 vtable; i32 matrix[9]; };
// A path leaf: own-SVG fill/stroke palette selectors and its command vector.
struct PathNode {
    Leaf leaf;
    u32 stock40[18];
    u16 fill, stroke;
    u32 stock116;
    Vector commands;  // 32-byte records: x, y, ..., type byte at +24
};
struct Command { i32 x, y; u32 stock8[4]; unsigned char type, stock25[7]; };
// The loaded artwork: its fill (4-byte) and stroke (20-byte) palette records.
struct Document { u32 stock0[25]; Vector fills; u32 stock112[5]; Vector strokes; };
// The stock control parameter vector: eight Q31 words from begin.
struct Parameters { u32 stock0; u32 begin, end; };
static_assert(sizeof(Leaf) == 40 && __builtin_offsetof(PathNode, fill) == 112 &&
              __builtin_offsetof(PathNode, stroke) == 114 && __builtin_offsetof(PathNode, commands) == 120 &&
              sizeof(Command) == 32 && __builtin_offsetof(Command, type) == 24 &&
              __builtin_offsetof(Document, fills) == 100 && __builtin_offsetof(Document, strokes) == 132 &&
              __builtin_offsetof(Parameters, begin) == 4, "Stock 1.2.1 screen layout");

// The 76-byte stock screen object. Adapter state follows it.
struct Base {
    const void* vtable;
    const char* asset;
    const Document* svg;  // the loaded artwork, 0 until it loads
    u32 page;
    const void* loader;
    u32 stock20;
    Leaf* values[4];      // control value and title text nodes, bound by stock
    Leaf* titles[4];
    u32 stock56[5];
};
static_assert(sizeof(Base) == 76 && __builtin_offsetof(Base, values) == 24, "Stock 1.2.1 screen object");

inline void base_construct(Base* o, u32 page, const char* asset) {
    stock<void(Base*, u32, const char*)>(0x8d520844)(o, page, asset);
}
inline void base_destroy(Base* o) { stock<void(Base*)>(0x8d51fdb8)(o); }
// Binds child i of the template's `values` group to values[i] and of `titles` to titles[i].
inline void bind_controls(Base* o) { stock<void(Leaf**, Base*, u32)>(0x8d521704)(o->values, o, 0); }
inline void* find(Base* o, const char* id) { return platform::find_node(o, id); }
inline void draw(Base* o) { stock<void(Base*, u32)>(0x8d51fe1c)(o, 0); }
inline u32 selected_track() { return platform::selected_track(); }
inline const Parameters* parameters(u32 track) { return stock<const Parameters*(u32)>(0x8d4d72a8)(track); }
inline void update_controls(Base* o, u32 track, const Parameters* p) {
    stock<void(Leaf**, u32, const Parameters*)>(0x8d5466ac)(o->values, track, p);
}
inline u32 note_serial(u32 track) { return stock<u32(u32)>(0x8d4d6c08)(track); }
inline void set_matrix(Leaf* node, const i32* matrix) { stock<void(Leaf*, const i32*)>(0x8d5f06a6)(node, matrix); }
inline void set_text(Leaf* node, const char* text) { platform::set_text(node, text); }
inline void set_fill(PathNode* node, u32 id) { stock<void(PathNode*, u32)>(0x8d5fa986)(node, id); }
inline void set_stroke(PathNode* node, u32 id) { stock<void(PathNode*, u32)>(0x8d5fa966)(node, id); }
inline void invalidate(PathNode* node) { stock<void(PathNode*)>(0x8d5fa9c4)(node); }

// Only stock leaves of the expected kind are ever passed to their setters.
inline PathNode* path_node(void* node) {
    return node && static_cast<Leaf*>(node)->vtable == kPathLeaf ? static_cast<PathNode*>(node) : 0;
}
inline Leaf* text_node(void* node) {
    return node && static_cast<Leaf*>(node)->vtable == kTextLeaf ? static_cast<Leaf*>(node) : 0;
}

// A donor path's positive fill/stroke selectors, packed fill | stroke << 16, or
// 0. Both are bounds-checked against the artwork's own palette vectors before
// they are cached; nothing is written.
inline u32 palette_ids(void* donor, const Document* svg) {
    const PathNode* node = path_node(donor);
    if (!node) return 0;
    const i32 fill = static_cast<__INT16_TYPE__>(node->fill);
    const u32 stroke = node->stroke;
    if (fill <= 0 || !stroke) return 0;
    if (!ordered(svg->fills) || svg->fills.end - svg->fills.begin < 4 * static_cast<u32>(fill)) return 0;
    if (!ordered(svg->strokes) || svg->strokes.end - svg->strokes.begin < 20 * stroke) return 0;
    return static_cast<u32>(fill) | stroke << 16;
}

// Selects packed palette IDs on a path. Unchanged selectors are skipped, fill
// 0 stays disabled as in the stock setter, and the path is invalidated once
// only when a selector changed.
inline void paint(void* raw, u32 style, bool fill) {
    if (!style) return;
    PathNode* node = path_node(raw);
    if (!node) return;
    bool changed = false;
    if (fill && node->fill && (style & 0xffff) != node->fill) {
        set_fill(node, style & 0xffff);
        changed = true;
    }
    if (style >> 16 != node->stroke) {
        set_stroke(node, style >> 16);
        changed = true;
    }
    if (changed) invalidate(node);
}

// Replaces some of a leaf's nine matrix words through the stock setter, which
// invalidates. A request equal to the current words is skipped.
template <u32 N>
inline void set_words(Leaf* node, const u32 (&at)[N], const i32 (&value)[N]) {
    bool same = true;
    for (u32 i = 0; i < N; ++i) same = same && node->matrix[at[i]] == value[i];
    if (same) return;
    i32 matrix[9];
    for (u32 i = 0; i < 9; ++i) matrix[i] = node->matrix[i];
    for (u32 i = 0; i < N; ++i) matrix[at[i]] = value[i];
    set_matrix(node, matrix);
}

// The four page positions as shown: the track, each position's parameter slot
// (its own, or an alternate an encoder press selected) and that slot's value,
// and every slot's value; valid once the stock vector was readable.
struct Controls {
    u32 track;
    bool valid;
    u8 shown[page_controls];
    i32 values[page_controls];
    i32 slots[parameter_slots];
};

// Fills controls.slots and values from the stock parameter vector,
// bounds-checked; a slot beyond the vector reads 0 and shows the position's
// own. view receives the same vector with each position's shown word in its
// place, for stock's percentages.
inline bool read_shown(const Parameters* p, Controls& controls, u32 (&view)[parameter_slots]) {
    if (!p || !p->begin || p->end < p->begin || p->end - p->begin < 16 || p->begin & 3) return false;
    const u32 words = (p->end - p->begin) / 4 < parameter_slots ? (p->end - p->begin) / 4 : parameter_slots;
    const i32* in = reinterpret_cast<const i32*>(p->begin);
    for (u32 i = 0; i < parameter_slots; ++i) controls.slots[i] = i < words ? in[i] : 0;
    for (u32 i = 0; i < parameter_slots; ++i) view[i] = static_cast<u32>(controls.slots[i]);
    for (u32 i = 0; i < page_controls; ++i) {
        if (controls.shown[i] >= words) controls.shown[i] = static_cast<u8>(i);
        controls.values[i] = controls.slots[controls.shown[i]];
        view[i] = static_cast<u32>(controls.values[i]);
    }
    return true;
}

// The shared opening of every frame: once the artwork and all four value and
// title nodes exist, stock updates the control titles and percentage values of
// the shown parameters. False when there is nothing to draw yet.
inline bool refresh_controls(Base* o, Controls& controls) {
    if (!o->svg) return false;
    for (u32 i = 0; i < 4; ++i)
        if (!o->values[i] || !o->titles[i]) return false;
    controls.track = selected_track();
    sdk_synth_shown(controls.track, controls.shown);
    for (u32 i = 0; i < page_controls; ++i) controls.values[i] = 0;
    for (u32 i = 0; i < parameter_slots; ++i) controls.slots[i] = 0;
    const Parameters* stock_vector = parameters(controls.track);
    u32 words[parameter_slots];
    const Parameters view = {0, static_cast<u32>(reinterpret_cast<platform::Address>(words)),
                             static_cast<u32>(reinterpret_cast<platform::Address>(words + parameter_slots))};
    controls.valid = read_shown(stock_vector, controls, words);
    update_controls(o, controls.track, controls.valid ? &view : stock_vector);
    if (controls.valid) {
        for (u32 i = 0; i < page_controls; ++i) {
            char text[16];
            if (sdk_synth_value(controls.track, controls.shown[i], controls.values[i], text))
                set_text(o->values[i], text);
        }
    }
    return true;
}

// Zeroes the adapter state after the stock object.
template <class Object> void clear(Object* o) {
    static_assert(sizeof(Object) % 4 == 0 && __builtin_offsetof(Object, base) == 0, "Word-sized renderer object");
    word* words = reinterpret_cast<word*>(o);
    for (u32 i = sizeof(Base) / 4; i < sizeof(Object) / 4; ++i) words[i] = 0;
}

// Method-table entries shared by every renderer.
template <class Object> Object* construct(Object* o, u32 page, u32 page_limit, const char* asset, const void* vtable) {
    if (page < 1 || page >= page_limit) return 0;
    base_construct(&o->base, page, asset);
    o->base.vtable = vtable;
    return o;
}
template <class Object> void destroy(Object* o) {
    base_destroy(&o->base);
    o->base.loader = 0;
    clear(o);
}

// Stock method table: destructors, a stock entry, frame, init, a stock entry.
template <class Renderer> struct VTable {
    typedef typename Renderer::Object Object;
    void (*destroy)(Object*);
    void (*destroy_delete)(Object*);
    const void* stock8;
    void (*frame)(Object*, i32);
    void (*init)(Object*);
    const void* stock20;
};
}}} // namespace opxy::screen::<unnamed>

// A machine's exported screen: <prefix>_screen_ctor (object, page ID -> the
// object or 0), _screen_vtable, _screen_asset, and the zero-initialized
// _screen_state object that screen registration constructs in place.
#define OPXY_SCREEN(Renderer, PREFIX, ASSET, PAGE_LIMIT, BYTES)                                            \
    static_assert(sizeof(Renderer::Object) == (BYTES), "Renderer object differs from the declared display"); \
    extern "C" {                                                                                           \
    extern const char opxy_screen_asset[] __asm__(PREFIX "_screen_asset");                                 \
    extern const opxy::screen::VTable<Renderer> opxy_screen_vtable __asm__(PREFIX "_screen_vtable");        \
    Renderer::Object opxy_screen_state __asm__(PREFIX "_screen_state");                                    \
    Renderer::Object* opxy_screen_ctor(Renderer::Object*, opxy::u32) __asm__(PREFIX "_screen_ctor");        \
    const char opxy_screen_asset[] = ASSET;                                                                \
    const opxy::screen::VTable<Renderer> opxy_screen_vtable = {                                            \
        &opxy::screen::destroy<Renderer::Object>, &opxy::screen::destroy<Renderer::Object>,                \
        reinterpret_cast<const void*>(0x8d51fcd4), &Renderer::frame, &Renderer::init,                      \
        reinterpret_cast<const void*>(0x8c0867f0)};                                                        \
    Renderer::Object* opxy_screen_ctor(Renderer::Object* o, opxy::u32 page) {                              \
        return opxy::screen::construct(o, page, PAGE_LIMIT, opxy_screen_asset, &opxy_screen_vtable);       \
    }                                                                                                      \
    }
