// GUI-side read of a track's stock amp envelope for drawing input. No audio
// callback reads it, and nothing is written.
#pragma once
#include <opxy/draw.hpp>
#include "stock.hpp"

namespace opxy { namespace platform {
// Stock's GUI getter 8d4d7680(track) returns a vector {stock, begin, end} of the
// track's parameter words 8..15, read like 0..7 by the parameter getter 8d4d72a8.
// Stock's envelope page (frame 8d5292bc, layout 8d527fdc) draws words 8..11
// as the amp envelope's attack, decay, sustain and release; it reads them as
// floats through 8d4d7458(track, 8), and the filter envelope from word 32.
struct EnvelopeWords { u32 stock0; u32 begin, end; };
inline void amp_envelope(u32 track, draw::Envelope& amp) {
    amp.attack = amp.decay = amp.sustain = amp.release = draw::envelope_unavailable;
    if (track >= kTracks) return;
    const EnvelopeWords* p = stock<const EnvelopeWords*(u32)>(0x8d4d7680)(track);
    if (!p || !p->begin || p->begin & 3 || p->end < p->begin || p->end - p->begin < 16) return;
    const i32* words = at<const i32>(p->begin);
    const draw::Envelope read = {words[0], words[1], words[2], words[3]};
    if (draw::envelope_known(read)) amp = read;
}
}}
