// GUI role. The played-pad snapshot of a drum kit's drawn screen (draw::Input
// trigger_*, SDK 1.21): only a kit declaring 1.21 or later calls it
// (screen_drawing.hpp), so it is the unit of the played_pad service, which a
// build links only with such a kit (tooling/native.py UNIT_SERVICES).
#include "drum_values.hpp"
#include <opxy/draw.hpp>
extern "C" void sdk_drum_trigger(opxy::u32, opxy::draw::Input*) __asm__("sdk_drum_trigger");

void sdk_drum_trigger(opxy::u32 track, opxy::draw::Input* input) {
    using namespace opxy;
    using namespace opxy::platform;
    input->trigger_key = input->trigger_model = 0;
    for (u32 i = 0; i < 8; ++i) input->trigger_controls[i] = 0;
    if (track >= kTracks) return;
    // Stock's last played pitch accompanies the same start counter for live
    // notes (8d595838) and sequenced notes (8d592092). The held-key map only
    // describes live gates and cannot identify sequenced drum strikes.
    const u32 note = drum_played_key(track);
    if (note >= 128) return;
    const DrumMachine* machine = sdk_drum_machine(track_engine(track));
    const void* region = machine ? pad_region(track,note) : nullptr;
    u8 model, controls[8], live[8];
    if (!region || !drum_region_values(*machine,track,note,region,&model,controls)) return;
    const u32 key = drum_canonical_key(track,note,region);
    if (key < 53 || key >= 77) return;
    // The track's offsets, as the audio adapter applies them; none without a
    // whole, aligned vector of eight words.
    const TrackParameters* parameters = track_parameters(track);
    const u32 begin = parameters ? parameters->begin : 0, end = parameters ? parameters->end : 0;
    const i32* offsets = begin && !(begin & 3) && end >= begin && end-begin >= 32
        ? at<const i32>(begin) : nullptr;
    drum_offset(controls,offsets,live);
    input->trigger_key = key;
    input->trigger_model = model;
    for (u32 i = 0; i < 8; ++i)
        input->trigger_controls[i] = live[i] == 127 ? 0x7fffffff : static_cast<i32>(u32(live[i]) << 24);
}
