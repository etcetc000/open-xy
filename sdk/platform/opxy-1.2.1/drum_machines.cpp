#include "drum_machines.hpp"

const opxy::platform::DrumMachine* sdk_drum_machine(opxy::u32 engine) {
    for (opxy::u32 i = 0; i < sdk_drum_machine_count; ++i) {
        const opxy::platform::DrumMachine& machine = sdk_drum_machines[i];
        if (machine.engine() == engine)
            return machine.ready() == 1 && machine.model_count && machine.model_count <= 128 ? &machine : nullptr;
    }
    return nullptr;
}

opxy::u32 sdk_drum_alias(opxy::u32 engine) { return sdk_drum_machine(engine) ? 4 : engine; }
