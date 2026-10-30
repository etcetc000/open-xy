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
