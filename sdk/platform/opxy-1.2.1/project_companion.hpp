// Project companions (docs/projects.md): what a saved project's machine numbers meant.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// A machine's ID and the absolute symbols holding its engine or effect number and page.
struct CompanionMachine {
    const char* id;
    const char* number;
    const char* page;
    bool effect;
};
}}

// Generated per collection; ends with a null ID.
extern const opxy::platform::CompanionMachine sdk_companion_machines[];

// What track control machines keep, written into the open companion: generated in
// startup.cpp, which reaches the control service (track_control_project.cpp) when the
// collection links it.
extern "C" opxy::u32 sdk_project_kept(void* file) __asm__("sdk_project_kept");
