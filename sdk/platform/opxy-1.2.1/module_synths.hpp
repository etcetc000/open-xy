// Synths from module files: the firmware's side of ModuleApi::synth_register
// and the startup hooks that treat them like built-in synths
// (module_synths.cpp). GUI and startup paths only, except the voice table.
#pragma once
#include <opxy/module.hpp>

extern "C" {
// Heap for module machines (module_probe.cpp): zeroing is the caller's.
unsigned char* sdk_module_allocate(opxy::u32 bytes) __asm__("sdk_module_allocate");
opxy::u32 stock_gui_role() __asm__("stock_gui_role");
opxy::u32 stock_catalog_insert(void*, const void*, opxy::u32) __asm__("stock_catalog_insert");

int sdk_module_synth_register(const opxy::ModuleSynth*) __asm__("sdk_module_synth_register");
void* sdk_module_synth_voice(int handle, opxy::u32 index) __asm__("sdk_module_synth_voice");
// Registration is open only while the loader runs module inits.
void sdk_module_registration(bool open) __asm__("sdk_module_registration");
// Startup hooks (startup.cpp.in): GUI screens and engines, catalog insertion,
// own pages and the preset scan's installed check. The catalog hook returns 1
// when the name is a module synth's, with stock's result in *result.
void sdk_module_screens() __asm__("sdk_module_screens");
int sdk_module_catalog(void* catalog, const void* name, opxy::u32 id, opxy::u32* result) __asm__("sdk_module_catalog");
int sdk_module_screen_owned(opxy::u32 page) __asm__("sdk_module_screen_owned");
int sdk_module_engine_installed(const void* name) __asm__("sdk_module_engine_installed");
}
