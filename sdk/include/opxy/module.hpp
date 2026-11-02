// What a module file gets from the firmware: one table, passed to its init.
// The table only grows at the end. Check api->version >= N before using a
// member added in version N; never compare with ==.
#pragma once
#include <opxy/types.hpp>
namespace opxy {
const u32 module_api_version = 1;

struct ModuleApi {
    u32 version;
    u32 gui;                           // 1 on the GUI processor, 0 on the audio processor
    void (*note)(const char* text);    // one short line into this role's module report
};
}

// Every module exports this. A nonzero return keeps the module out.
extern "C" int opxy_module_init(const opxy::ModuleApi* api);
