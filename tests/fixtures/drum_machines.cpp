#include <cstdlib>
#include "drum_machines.hpp"

namespace {
int state = 1;
int ready() { return state; }
void defaults(opxy::u32, opxy::u8*, opxy::u8*) {}
const char* label(opxy::u32, opxy::u32) { return ""; }
const char* name(opxy::u32) { return ""; }
opxy::u32 count(opxy::u32) { return 8; }
void require(bool ok) { if (!ok) std::abort(); }
}

extern "C" {
const opxy::platform::DrumMachine sdk_drum_machines[] = {
    {reinterpret_cast<const char*>(34), ready, 12, defaults, label, name, count},
    {reinterpret_cast<const char*>(35), ready, 0, defaults, label, name, count},
    {reinterpret_cast<const char*>(36), ready, 65, defaults, label, name, count}
};
const opxy::u32 sdk_drum_machine_count = 3;
}

int main() {
    require(sdk_drum_machine(34) == sdk_drum_machines);
    require(sdk_drum_alias(34) == 4);
    require(sdk_drum_alias(4) == 4 && !sdk_drum_machine(4));
    require(sdk_drum_alias(35) == 35 && sdk_drum_alias(36) == 36);
    require(sdk_drum_alias(99) == 99);
    state = 0;
    require(!sdk_drum_machine(34) && sdk_drum_alias(34) == 34);
    state = -2;  // Conflicting registration is never considered owned.
    require(!sdk_drum_machine(34) && sdk_drum_alias(34) == 34);
    state = 2;
    require(!sdk_drum_machine(34));
    return 0;
}
