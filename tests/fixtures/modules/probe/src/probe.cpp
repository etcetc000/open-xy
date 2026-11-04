// Probe module: exercises every relocation type, an import by name and bss,
// then reports through the API. It registers nothing.
#include <opxy/module.hpp>
using namespace opxy;
namespace {
u32 counter;                       // bss
const char* const words[] = {"relocated", "bss", "init"};   // ABS32 data
const char* role_name(u32 gui) { return gui ? "gui" : "audio"; }
const char* (*const lookup)(u32) = role_name;               // ABS32 function pointer
}

extern "C" int opxy_module_init(const ModuleApi* api) {
    if (!api || api->version < 1 || !api->note) return 1;
    if (counter != 0) return 2;    // bss must start zeroed
    counter = 3;
    for (u32 i = 0; i < counter; ++i) api->note(words[i]);   // HI16/LO16 for &words
    api->note(lookup(api->gui));
    core::note("by name");   // IMPORT
    return 0;
}
