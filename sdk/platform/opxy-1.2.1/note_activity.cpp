#include "note_activity.hpp"
#include "stock.hpp"

namespace {
// Stock 8d5b7d10 reads map[track * 128 + note]; 8d5b7d54 sets it,
// 8d5b7d2c clears it. Both note-off routes in 8d595420 call the latter.
// The same map paints stock's keyboard, independently of voice/envelope life.
const opxy::u32 kNoteMap = 0x86bda258;
opxy::u32 releases[opxy::platform::kTracks];
}

extern "C" opxy::u32 sdk_note_off(opxy::u32 track, opxy::u32 note) {
    using namespace opxy::platform;
    const bool held = track < kTracks && note < 128 && byte(kNoteMap + track * 128 + note);
    // Always delegate, including unowned engines and tracks outside the display
    // API. Preserve stock's return value and exactly one clear per call.
    const opxy::u32 result = stock<opxy::u32(opxy::u32, opxy::u32)>(0x8d5b7d2c)(track, note);
    if (held) ++releases[track];
    return result;
}

extern "C" void sdk_display_note_activity(opxy::u32 track, opxy::draw::Input* input) {
    using namespace opxy::platform;
    input->held_notes = 0;
    input->note_off_serial = track < kTracks ? releases[track] : 0;
    if (track >= kTracks) return;
    for (opxy::u32 note = 0; note < 128; ++note)
        if (byte(kNoteMap + track * 128 + note)) ++input->held_notes;
}
