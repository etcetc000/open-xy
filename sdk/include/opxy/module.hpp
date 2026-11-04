// What a module file gets from the firmware: one table, passed to its init.
// The table only grows at the end. Check api->version >= N before using a
// member added in version N; never compare with ==.
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
    u32 engine_id, page_id;      // the machine's numbers from its layout; checked against every owner
    // The module's own entry points, so stock can call them without context:
    // factory forwards to api->synth_voice with the handle synth_register gave.
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

// Every module exports this. A nonzero return keeps the module out.
extern "C" int opxy_module_init(const opxy::ModuleApi* api);
