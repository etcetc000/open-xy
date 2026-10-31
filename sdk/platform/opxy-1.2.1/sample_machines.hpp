// The collection's sample machines as one generated table (sample_callbacks.cpp)
// and the ownership lookup every sample hook uses.
#pragma once
#include <opxy/sample_engine.hpp>

namespace opxy { namespace platform {
// One sample machine: its identity, readiness check and declared callbacks.
// A callback a machine does not declare is null. In a browser kit, an
// unselected machine's symbols resolve to a function returning 0, so `ready`
// declines and nothing else is called.
struct SampleMachine {
    const char* engine_id;  // absolute symbol: the engine ID, supplied at link time
    u32 page;               // stock sample page, 2 or 7: what the machine is, never renumbered
    int (*ready)();
    // padSettings
    PadSettings (*defaults)();
    PadSettings (*assign)(const SampleAssignment*);
    // drumControls
    void (*control)(PadSettings*, const DrumControl*);
    void (*panel)(PadSettings, const SampleRegion*, const i32*, DrumPanel*);
    // sampleAnalysis: one preparation at a time per machine
    u32 analysis_budget;  // bytes of stock heap its cached analyses may hold
    u32 (*analysis_words)(const SampleView*);
    u32 (*prepare_begin)(const SampleView*, u32*, u32);
    u32 (*prepare_step)(u32, u32*);
    u32 (*prepared)(SampleAnalysis*);
    // sliceTiming: source-frame clock for a frozen machine audition.
    void (*slice_clock)(const SampleSlice*, SampleSliceRate*);
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(engine_id)); }
};
}}

extern "C" {
extern const opxy::platform::SampleMachine sdk_sample_machines[] __asm__("sdk_sample_machines");
extern const opxy::u32 sdk_sample_machine_count __asm__("sdk_sample_machine_count");
// The machine registered as `engine`, when its stock record and name are still
// ours; null for stock, absent and conflicting registrations.
const opxy::platform::SampleMachine* sdk_sample_machine(opxy::u32 engine) __asm__("sdk_sample_machine");
// Each track's keyboard octave as last stored or looked up on the GUI
// (drum_controls_stock.cpp); audio maps drum notes with it instead of calling
// GUI project getters.
extern volatile opxy::i32 sdk_sample_drum_octaves[8] __asm__("sdk_sample_drum_octaves");
}

namespace opxy { namespace platform {
inline u32 sample_page(u32 engine) {
    const SampleMachine* m = sdk_sample_machine(engine);
    return m ? m->page : 0;
}
inline bool pad_settings(u32 engine) {
    const SampleMachine* m = sdk_sample_machine(engine);
    return m && m->assign;
}
inline bool drum_controls(u32 engine) {
    const SampleMachine* m = sdk_sample_machine(engine);
    return m && m->control;
}
inline bool sample_analysis(u32 engine) {
    const SampleMachine* m = sdk_sample_machine(engine);
    return m && m->analysis_budget;
}
// Whether any registered machine of the collection analyzes samples.
inline bool any_sample_analysis() {
    for (u32 i = 0; i < sdk_sample_machine_count; ++i)
        if (sdk_sample_machines[i].analysis_budget && sdk_sample_machines[i].ready() == 1) return true;
    return false;
}
}}
