// Synths from module files: the firmware's side of ModuleApi::synth_register
// and the startup hooks that treat them like built-in synths
// (module_synths.cpp). GUI and startup paths only, except the voice table.
#pragma once
#include <opxy/module.hpp>

extern "C" {
// Heap and files for module machines (module_loader_stock.cpp): zeroing is the caller's.
unsigned char* sdk_module_allocate(opxy::u32 bytes) __asm__("sdk_module_allocate");
void sdk_module_release(unsigned char*) __asm__("sdk_module_release");
unsigned char* sdk_module_read_file(const char* path, opxy::u32& bytes) __asm__("sdk_module_read_file");
// Loads the modules: from startup, or earlier when the catalog is read first (idempotent).
void sdk_module_load() __asm__("sdk_module_load");
void* sdk_module_asset_load(const char* path) __asm__("sdk_module_asset_load");
bool sdk_module_catalog_load(const char* path, void* out) __asm__("sdk_module_catalog_load");
opxy::u32 stock_gui_role() __asm__("stock_gui_role");
opxy::u32 stock_catalog_insert(void*, const void*, opxy::u32) __asm__("stock_catalog_insert");

int sdk_module_synth_register(const opxy::ModuleSynth*) __asm__("sdk_module_synth_register");
void* sdk_module_synth_voice(int handle, opxy::u32 index) __asm__("sdk_module_synth_voice");
// Registration is open only while the loader runs module inits.
void sdk_module_registration(bool open) __asm__("sdk_module_registration");
// The machine ID (lowercase) of the module whose init runs now, or null after it.
void sdk_module_current(const char* id) __asm__("sdk_module_current");
// Startup hooks (startup.cpp.in): GUI screens and engines, catalog insertion,
// own pages and the preset scan's installed check. The catalog hook returns 1
// when the name is a module synth's, with stock's result in *result.
void sdk_module_screens() __asm__("sdk_module_screens");
int sdk_module_catalog(void* catalog, const void* name, opxy::u32 id, opxy::u32* result) __asm__("sdk_module_catalog");
int sdk_module_screen_owned(opxy::u32 page) __asm__("sdk_module_screen_owned");
int sdk_module_engine_installed(const void* name) __asm__("sdk_module_engine_installed");
}
