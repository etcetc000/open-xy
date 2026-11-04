#include "drum_machines.hpp"
#include "drum_storage.hpp"
#include "drum_locks.hpp"
#include "stock.hpp"

const opxy::platform::DrumMachine* sdk_drum_machine(opxy::u32 engine) {
    for (opxy::u32 i = 0; i < sdk_drum_machine_count; ++i) {
        const opxy::platform::DrumMachine& machine = sdk_drum_machines[i];
        if (machine.engine() == engine)
            return machine.ready() == 1 && machine.model_count && machine.model_count <= opxy::platform::kDrumModelLimit ? &machine : nullptr;
    }
    return nullptr;
}

opxy::u32 sdk_drum_alias(opxy::u32 engine) { return sdk_drum_machine(engine) ? 4 : engine; }

// Project load's check of each pattern's track object (drum_hooks.s, 8d588ec8):
// the engine's alias, after a padMix kit's lock words get their neutral base
// in the object's stored words (+0x3864), as stock gives a Drumline track's
// eight just after (8d588dfe).
opxy::u32 sdk_drum_load_engine(opxy::u32 engine, opxy::u32 object) {
    using namespace opxy::platform;
    const DrumMachine* machine = sdk_drum_machine(engine);
    if (machine && machine->pad_mix && object)
        for (opxy::u32 i = 0; i < kDrumLocks; ++i)
            at<volatile opxy::i32>(object + 0x3864)[kDrumLockWord + i] = kDrumLockNeutral;
    return machine ? 4 : engine;
}
