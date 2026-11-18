// Sample Studio's capability, main board (opxy/sample_studio.hpp, docs/sample-studio.md): the
// library controller's and browser screen's entry branches (sample_studio_hooks.s) handed to the
// extension's editor, and the services it uses, each a stock call or global of OS 1.2.1.
#include <opxy/sample_studio.hpp>
#include <opxy/sample_engine.hpp>
#include "barrier.hpp"
#include "retained_drawing.hpp"
#include "stock.hpp"
#include "stock_sample.hpp"

using opxy::i32;
using opxy::u32;
using opxy::platform::byte;
using opxy::platform::stock;
using opxy::platform::word;
namespace studio = opxy::studio;

extern "C" {
u32 sdk_sample_studio_press(unsigned char* controller, const u32* event) __asm__("sdk_sample_studio_press");
u32 sdk_sample_studio_shift_press(unsigned char* controller, const u32* event) __asm__("sdk_sample_studio_shift_press");
u32 sdk_sample_studio_release(unsigned char* controller, u32 key) __asm__("sdk_sample_studio_release");
void sdk_sample_studio_turn(unsigned char* controller, u32 position, const i32* event) __asm__("sdk_sample_studio_turn");
void sdk_sample_studio_frame(void* screen, i32 elapsed) __asm__("sdk_sample_studio_frame");
u32 sdk_sample_studio_stock_press(unsigned char*, const u32*) __asm__("sdk_sample_studio_stock_press");
u32 sdk_sample_studio_stock_shift_press(unsigned char*, const u32*) __asm__("sdk_sample_studio_stock_shift_press");
u32 sdk_sample_studio_stock_release(unsigned char*, u32) __asm__("sdk_sample_studio_stock_release");
void sdk_sample_studio_stock_turn(unsigned char*, u32, const i32*) __asm__("sdk_sample_studio_stock_turn");
void sdk_sample_studio_stock_frame(void*, i32) __asm__("sdk_sample_studio_stock_frame");
// The extension's editor (generated beside its object, tooling/extensions.py).
const opxy::studio::Editor* sdk_sample_studio_editor() __asm__("sdk_sample_studio_editor");
u32 sdk_sample_cache_stock_hash(const char*) __asm__("sdk_sample_cache_stock_hash");
void sdk_sample_cache_sample_release(u32) __asm__("sdk_sample_cache_sample_release");
// The engine the sample pages treat a track as (drum_controls_stock.cpp): an owned page-2 or page-7
// machine counts as the sampler or multisampler.
u32 sdk_sample_ui_engine(u32) __asm__("sdk_sample_ui_engine");
}

namespace {
// The library controller (firmware notes §2): its object, the renaming and delete-prompt flags in it,
// and the controller stack (five pointers, the count at +0x14), whose top is the library while it shows.
const u32 kLibrary = 0x0801f2fc, kRenaming = kLibrary + 0x50, kDeletePrompt = kLibrary + 0x5c;
const u32 kStack = 0x0801ed24, kStackCount = 0x14;
// Stock's region table counter, bumped before the table goes to the voices (8d58938c), as its slicer does.
const u32 kRegionChanges = 0x840a0530;
// Stock's "samples changed" byte: its refresh (8d593594) gathers the keys' files and clears it.
const u32 kSamplesChanged = 0x840a052d;
// The selected track's held notes, one byte a note (non-zero while held).
const u32 kNoteMap = 0x86bda258;
// Stock's sample cache: the manager (its lock at +4), the map and a node's fields.
const u32 kSampleManager = 0x84048894, kCacheMap = 0x08039f24;
const u32 kNodeObject = 0x14, kNodeCount = 0x18, kNodeUnwanted = 0x1c, kNodeLoaded = 0x1d;
// Stock's heap (mutex, mspace).
const u32 kHeap = 0x08036aa4;
}  // namespace

namespace opxy { namespace studio {
// ---------------------------------------------------------------- both boards
u32 now_us() {
    u32 t[2] = {0, 0};
    stock<void(u32*)>(0x8d5be3a0)(t);
    return t[0];
}
u32 now_ms() {
    u32 time[2] = {0, 0};
    stock<void(u32*)>(0x8d5be3a0)(time);
    return time[0] / 1000u + time[1] * 4294967u;
}
void barrier() { opxy::platform::barrier(); }
u32 cycles() {
    u32 c;
    __asm__ volatile("%0 = CYCLES;" : "=d"(c));
    return c;
}
void log(u32 line, const char* format, i32 a, i32 b, i32 c, i32 d, i32 e, i32 f, i32 g, i32 h, i32 i, i32 j, i32 k,
         i32 l) {
    stock<void(u32, const char*, u32, const char*, ...)>(0x8c042efc)(3, "sample_studio/editor.cpp", line, format, a, b, c, d, e,
                                                                     f, g, h, i, j, k, l);
}
void log_audio(u32 line, const char* format, i32 a, i32 b, i32 c, i32 d, i32 e, i32 f) {
    stock<void(u32, const char*, u32, const char*, ...)>(0x8c042efc)(3, "sample_studio/player.cpp", line, format, a, b,
                                                                     c, d, e, f);
}
void post(void (*job)(u32), void (*done)(u32), u32 arg) {
    stock<void(u32, u32, void (*)(u32), void (*)(u32))>(0x8d4dd728)(arg, 0, job, done);
}
File* file_open(const char* path, const char* mode) { return stock<File*(const char*, const char*)>(0x8fed0348)(path, mode); }
u32 file_read(void* to, u32 size, u32 count, File* file) {
    return stock<u32(void*, u32, u32, File*)>(0x8fed05bc)(to, size, count, file);
}
u32 file_write(const void* from, u32 size, u32 count, File* file) {
    return stock<u32(const void*, u32, u32, File*)>(0x8fed0780)(from, size, count, file);
}
void file_close(File* file) { stock<int(File*)>(0x8fed00ae)(file); }
bool rename_file(const char* from, const char* to) { return stock<u32(const char*, const char*)>(0x8c040bd8)(from, to) != 0; }
void remove_file(const char* path) { stock<u32(const char*)>(0x8c040c24)(path); }
void make_folder(const char* path) { stock<i32(const char*, u32)>(0x8c0bd5bc)(path, 0); }
bool file_exists(const char* path) {
    File* file = file_open(path, "rb");
    if (!file) return false;
    file_close(file);
    return true;
}

// ---------------------------------------------------------------- main board
void* allocate(u32 bytes) {
    stock<void(u32, i32)>(0x8c03e6f4)(word(kHeap), -1);
    void* block = stock<void*(u32, u32)>(0x8c048788)(word(kHeap + 4), bytes);
    stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(word(kHeap), 0, 0, 0);
    return block;
}
void release(void* block) { stock<void(void*)>(0x8d4d2910)(block); }
namespace {
char popup_text[32];
}
void popup(const char* text) {
    u32 n = 0;
    for (; n + 1 < sizeof popup_text && text[n]; ++n) popup_text[n] = text[n];
    popup_text[n] = 0;
    stock<void(const char*, u32, u32, u32)>(0x8d549168)(popup_text, 1, 0, 0x3f000000u);
}
bool library_on_top() {
    const u32 count = word(kStack + kStackCount);
    return count >= 1 && count <= 5 && word(kStack + 4 * (count - 1)) == kLibrary;
}
bool library_busy() { return byte(kRenaming) || byte(kDeletePrompt); }
u32 selected_track() { return byte(opxy::platform::kSelectedTrack); }
u32 sample_engine(u32 track) { return sdk_sample_ui_engine(track); }
u32 selected_key(u32 track) { return opxy::platform::drum_selected_key(track); }
bool shift_held() { return opxy::platform::shift_held(); }
u32 project_bpm_x10() { return opxy::platform::project_bpm_x10(); }
u32 track_engine(u32 track) { return opxy::platform::track_engine(track); }
namespace {
// Stock's string: the text inline at +4 up to 15 characters, else a pointer there; size, capacity.
struct StockString {
    u32 head;
    union {
        char text[16];
        char* data;
    };
    u32 size, capacity;
};
static_assert(sizeof(StockString) == 0x1c, "stock's string: the text at +4, its capacity at +0x18");
}
// The highlighted file's full path ("" for a folder or nothing), as stock's press reads it:
// 8c0a84b4(&string, 8c0ad5dc()), the library tree's highlighted entry.
bool highlighted(char* out, u32 size) {
    StockString text;
    __builtin_memset(&text, 0, sizeof text);
    const u32 tree = stock<u32()>(0x8c0ad5dc)();
    stock<void(StockString*, u32)>(0x8c0a84b4)(&text, tree);
    const char* chars = text.capacity >= 16 ? text.data : text.text;
    u32 n = 0;
    for (; chars && n + 1 < size && n < text.size && chars[n]; ++n) out[n] = chars[n];
    out[n] = 0;
    if (text.capacity >= 16 && text.data) stock<void(void*)>(0x8d4d2910)(text.data);
    return n > 0;
}
// The last highlighted file stock keeps (96 bytes).
bool highlight_seen() { return byte(0x84039408) != 0; }
void stop_audition() { stock<void()>(0x8c0a1454)(); }
// Stock's library caches its listing and lists a folder again only after a change clears its flag,
// as stock's own preset save does (8c0b4fce: 8c0a7e70(8c0ad5dc())); then the library's own listing
// step, as entering the library queues it (8c0a2ee4: 8d4dd728(0, 0, 8c0a349c, 0)).
void relist_library() {
    const u32 tree = stock<u32()>(0x8c0ad5dc)();
    stock<void(u32)>(0x8c0a7e70)(tree);
    stock<void(u32, u32, u32, u32)>(0x8d4dd728)(0, 0, 0x8c0a349cu, 0);
}
// Stock's preset index, built at boot (8d4eefec(1)); its preset rename and delete build it again
// with 8d4eefec(0) on the GUI side. It reads the card: about 1.4 s.
void rescan_presets() { stock<void(u32)>(0x8d4eefec)(0); }
u32 note_serial(u32 track) { return stock<u32(u32)>(0x8d4d6c08)(track); }
bool note_held(u32 track, u32 note) {
    return note < 128 && track < 8 && reinterpret_cast<const volatile unsigned char*>(kNoteMap + track * 128)[note] != 0;
}
unsigned char* key_entry(u32 track, u32 key) { return stock<unsigned char*(u32, u32)>(0x8d589370)(track, key); }
const char* entry_path(const unsigned char* entry) { return reinterpret_cast<const char*>(entry + 0x20); }
i32 entry_frames(const unsigned char* entry) {
    return static_cast<i32>(opxy::platform::pad_word(entry, opxy::platform::pad_record::kFrames));
}
SampleRegion entry_region(const unsigned char* entry) { return opxy::platform::sample_gui_region(entry); }
i32 entry_level(const unsigned char* entry) {
    return static_cast<signed char>(opxy::platform::pad_byte(entry, opxy::platform::pad_record::kLevel));
}
bool entry_reversed(const unsigned char* entry) {
    using namespace opxy::platform::pad_record;
    return (entry[kFlags] & kReverse) != 0;
}
u32 entry_root(const unsigned char* entry) { return entry[opxy::platform::pad_record::kRoot]; }
i32 entry_fine(const unsigned char* entry) { return static_cast<signed char>(entry[opxy::platform::pad_record::kFine]); }
void set_entry_tuning(unsigned char* entry, u32 root, i32 fine) {
    entry[opxy::platform::pad_record::kRoot] = static_cast<unsigned char>(root);
    entry[opxy::platform::pad_record::kFine] = static_cast<unsigned char>(fine);
}
void set_entry_whole(unsigned char* entry, u32 frames) {
    using namespace opxy::platform::pad_record;
    u32* w = reinterpret_cast<u32*>(entry);
    w[kFrames] = w[kEnd] = frames;
    w[kStart] = 0;
}
// A file on a key as the library loads one (8d5893bc names it; a drum key takes its length with
// 8d589ac0, which keeps the key's root and fine: an empty key's are 0, 0, which the voices read as 60
// semitones up, so it gets 60; a sampler or multisampler key takes root and length with 8d589b8c).
bool put_file(u32 track, u32 engine, u32 key, const char* path, u32 frames, u32 root) {
    if (!stock<u32(u32, u32, const char*)>(0x8d5893bc)(track, key, path)) return false;
    if (engine == engine_drum) {
        stock<void(u32, u32, u32, u32)>(0x8d589ac0)(track, key, frames, 0);
        unsigned char* entry = key_entry(track, key);
        if (entry && !entry_root(entry) && !entry_fine(entry)) set_entry_tuning(entry, 60, 0);
    } else {
        stock<void(u32, u32, u32, u32, u32, u32, u32)>(0x8d589b8c)(track, key, root, 0xffffffffu, 0xffffffffu, frames, 0);
    }
    return true;
}
void send_regions(u32 track, bool changed) {
    *reinterpret_cast<volatile u32*>(kRegionChanges) = word(kRegionChanges) + 1;
    stock<void(u32)>(0x8d58938c)(track);
    if (changed) *reinterpret_cast<volatile unsigned char*>(kSamplesChanged) = 1;
}
void samples_changed() { *reinterpret_cast<volatile unsigned char*>(kSamplesChanged) = 1; }
bool samples_gathered() { return !byte(kSamplesChanged); }
u32 sample_key(const char* path) { return sdk_sample_cache_stock_hash(path); }
// A sample from stock's cache, wanted by stock or not (its acquire refuses unwanted ones while the
// library is open). Worker side.
Borrowed borrow(u32 key) {
    Borrowed b = {nullptr, 0, false};
    const u32 lock = word(kSampleManager) + 4;
    stock<void(u32)>(0x8d5be032)(lock);
    u32 wanted_key = key;
    unsigned char* node = stock<unsigned char*(u32, u32*)>(0x8d509824)(kCacheMap, &wanted_key);
    if (reinterpret_cast<u32>(node) != word(kCacheMap + 4)) {
        b.state = 1;
        b.unwanted = node[kNodeUnwanted] != 0;
        const u32 object = *reinterpret_cast<volatile u32*>(node + kNodeObject);
        if (node[kNodeLoaded] && object && word(object) && word(object + 0x30)) {
            *reinterpret_cast<volatile u32*>(node + kNodeCount) += 1;
            b.object = reinterpret_cast<const void*>(object);
            b.state = 2;
        }
    }
    stock<void(u32)>(0x8d5be060)(lock);
    return b;
}
void release_sample(u32 key) { sdk_sample_cache_sample_release(key); }
SampleView sample_view(const void* object) { return opxy::platform::stock_sample_view(object); }
// Main board to audio board: stock's B3 parameter send (8d4dc84c); IDs 138..152 are the player's.
void send(u32 channel, i32 value) {
    if (channel < 15) stock<void(u32, i32)>(0x8d4dc84c)(138 + channel, value);
}
}}  // namespace opxy::studio

// ---------------------------------------------------------------- the surface
namespace {
using Renderer = opxy::screen::RetainedDrawing<studio::Surface>;
using Object = Renderer::Object;
Object object;
bool constructed, bound;
const char* const* bound_ids;
u32 bound_paths, bound_nodes, bound_texts, bound_palette;
// The artwork's IDs in the renderer's order, unbound slots past each part's count.
const char* ids[studio::surface_nodes + studio::surface_texts + studio::surface_palette];
const char kNone[] = "";
const char kAsset[] = "sample_studio.svg";

void init(Object* o) {
    opxy::screen::clear(o);
    if (!o->base.svg || !bound_ids) return;
    for (u32 i = 0; i < studio::surface_nodes; ++i) ids[i] = i < bound_nodes ? bound_ids[i] : kNone;
    for (u32 i = 0; i < studio::surface_texts; ++i)
        ids[studio::surface_nodes + i] = i < bound_texts ? bound_ids[bound_nodes + i] : kNone;
    for (u32 i = 0; i < studio::surface_palette; ++i)
        ids[studio::surface_nodes + studio::surface_texts + i] =
            i < bound_palette ? bound_ids[bound_nodes + bound_texts + i] : kNone;
    Renderer::bind(o, ids);
}
void unused_frame(Object*, i32) {}
const opxy::screen::VTable<Renderer> methods = {&opxy::screen::destroy<Object>, &opxy::screen::destroy<Object>,
                                               reinterpret_cast<const void*>(0x8d51fcd4), unused_frame, init,
                                               reinterpret_cast<const void*>(0x8c0867f0)};
const studio::Editor* editor() { return sdk_sample_studio_editor(); }
}  // namespace

namespace opxy { namespace studio {
bool surface_ready(const char* const* artwork, u32 paths, u32 nodes, u32 texts, u32 palette) {
    if (paths > surface_paths || nodes > surface_nodes || texts > surface_texts || palette > surface_palette || paths > nodes)
        return false;
    bound_ids = artwork;
    bound_paths = paths;
    bound_nodes = nodes;
    bound_texts = texts;
    bound_palette = palette;
    if (!constructed) {
        opxy::screen::base_construct(&object.base, 0, kAsset);
        object.base.vtable = &methods;
        constructed = true;
    }
    if (!bound) {
        if (!object.base.svg) stock<void(opxy::screen::Base*)>(0x8d520ce0)(&object.base);
        if (object.base.svg) {
            init(&object);
            bound = true;
        }
    }
    return bound;
}
}}  // namespace opxy::studio

namespace {
// draw::valid for the slots the artwork bound. The Surface is fixed at its capacities and an
// editor binds fewer; its unbound slots stay zero, which draw::valid refuses (no path key, no
// scale), so the check and the apply both stop at the bound counts.
bool bound_valid(const studio::Surface& frame) {
    using namespace opxy::draw;
    for (u32 p = 0; p < bound_paths; ++p) {
        if (!frame.paths[p].key) return false;
        for (u32 i = 0; i < studio::surface_points; ++i) {
            const Point& point = frame.paths[p].points[i];
            if (point.x < -path_extent * unit || point.x > path_extent * unit ||
                point.y < -path_extent * unit || point.y > path_extent * unit) return false;
        }
    }
    for (u32 i = 0; i < bound_nodes; ++i) {
        const Node& node = frame.nodes[i];
        if (!placement(node.x, node.y, node.scale) || !rotation_valid(node.cos, node.sin) ||
            node.shade >= bound_palette || node.paint > fill_and_stroke) return false;
    }
    for (u32 i = 0; i < bound_texts; ++i) {
        const Text& text = frame.texts[i];
        if (!placement(text.x, text.y, text.scale) || !terminated(text.chars, text_capacity)) return false;
    }
    for (u32 i = 0; i < opxy::page_controls; ++i)
        if (!terminated(frame.header[i], header_capacity)) return false;
    return true;
}
bool refusal_said;
}  // namespace

namespace opxy { namespace studio {
// Only the bound slots are checked and applied: the rest of the Surface is the editor's to leave empty.
void surface_present(const Surface& frame) {
    if (!bound) return;
    if (!bound_valid(frame)) {
        if (!refusal_said) log(__LINE__, "surface: a frame out of range, not drawn");
        refusal_said = true;
        return;
    }
    for (u32 p = 0; p < bound_paths; ++p) Renderer::apply_path(&object, frame, p);
    for (u32 i = 0; i < bound_nodes; ++i) Renderer::apply_node(&object, frame, i);
    for (u32 i = 0; i < bound_texts; ++i) Renderer::apply_text(&object, frame, i);
    opxy::screen::draw(&object.base);
}
void surface_blank() {
    i32 a[2] = {-4096, 110 * 1024}, b[2] = {484 * 1024, 110 * 1024};
    const u32 black = 0xff000000;
    stock<void(i32*, i32*, i32, const u32*)>(0x8d5fe884)(a, b, 224 * 1024, &black);
}
}}  // namespace opxy::studio

// ---------------------------------------------------------------- the library's entry branches
// A1 Keys: the library controller's methods see every key and encoder while the library is on top:
// press (ctl, event) with event[0] the key, Shift press likewise, release (ctl, key), turn (ctl,
// position, event) with event[1] the detents. Returning 1 keeps the key from the global handlers.
u32 sdk_sample_studio_press(unsigned char* controller, const u32* event) {
    const studio::Editor* e = editor();
    if (event && e && e->press(event[0], false)) return 1;
    return sdk_sample_studio_stock_press(controller, event);
}
u32 sdk_sample_studio_shift_press(unsigned char* controller, const u32* event) {
    const studio::Editor* e = editor();
    if (event && e && e->press(event[0], true)) return 1;
    return sdk_sample_studio_stock_shift_press(controller, event);
}
u32 sdk_sample_studio_release(unsigned char* controller, u32 key) {
    const studio::Editor* e = editor();
    if (e && e->release(key)) return 1;
    return sdk_sample_studio_stock_release(controller, key);
}
void sdk_sample_studio_turn(unsigned char* controller, u32 position, const i32* event) {
    const studio::Editor* e = editor();
    if (event && e && e->turn(position & 3u, event[1])) return;
    sdk_sample_studio_stock_turn(controller, position, event);
}
namespace {
void* frame_screen;
i32 frame_elapsed;
bool stock_drawn;
}
void opxy::studio::stock_frame() {
    if (!frame_screen || stock_drawn) return;
    stock_drawn = true;
    sdk_sample_studio_stock_frame(frame_screen, frame_elapsed);
}
void sdk_sample_studio_frame(void* screen, i32 elapsed) {
    const studio::Editor* e = editor();
    frame_screen = screen;
    frame_elapsed = elapsed;
    stock_drawn = false;
    const bool drew = e && e->frame(elapsed);
    if (!drew) studio::stock_frame();
    frame_screen = nullptr;
}
