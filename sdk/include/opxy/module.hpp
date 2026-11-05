// What a module file gets from the firmware: one table, passed to its init.
// The table only grows at the end. Check api->version >= N before using a
// member added in version N; never compare with ==.
//
// Calls into the firmware go by name (opxy::core below), so their order never
// matters. Structs still go by position: every struct a module and the
// firmware share (ModuleSynth here, draw::Input and the other <opxy/...>
// types a machine reads) only grows at the end, and a module built before a
// field moved reads the wrong one without notice. The sturdier way, not built
// yet: no shared structs at all. The firmware hands values out through
// functions by name (accessors in), and the module offers its callbacks and
// sizes as named symbols the loader finds (named exports out).
//
// Machine authors never use this header. The module builder (sdk.py module
// build --machine) generates a machine module's init, which describes the
// unchanged machine with ModuleSynth and registers it.
#pragma once
#include <opxy/types.hpp>
#include <opxy/draw.hpp>
namespace opxy {
const u32 module_api_version = 2;

// A synth, as its generated init describes it. Callbacks take the machine's
// own types as untyped pointers; the generated wrappers cast them back.
struct ModuleSynth {
    u32 size;                    // sizeof(ModuleSynth) as built; later fields only when larger
    const char* name;            // native engine name, 1-15 characters
    u32 engine_id, page_id;      // unused, 0: the firmware numbers a module by its machine ID
    // The module's own entry points, so stock can call them without context:
    // factory forwards to core::synth_voice with the handle synth_register gave.
    void* (*factory)(u32 index);
    const char* (*parameter_name)(u32 index);
    // Voice: as voice.cpp.in compiles them in for a built-in synth.
    u32 voice_bytes, voice_alignment, shared_bytes, capacity;
    void (*initialize)(void* voice, void* shared, u32 id);
    void (*start)(void* voice, void* shared, u32 id);
    void (*destroy)(void* voice, void* shared, u32 id);
    void (*render)(void* voice, void* shared, u32 id, i32* output, const i32* controls, const u32* increments);
    // Display: the machine's draw::Frame<paths, points, nodes, texts> and the
    // offsets the compiler gave its members, checked by the firmware.
    u32 display_bytes, display_alignment, frame_bytes;
    u32 paths, points, nodes, texts, palette;
    u32 nodes_offset, texts_offset, header_offset;
    const char* const* ids;      // nodes + texts + palette artwork IDs, as the screen finds them
    const char* asset;           // the page's SVG asset name
    void (*display_reset)(void* state, void* frame);
    void (*display_render)(void* state, const draw::Input* input, void* frame);
    unsigned (*display_validate)(const void* frame);
    // Content the module carries, so nothing has to be installed beside it:
    // the page's artwork (stock loads it as content/display/<asset>) and the
    // factory catalog entry, one JSON object for "engine_types".
    const char* svg;
    u32 svg_bytes;
    const char* catalog_entry;
    // The machine's presets, shown as content/presets/<machine ID>/<name>.json
    // while the module is loaded and gone with it: preset_count names (as
    // tooling/machines.py PRESET_NAME, without .json) and their JSON texts.
    // Only when size covers them; a module built before they existed has none.
    u32 preset_count;
    const char* const* preset_names;
    const char* const* preset_texts;
    // The machine's controls, as a build's synth control table row holds them
    // (tooling/adapters.py control_row): each slot's press target, the
    // continuous and absent masks, enum step counts, whether a press pages the
    // row, the stock pages it hides, and its caption function (control_formatter).
    // Only when size covers them; a module built before they existed has none.
    u8 control_next[8];
    u32 control_continuous;
    u8 control_steps[8];
    u32 control_page, control_hidden_pages, control_absent;
    bool (*control_format)(u32 slot, i32 value, char* text);
};

struct ModuleApi {
    u32 version;
    u32 gui;                           // 1 on the GUI processor, 0 on the audio processor
    void (*note)(const char* text);    // one short line about this module
    // v2: registers a synth from init. Returns its handle (>= 0) or a negative refusal.
    int (*synth_register)(const ModuleSynth* synth);
    // v2: stock's voice factory for a registered synth; ModuleSynth::factory calls it.
    void* (*synth_voice)(int handle, u32 index);
};

// Refusals from synth_register.
const int module_invalid = -1, module_taken = -2, module_full = -3, module_late = -4, module_memory = -5;
}

// Firmware functions by name. A module calls opxy::core::<name>(...); the
// builder gives each one it uses a slot in the module's data, and the loader
// writes the firmware's address for that name into it. What the firmware adds
// or reorders never moves a module's calls; a name the firmware lacks keeps
// the module out (m-112). Only the names below exist.
extern "C" {
extern void (*opxy_import_note)(const char* text) __asm__("opxy_import_note");
extern int (*opxy_import_synth_register)(const opxy::ModuleSynth* synth) __asm__("opxy_import_synth_register");
extern void* (*opxy_import_synth_voice)(int handle, opxy::u32 index) __asm__("opxy_import_synth_voice");
// Control captions, as a build's synth control table formats them (synth_controls.hpp).
extern void (*opxy_import_control_tenths)(char* text, opxy::u32 tenths, const char* suffix)
    __asm__("opxy_import_control_tenths");
extern opxy::u32 (*opxy_import_control_ratio)(opxy::i32 value, const opxy::u32* anchors, opxy::u32 count)
    __asm__("opxy_import_control_ratio");
extern void (*opxy_import_control_label)(char* text, opxy::i32 value, const char* const* labels, opxy::u32 count)
    __asm__("opxy_import_control_label");
extern void (*opxy_import_control_division)(char* text, opxy::i32 value) __asm__("opxy_import_control_division");
}
namespace opxy { namespace core {
inline void note(const char* text) { opxy_import_note(text); }
inline int synth_register(const ModuleSynth* synth) { return opxy_import_synth_register(synth); }
inline void* synth_voice(int handle, u32 index) { return opxy_import_synth_voice(handle, index); }
inline void control_tenths(char* text, u32 tenths, const char* suffix) { opxy_import_control_tenths(text, tenths, suffix); }
inline u32 control_ratio(i32 value, const u32* anchors, u32 count) { return opxy_import_control_ratio(value, anchors, count); }
inline void control_label(char* text, i32 value, const char* const* labels, u32 count) {
    opxy_import_control_label(text, value, labels, count);
}
inline void control_division(char* text, i32 value) { opxy_import_control_division(text, value); }
}}

// Every module exports this. A nonzero return keeps the module out.
extern "C" int opxy_module_init(const opxy::ModuleApi* api);
