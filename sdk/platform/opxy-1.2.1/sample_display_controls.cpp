// GUI display routing is independent of sample payload identity. All unknown,
// conflicting or unloaded registrations keep the complete stock route.
#include "sample_display_controls.hpp"
#include "sample_machines.hpp"
#include "stock.hpp"
extern "C" opxy::u32 sdk_sample_alias(opxy::u32) __asm__("sdk_sample_alias");
namespace {
using namespace opxy;
using namespace opxy::platform;
struct Mode { u32 engine; bool trim; };
Mode modes[kTracks];
SampleDisplayKey sample_key;
bool instrument_page() {
    const u32* controller = stock<const u32*()>(0x8c0944b0)();
    return controller && *controller == 0x8400dff8 && word(0x080238bc) == 0;
}
u32 display_page(u32 engine) {
    const SampleMachine* machine = sdk_sample_machine(engine);
    const u32 page = machine && machine->display_page ? machine->display_page() : 0;
    return sample_display_page_valid(page) ? page : 0;
}
Mode& mode(u32 track, u32 engine) {
    Mode& value = modes[track];
    if (value.engine != engine) { value.engine = engine; value.trim = false; }
    return value;
}
}
namespace opxy { namespace platform {
bool sample_display_capable(u32 engine) {
    const SampleMachine* machine = sdk_sample_machine(engine);
    return machine && machine->display_page;
}
bool sample_display_active(u32 engine) {
    const u32 track = byte(kSelectedTrack);
    return track < kTracks && display_page(engine) && !mode(track, engine).trim;
}
}}
bool sdk_sample_display_key_event(const void* raw) {
    if (!raw) return false;
    const u32* event = static_cast<const u32*>(raw);
    if (event[0] != 23) return false;
    // Ownership survives navigation/Shift release. Never swallow a release
    // whose press stock already received, nor toggle on repeated keydowns.
    if (sample_key.owned) return sample_key.consume(event[0], event[1] != 0, false);
    if (!event[1] || !shift_held() || !instrument_page() ||
        stock<u32(u32)>(0x8d5b7d7c)(23)) return false;
    const u32 track = byte(kSelectedTrack);
    if (track >= kTracks) return false;
    const u32 engine = track_engine(track);
    if (!display_page(engine)) return false;
    Mode& value = mode(track, engine);
    value.trim = !value.trim;
    sample_key.consume(event[0], true, true);
    return true;
}
u32 sdk_sample_display_page(void* controller) {
    if (instrument_page()) {
        const u32 track = byte(kSelectedTrack);
        if (track < kTracks) {
            const u32 engine = track_engine(track);
            if (sample_display_active(engine)) return display_page(engine);
        }
    }
    return stock<u32(void*)>(0x8c0b1198)(controller);
}
u32 sdk_sample_parameter_kind(u32 engine) {
    // The parameter bank belongs to the engine even when its stock waveform
    // is shown: live/step locks must never write stock sampler knob storage.
    return sample_display_capable(engine) ? 0 : stock<u32(u32)>(0x8d5b79c0)(engine);
}

// Shared generated callback tables exist even for a synth-only collection.
extern "C" u32 sdk_sample_selected_engine() __asm__("sdk_sample_selected_engine");
u32 sdk_sample_selected_engine() {
    const u32 track = opxy::platform::selected_track();
    return track < opxy::platform::kTracks ? opxy::platform::track_engine(track) : opxy::platform::kNoEngine;
}
