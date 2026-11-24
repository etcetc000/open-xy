// Stock sample dispatch for every registered SDK sample engine. A matching
// engine number is not enough: the machine's registration must still be ours.
// The stubs in sample_hooks.s call these from inside stock functions.
#include "sample_machines.hpp"
#include "drum_machines.hpp"

using opxy::u32;
using opxy::platform::SampleMachine;

extern "C" {
u32 sdk_sample_drum_alias(u32) __asm__("sdk_sample_drum_alias");
u32 sdk_sample_note_route(u32) __asm__("sdk_sample_note_route");
u32 sdk_sample_alias(u32) __asm__("sdk_sample_alias");
}

const SampleMachine* sdk_sample_machine(u32 engine) {
    for (u32 i = 0; i < sdk_sample_machine_count; ++i) {
        const SampleMachine& m = sdk_sample_machines[i];
        if (m.engine() == engine) return m.ready() == 1 ? &m : nullptr;
    }
    return nullptr;
}

// Note-region hook, decoded by extensions_sample_note_region (sample_hooks.s).
// An owned engine on page 2 borrows the stock sampler's (2) payload and
// route: one sample played chromatically across the keyboard. On the drum
// page (7) notes map to its canonical pads.
enum NoteRoute : u32 { kStockRoute = 0, kOwnedNearest = 1, kOwnedDrumPads = 2, kOwnedSampler = 3,
                       kOwnedSynthesis = 4 };
u32 sdk_sample_note_route(u32 engine) {
    if (sdk_drum_machine(engine)) return kOwnedSynthesis;
    const u32 page = opxy::platform::sample_page(engine);
    return !page ? kStockRoute : page == 7 ? kOwnedDrumPads : kOwnedSampler;
}

// Stock's sample predicates know only stock engine IDs. Owned SDK engines
// borrow a stock payload, the sampler's (2) on page 2 and the multisampler's
// (30) on page 7; the track keeps the real engine.
u32 sdk_sample_alias(u32 engine) {
    const u32 page = opxy::platform::sample_page(engine);
    return page == 2 ? 2 : page ? 30 : engine;
}

// Only the drum-page key handlers use this identity: stock ID 3 enables the
// held-pad overlay, slicing and individual pad copy/paste. Audio and sample
// loading still use the multisampler alias above. Never change the saved ID.
u32 sdk_sample_drum_alias(u32 engine) { return opxy::platform::sample_page(engine) == 7 ? 3 : engine; }
