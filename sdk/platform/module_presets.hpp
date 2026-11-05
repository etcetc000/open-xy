// Module presets as stock's factory preset walk sees them (module_synths.cpp
// binds this to stock): content/presets/<machine ID>/<name>.json for each
// preset a loaded module ships, gone with the module. Portable, host-tested.
//
// content/presets lists stock's folders, then one per module with presets; a
// stock folder with a module's name is left out, so the module's takes its
// place. content/presets/<id> lists only the module's presets, never files
// left there. A folder entry holds its type at +0xa (4 folder, 8 file) and its
// 0-terminated name at +0xb, as stock's folder call (8c0be6a4) returns them.
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace modules {
const u32 entry_type = 0xa, entry_name = 0xb, entry_folder = 4, entry_file = 8;
const u32 entry_bytes = 64;  // room for a 24-character preset name and .json

// One loaded module's presets, by its machine ID.
struct PresetModule {
    const char* id;
    u32 count;
    const char* const* names;  // without .json
};
struct PresetModules {
    const PresetModule* modules;
    u32 count;
};

// content/presets/<id>/<name>.json, with or without stock's /yaffs2/ in front:
// the module and preset it names.
bool preset_file(const PresetModules& all, const char* path, u32& module, u32& preset);

// What a folder path is to the walk: 0 not a module's concern, 1 content/presets,
// 2 content/presets/<id> of a module with presets (module set).
const u32 folder_stock = 0, folder_root = 1, folder_module = 2;
u32 preset_folder(const PresetModules& all, const char* path, u32& module);

// Whether a stock entry in content/presets is a folder a module's takes the place of.
bool replaced(const PresetModules& all, const unsigned char* entry);

// A virtual folder's position: after stock's entries in content/presets, the
// next module folder; in a module's folder, its next preset.
struct PresetFolder {
    u32 kind;    // folder_root or folder_module
    u32 module;  // folder_module: whose presets
    u32 next;
};
// The folder's next virtual entry written into entry (entry_bytes long), or false at its end.
bool next_entry(const PresetModules& all, PresetFolder& folder, unsigned char* entry);
}}
