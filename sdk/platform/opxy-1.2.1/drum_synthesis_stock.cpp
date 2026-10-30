// GUI role. 8c0b147a calls the stock per-key editor with the selected/held key,
// control index and encoder event. Keep publication on stock's existing route.
#include "drum_values.hpp"
#include "synth_controls.hpp"

opxy::u32 sdk_drum_edit(opxy::u32 key, opxy::u32 control, const opxy::u32* event, opxy::u32 fine) {
    using namespace opxy;
    using namespace opxy::platform;
    const u32 track = byte(kSelectedTrack);
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    if (!machine) return stock<u32(u32, u32, const u32*, u32)>(0x8d4d5f60)(key, control, event, fine);
    if (!event || control >= kDrumControlCount) return 0;
    u8 shown[page_controls];
    sdk_synth_shown(track, shown);
    // Stock maps Shift's four encoders to indices 4..7. Shift retains the
    // visible parameter, as on synths; these discrete 7-bit controls already
    // use the finest representable step (one integer per detent).
    const u32 slot = shown[control % page_controls];
    if (slot >= kDrumControlCount) return 0;
    void* region = pad_region(track, key);
    u8 controls[kDrumControlCount];
    if (!drum_region_values(*machine, track, key, region, controls) || slot >= machine->count(controls[0])) return 0;
    // Encoder event word one is its signed delta. Bound before addition to
    // avoid overflow and preserve exact integer steps even at high velocity.
    i32 delta = static_cast<i32>(event[1]);
    if (!delta) return 0;
    if (delta > 127) delta = 127;
    if (delta < -127) delta = -127;
    const i32 limit = slot == 0 ? static_cast<i32>(machine->model_count - 1) : 127;
    i32 next = static_cast<i32>(controls[slot]) + delta;
    if (next < 0) next = 0;
    if (next > limit) next = limit;
    if (next == controls[slot]) return 0;
    controls[slot] = static_cast<u8>(next);
    if (!drum_pack(controls, static_cast<i32*>(region) + 1)) return 0;
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
    return 0;
}
