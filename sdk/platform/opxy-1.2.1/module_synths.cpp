// Synths from module files. A module's generated init describes an unchanged
// machine (ModuleSynth); this unit does at runtime what the build-time glue
// does for a built-in synth: registration.cpp's checked engine and screen
// registration, synth_pool.cpp in place of voice.cpp.in, and the drawing
// renderer of screen_drawing.hpp with the frame's shape read from the
// description instead of compiled in. Startup calls the hooks at the end.
#include "module_synths.hpp"
#include "module_loader.hpp"
#include "registration.hpp"
#include "project_companion.hpp"
#include "synth_pool.hpp"
#include "screen.hpp"
#include "note_activity.hpp"
#include <opxy/audio.hpp>
#include <opxy/module.hpp>
using namespace opxy;
using namespace opxy::platform;

extern "C" {
u32 sdk_voice_ticks() __asm__("sdk_voice_ticks");
u32 sdk_screen_activation(u32, u32) __asm__("sdk_screen_activation");
}

namespace {
const u32 entries_max = 200;   // the stock engine table, less stock and built-in machines
const u32 page_limit = 256;    // OPXY_SCREEN's page limit

// A module's numbers, by its machine ID, as a build gives a machine new to its
// layout (tooling/machines.py number_layout): engines from the relocated table
// above stock's "none" (33), pages above stock's "none" (81), each from the
// ID's own place; the last page stays reserved for the SDK overlay screen.
// Held: the fixed machines' numbers (FIXED_NUMBERS), every number this
// firmware's own machines hold (an effect's number also as an engine), and
// earlier modules'. Profile values: profiles/opxy-1.2.1.json "identities".
const u32 engine_first = 34, engine_end = 256, page_first = 82, page_end = 256, page_reserved = 255;
struct Fixed { const char* id; u32 engine, page; };
const Fixed fixed[] = {{"fm2op", 34, 82}, {"macroosc", 26, 83}, {"ringmod", 1, 21}, {"timestretch", 35, 7}};

char current_id[16];           // the module whose init runs now (sdk_module_current)

struct Entry {
    const ModuleSynth* d;
    u32 engine, page;              // given at registration
    Record record;
    Screen screen;
    synth::Pool* pool;             // audio role
    // GUI role: the screen object (stock Base, then the arrays below) and the
    // machine's display state and frame.
    unsigned char* object;
    u32 object_bytes;
    u32 at_nodes, at_texts, at_shown, at_palette, at_delta, at_geometry, at_keys;
    unsigned char* state;
    unsigned char* frame;
    u32 activation;
    bool screen_ok, engine_ok;
};
Entry entries[entries_max];
u32 count;
bool open_for_registration;

u32 round4(u32 v) { return (v + 3) & ~3u; }
u32 at_least_one(u32 v) { return v ? v : 1; }
template <class T> T* field(unsigned char* base, u32 at) { return reinterpret_cast<T*>(base + at); }
const char* as_symbol(u32 value) { return reinterpret_cast<const char*>(static_cast<Address>(value)); }

Entry* entry_of_handle(const void* handle) {
    for (u32 i = 0; i < count; ++i)
        if (entries[i].pool && synth::owns(*entries[i].pool, handle)) return &entries[i];
    return nullptr;
}
Entry* entry_of_object(const void* object) {
    for (u32 i = 0; i < count; ++i)
        if (entries[i].object == object) return &entries[i];
    return nullptr;
}

// ---- voice: one method table for every module synth (voice.cpp.in's layout)
int voice_destroy(void* self) { Entry* e = entry_of_handle(self); return e ? synth::destroy(*e->pool, self) : 0; }
int voice_start(void* self) { Entry* e = entry_of_handle(self); return e ? synth::start(*e->pool, self) : 0; }
int voice_render(void* self, i32* output, const i32* parameters) {
    Entry* e = entry_of_handle(self);
    if (e) return synth::render(*e->pool, self, output, parameters);
    for (u32 i = 0; i < block_frames; ++i) output[i] = 0;
    return 0;
}
int zero() { return 0; }
void noop() {}
const void* const voice_vtable[11] = {
    reinterpret_cast<const void*>(&voice_destroy), reinterpret_cast<const void*>(&voice_destroy),
    reinterpret_cast<const void*>(&voice_render), reinterpret_cast<const void*>(&noop),
    reinterpret_cast<const void*>(&voice_start), reinterpret_cast<const void*>(&noop),
    reinterpret_cast<const void*>(&zero), reinterpret_cast<const void*>(&noop),
    reinterpret_cast<const void*>(&zero), reinterpret_cast<const void*>(&zero),
    reinterpret_cast<const void*>(kVoiceDefaultMethod)
};
const u32* increments() { return reinterpret_cast<const u32*>(static_cast<Address>(kPitchIncrements)); }

// ---- screen: screen_drawing.hpp's Drawing<Machine> with runtime counts
using screen::Base;
using screen::Leaf;
using screen::PathNode;

u32 path_bytes(const ModuleSynth& d) { return 4 + 8 * d.points; }
u32 path_key(Entry& e, u32 p) { return *field<u32>(e.frame, p * path_bytes(*e.d)); }
const i32* path_points(Entry& e, u32 p) { return field<i32>(e.frame, p * path_bytes(*e.d) + 4); }
const draw::Node& frame_node(Entry& e, u32 i) { return *field<draw::Node>(e.frame, e.d->nodes_offset + i * sizeof(draw::Node)); }
const draw::Text& frame_text(Entry& e, u32 i) { return *field<draw::Text>(e.frame, e.d->texts_offset + i * sizeof(draw::Text)); }
const char* frame_header(Entry& e, u32 i) { return field<char>(e.frame, e.d->header_offset + i * draw::header_capacity); }

PathNode** nodes_of(Entry& e) { return field<PathNode*>(e.object, e.at_nodes); }
Leaf** texts_of(Entry& e) { return field<Leaf*>(e.object, e.at_texts); }
char* shown_of(Entry& e, u32 i) { return field<char>(e.object, e.at_shown + i * draw::text_capacity); }
u32* palette_of(Entry& e) { return field<u32>(e.object, e.at_palette); }
i32& delta_of(Entry& e) { return *field<i32>(e.object, e.at_delta); }
u32* geometry_of(Entry& e) { return field<u32>(e.object, e.at_geometry); }
u32* keys_of(Entry& e) { return field<u32>(e.object, e.at_keys); }

void clear(Entry& e) {
    u32* words = reinterpret_cast<u32*>(e.object);
    for (u32 i = sizeof(Base) / 4; i < e.object_bytes / 4; ++i) words[i] = 0;
}

u32 geometry(const ModuleSynth& d, void* raw) {
    const PathNode* node = screen::path_node(raw);
    if (!node) return 0;
    const screen::Vector& v = node->commands;
    if (!v.begin || v.begin & 3 || v.end < v.begin || v.capacity < v.end) return 0;
    if (v.end - v.begin != 32 * (d.points + 1) || v.capacity - v.begin > 4096) return 0;
    const screen::Command* command = reinterpret_cast<const screen::Command*>(v.begin);
    for (u32 i = 0; i < d.points; ++i)
        if (command[i].type != (i ? 2 : 1)) return 0;
    return command[d.points].type == 5 ? v.begin : 0;
}

void screen_init(Base* object) {
    Entry* found = entry_of_object(object);
    if (!found) return;
    Entry& e = *found;
    const ModuleSynth& d = *e.d;
    clear(e);
    e.activation = 0;
    d.display_reset(e.state, e.frame);
    if (!object->svg) return;
    screen::bind_controls(object);
    for (u32 i = 0; i < d.nodes; ++i) nodes_of(e)[i] = screen::path_node(screen::find(object, d.ids[i]));
    for (u32 i = 0; i < d.texts; ++i) {
        texts_of(e)[i] = screen::text_node(screen::find(object, d.ids[d.nodes + i]));
        shown_of(e, i)[0] = 1;  // apply every slot on the first frame
    }
    for (u32 i = 0; i < d.palette; ++i)
        palette_of(e)[i] = screen::palette_ids(screen::find(object, d.ids[d.nodes + d.texts + i]), object->svg);
    for (u32 i = 0; i < d.paths; ++i) geometry_of(e)[i] = geometry(d, nodes_of(e)[i]);
}

void apply_path(Entry& e, u32 p) {
    if (path_key(e, p) == keys_of(e)[p]) return;
    const u32 begin = geometry(*e.d, nodes_of(e)[p]);
    if (!begin || begin != geometry_of(e)[p]) return;
    screen::Command* command = reinterpret_cast<screen::Command*>(begin);
    const i32* points = path_points(e, p);
    for (u32 i = 0; i < e.d->points; ++i) { command[i].x = points[2 * i]; command[i].y = points[2 * i + 1]; }
    screen::invalidate(nodes_of(e)[p]);
    keys_of(e)[p] = path_key(e, p);
}

void apply_node(Entry& e, u32 i) {
    const draw::Node& node = frame_node(e, i);
    PathNode* leaf = nodes_of(e)[i];
    if (node.paint != draw::keep) screen::paint(leaf, palette_of(e)[node.shade], node.paint == draw::fill_and_stroke);
    if (!leaf) return;
    const i32 a = draw::floor_q10(node.scale * node.cos), b = draw::floor_q10(node.scale * node.sin);
    const u32 at[6] = {0, 1, 3, 4, 6, 7};
    const i32 value[6] = {a, b, -b, a, node.x, node.y};
    screen::set_words(&leaf->leaf, at, value);
}

void apply_text(Entry& e, u32 i) {
    const draw::Text& text = frame_text(e, i);
    Leaf* leaf = texts_of(e)[i];
    if (!leaf) return;
    const u32 at[6] = {0, 1, 3, 4, 6, 7};
    const i32 value[6] = {text.scale, 0, 0, text.scale, text.x, text.y};
    screen::set_words(leaf, at, value);
    char* shown = shown_of(e, i);
    u32 n = 0;
    while (n < draw::text_capacity && shown[n] == text.chars[n] && text.chars[n]) ++n;
    if (n < draw::text_capacity && shown[n] == text.chars[n]) return;
    for (n = 0; n < draw::text_capacity; ++n) shown[n] = text.chars[n];
    screen::set_text(leaf, shown);
}

void animate(Entry& e, Base* object, const screen::Controls& controls) {
    if (!controls.valid) return;
    const ModuleSynth& d = *e.d;
    const u32 activation = sdk_screen_activation(object->page, controls.track);
    if (activation && activation != e.activation) {
        d.display_reset(e.state, e.frame);
        for (u32 p = 0; p < d.paths; ++p) keys_of(e)[p] = 0;
        e.activation = activation;
    }
    draw::Input input = {};
    for (u32 i = 0; i < page_controls; ++i) { input.parameters[i] = controls.values[i]; input.shown[i] = controls.shown[i]; }
    for (u32 i = 0; i < parameter_slots; ++i) input.slots[i] = controls.slots[i];
    input.model = controls.model;
    input.shift = controls.choosing ? 1 : controls.shift;
    for (u32 i = 0; i < 4; ++i) input.choices[i] = controls.choices[i];
    input.elapsed = delta_of(e);
    input.track = controls.track;
    input.note_serial = controls.track < 8 ? screen::note_serial(controls.track) : 0;
    sdk_display_note_activity(controls.track, &input);
    d.display_render(e.state, &input, e.frame);
    if (!d.display_validate(e.frame)) return;
    for (u32 p = 0; p < d.paths; ++p) apply_path(e, p);
    for (u32 i = 0; i < d.nodes; ++i) apply_node(e, i);
    for (u32 i = 0; i < d.texts; ++i) apply_text(e, i);
    for (u32 i = 0; i < page_controls; ++i)
        if (!controls.choosing && frame_header(e, i)[0])
            if (Leaf* value = screen::text_node(object->values[i])) screen::set_text(value, frame_header(e, i));
}

void screen_frame(Base* object, i32 elapsed) {
    Entry* e = entry_of_object(object);
    if (!e) return;
    delta_of(*e) = elapsed;
    screen::Controls controls;
    if (!screen::refresh_controls(object, controls)) return;
    animate(*e, object, controls);
    screen::draw(object);
}

void screen_destroy(Base* object) {
    screen::base_destroy(object);
    object->loader = 0;
    if (Entry* e = entry_of_object(object)) clear(*e);
}

struct ScreenTable {
    void (*destroy)(Base*);
    void (*destroy_delete)(Base*);
    const void* stock8;
    void (*frame)(Base*, i32);
    void (*init)(Base*);
    const void* stock20;
};
const ScreenTable screen_vtable = {screen_destroy, screen_destroy, reinterpret_cast<const void*>(0x8d51fcd4),
                                   screen_frame, screen_init, reinterpret_cast<const void*>(0x8c0867f0)};

char* screen_construct(char* state, u32 page) {
    Entry* e = entry_of_object(state);
    if (!e || page < 1 || page >= page_limit) return nullptr;
    Base* object = reinterpret_cast<Base*>(state);
    screen::base_construct(object, page, e->d->asset);
    object->vtable = &screen_vtable;
    return state;
}

// ---- registration
bool name_ok(const char* name) {
    u32 n = 0;
    while (name && name[n] && n < 16) ++n;
    return n >= 1 && n <= 15;
}

// The frame's members where draw::Frame puts them, so the renderer reads the
// machine's frame correctly; anything else is refused.
bool shape_ok(const ModuleSynth& d) {
    const u32 path = 4 + 8 * d.points, nodes = at_least_one(d.paths) * path;
    const u32 texts = nodes + d.nodes * sizeof(draw::Node), header = texts + at_least_one(d.texts) * sizeof(draw::Text);
    return d.points && d.points <= 1024 && d.paths <= 64 && d.nodes <= 64 && d.texts <= 16 && d.palette <= 32 &&
        d.nodes_offset == nodes && d.texts_offset == texts && d.header_offset == header &&
        d.frame_bytes == header + page_controls * draw::header_capacity && d.ids && d.asset &&
        d.display_reset && d.display_render && d.display_validate && d.display_bytes <= 0x10000 &&
        d.display_alignment && d.display_alignment <= 8 && !(d.display_alignment & (d.display_alignment - 1));
}

bool description_ok(const ModuleSynth* d) {
    return d && d->size >= sizeof(ModuleSynth) && name_ok(d->name) && d->factory && d->parameter_name && shape_ok(*d);
}

// ---- numbers
u32 symbol_value(const char* symbol) { return static_cast<u32>(reinterpret_cast<Address>(symbol)); }
bool equal_id(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
bool built_in(const char* id) {
    for (const CompanionMachine* m = sdk_companion_machines; m->id; ++m)
        if (symbol_value(m->number) && equal_id(m->id, id)) return true;
    return false;
}
bool engine_held(void*, u32 n) {
    for (const Fixed& f : fixed) if (f.engine == n) return true;
    for (const CompanionMachine* m = sdk_companion_machines; m->id; ++m) if (symbol_value(m->number) == n) return true;
    for (u32 i = 0; i < count; ++i) if (entries[i].engine == n) return true;
    return false;
}
bool page_held(void*, u32 n) {
    if (n == page_reserved) return true;
    for (const Fixed& f : fixed) if (f.page == n) return true;
    for (const CompanionMachine* m = sdk_companion_machines; m->id; ++m)
        if (symbol_value(m->number) && symbol_value(m->page) == n) return true;
    for (u32 i = 0; i < count; ++i) if (entries[i].page == n) return true;
    return false;
}
// A fixed machine as a module keeps its fixed numbers; any other gets its preferred free ones.
bool number(const char* id, u32& engine, u32& page) {
    for (const Fixed& f : fixed)
        if (equal_id(f.id, id)) {
            engine = f.engine; page = f.page;
            for (u32 i = 0; i < count; ++i) if (entries[i].engine == engine || entries[i].page == page) return false;
            return true;
        }
    engine = modules::preferred_number(id, engine_first, engine_end, engine_held, nullptr);
    page = modules::preferred_number(id, page_first, page_end, page_held, nullptr);
    return engine && page;
}

// Sized and laid out like Drawing<Machine>::Object after the stock Base.
void lay_out(Entry& e) {
    const ModuleSynth& d = *e.d;
    u32 at = sizeof(Base);
    e.at_nodes = at; at += 4 * d.nodes;
    e.at_texts = at; at += 4 * at_least_one(d.texts);
    e.at_shown = at; at += round4(draw::text_capacity * at_least_one(d.texts));
    e.at_palette = at; at += 4 * d.palette;
    e.at_delta = at; at += 4;
    e.at_geometry = at; at += 4 * at_least_one(d.paths);
    e.at_keys = at; at += 4 * at_least_one(d.paths);
    e.object_bytes = at;
}

unsigned char* zeroed(u32 bytes, u32 alignment) {
    unsigned char* block = sdk_module_allocate(bytes + alignment);
    if (!block) return nullptr;
    for (u32 i = 0; i < bytes + alignment; ++i) block[i] = 0;
    return reinterpret_cast<unsigned char*>((reinterpret_cast<Address>(block) + alignment - 1) & ~static_cast<Address>(alignment - 1));
}
}

int sdk_module_synth_register(const ModuleSynth* d) {
    if (!open_for_registration) return module_late;
    if (!description_ok(d)) return module_invalid;
    if (count == entries_max) return module_full;
    // One synth per module, known by the module's machine ID; never one this firmware holds itself.
    if (!current_id[0] || built_in(current_id)) return module_taken;
    for (u32 i = 0; i < count; ++i) if (entries[i].d == d) return module_taken;
    u32 engine = 0, page = 0;
    if (!number(current_id, engine, page)) return module_full;
    Entry& e = entries[count];
    e = Entry{};
    e.d = d;
    e.engine = engine;
    e.page = page;
    e.record = Record{reinterpret_cast<const char*>(d->factory), reinterpret_cast<const char*>(d->parameter_name),
                      d->name, as_symbol(engine), as_symbol(page), 0};
    current_id[0] = 0;  // one synth per module
    if (stock_gui_role()) {
        // Registered with the screen at the end of startup, like a built-in synth.
        lay_out(e);
        e.object = zeroed(e.object_bytes, 8);
        e.state = zeroed(at_least_one(d->display_bytes), d->display_alignment);
        e.frame = zeroed(d->frame_bytes, 4);
        if (!e.object || !e.state || !e.frame) return module_memory;
        e.screen = Screen{reinterpret_cast<char*>(e.object), e.object_bytes,
                          reinterpret_cast<const char*>(&screen_vtable), d->asset, screen_construct};
        return static_cast<int>(count++);
    }
    synth::Machine* machine = reinterpret_cast<synth::Machine*>(zeroed(sizeof(synth::Machine), 4));
    e.pool = reinterpret_cast<synth::Pool*>(zeroed(sizeof(synth::Pool), 4));
    if (!machine || !e.pool) return module_memory;
    *machine = synth::Machine{d->voice_bytes, d->voice_alignment, d->shared_bytes, d->capacity,
                              d->initialize, d->start, d->destroy, d->render};
    unsigned char* storage = zeroed(synth::storage_bytes(*machine), 4);
    if (!storage || !synth::setup(*e.pool, machine, voice_vtable, sdk_voice_ticks, increments(), storage))
        return module_memory;
    // The audio role registers the engine now, as built-in synths do in sdk_startup_synth.
    const int added = sdk_register_engine(&e.record);
    if (added != 1 && added != 2) return module_taken;
    e.engine_ok = true;
    return static_cast<int>(count++);
}

void* sdk_module_synth_voice(int handle, u32 index) {
    if (handle < 0 || static_cast<u32>(handle) >= count || !entries[handle].pool) return nullptr;
    return synth::factory(*entries[handle].pool, index);
}

void sdk_module_registration(bool open) { open_for_registration = open; }

void sdk_module_current(const char* id) {
    u32 n = 0;
    for (; id && id[n] && n + 1 < sizeof current_id; ++n)
        current_id[n] = id[n] >= 'A' && id[n] <= 'Z' ? static_cast<char>(id[n] + 32) : id[n];
    current_id[id && id[n] ? 0 : n] = 0;  // too long: no ID at all
}

void sdk_module_screens() {
    for (u32 i = 0; i < count; ++i) {
        Entry& e = entries[i];
        if (!e.object) continue;
        e.screen_ok = sdk_register_screen(&e.record, &e.screen) == 1;
        if (e.screen_ok) { const int added = sdk_register_engine(&e.record); e.engine_ok = added == 1 || added == 2; }
    }
}

int sdk_module_catalog(void* catalog, const void* name, u32 id, u32* result) {
    for (u32 i = 0; i < count; ++i) {
        Entry& e = entries[i];
        if (!sdk_name_matches(&e.record, name)) continue;
        *result = id == e.engine && sdk_engine_ready(&e.record, e.object ? &e.screen : nullptr) == 1 ?
            stock_catalog_insert(catalog, name, id) : 0;
        return 1;
    }
    return 0;
}

int sdk_module_screen_owned(u32 page) {
    for (u32 i = 0; i < count; ++i)
        if (entries[i].object && entries[i].page == page) return sdk_engine_ready(&entries[i].record, &entries[i].screen) == 1;
    return 0;
}

// ---- content: artwork and catalog entries from the modules
namespace {
bool equal(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
// path is "content/display/<asset>" for a registered module synth's asset.
const ModuleSynth* asset_owner(const char* path) {
    const char* prefix = "content/display/";
    if (!path) return nullptr;
    const char* p = path;
    while (*prefix) if (*p++ != *prefix++) return nullptr;
    for (u32 i = 0; i < count; ++i)
        if (entries[i].d->svg && entries[i].d->svg_bytes && equal(p, entries[i].d->asset)) return entries[i].d;
    return nullptr;
}
u32 length(const char* s) { u32 n = 0; while (s && s[n]) ++n; return n; }
}
namespace {
u32 engine_types_end(const unsigned char* text, u32 bytes) { return modules::array_end(text, bytes, "\"engine_types\""); }
}

// Stock's asset load (0x8d5f2400) at its call site 8d56d6b6. A module's
// artwork is parsed from its text with stock's own parser (0x8d5f2160), from a
// heap copy since the parser writes into its input; every other path is stock's.
void* sdk_module_asset_load(const char* path) {
    const ModuleSynth* d = asset_owner(path);
    if (!d) return stock<void*(const char*)>(0x8d5f2400)(path);
    unsigned char* text = sdk_module_allocate(d->svg_bytes + 1);
    if (!text) return stock<void*(const char*)>(0x8d5f2400)(path);
    for (u32 i = 0; i < d->svg_bytes; ++i) text[i] = static_cast<unsigned char>(d->svg[i]);
    text[d->svg_bytes] = 0;
    void* document = stock<void*(char*)>(0x8d5f2160)(reinterpret_cast<char*>(text));
    sdk_module_release(text);
    return document;
}

// Stock's factory catalog read (0x8c042490(path, json)) at 8d4e06e4. With
// module synths, the file's text gets their entries at the end of
// "engine_types" (as the build appends built-in machines'), and stock's JSON
// parser reads the result into the caller's value, as collection_resource_stock
// does for a whole collection. Anything unexpected: stock reads the file alone.
bool sdk_module_catalog_load(const char* path, void* out) {
    sdk_module_load();  // the catalog may be read before startup reaches the loader
    const auto original = stock<bool(const char*, void*)>(0x8c042490);
    u32 extra = 0;
    for (u32 i = 0; i < count; ++i) if (entries[i].d->catalog_entry) extra += length(entries[i].d->catalog_entry) + 1;
    if (!extra || !equal(path, "content/op1_factory.json")) return original(path, out);
    u32 bytes = 0;
    unsigned char* file = sdk_module_read_file(path, bytes);
    if (!file) return original(path, out);
    const u32 at = engine_types_end(file, bytes);
    unsigned char* text = at ? sdk_module_allocate(bytes + extra + 1) : nullptr;
    if (!text) { sdk_module_release(file); return original(path, out); }
    u32 n = 0;
    for (u32 i = 0; i < at; ++i) text[n++] = file[i];
    for (u32 i = 0; i < count; ++i)
        if (const char* entry = entries[i].d->catalog_entry) {
            text[n++] = ',';
            for (const char* c = entry; *c; ++c) text[n++] = static_cast<unsigned char>(*c);
        }
    for (u32 i = at; i < bytes; ++i) text[n++] = file[i];
    text[n] = 0;
    sdk_module_release(file);
    // collection_resource_stock.cpp's parse: a Json value, a reader, the parse, then copy-assign into out.
    unsigned char* value = sdk_module_allocate(12);
    bool parsed = false;
    if (value) {
        alignas(4) unsigned char reader[120];
        stock<void(void*, u32)>(0x8c0555c0)(value, 0);
        stock<void(void*)>(0x8c050de8)(reader);
        parsed = stock<bool(void*, const char*, const char*, void*, bool)>(0x8c052230)(
            reader, reinterpret_cast<const char*>(text), reinterpret_cast<const char*>(text + n), value, false);
        stock<void(void*)>(0x8c042420)(reader);
        if (parsed) stock<void*(void*, const void*)>(0x8c054098)(out, value);
        stock<void(void*)>(0x8c053fc0)(value);
        sdk_module_release(value);
    }
    sdk_module_release(text);
    return parsed ? true : original(path, out);
}

int sdk_module_engine_installed(const void* name) {
    for (u32 i = 0; i < count; ++i)
        if (sdk_name_matches(&entries[i].record, name)) return 1;
    return 0;
}
