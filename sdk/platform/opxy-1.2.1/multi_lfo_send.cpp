// The GUI role's stock boundary of the four LFOs (multi_lfo.hpp).
// - B3 global-parameter publication 8d4dc84c(id, value), as the master
//   processor sends its snapshots; the audio role's receiver (b3_receive.cpp)
//   takes the LFOs' virtual IDs before stock's setter.
// - Stock's own LFO storage on the track's current pattern (8d587708(track)):
//   type +0x20 (8d587920), on +0x24 (8d5879c0), stored words +0x3864 (8d5ac910).
// - Loading it as stock's LFO type list does (8c0b242c: 8d4e1f88, then
//   8d5accf8): the stored words (8d587a20), type (8d5878e8) and on (8d587984),
//   the type and on to the audio role (8d4dc794), then the stored words copied
//   to the live words and the track marked for publication (8d5accf8). The
//   page draws from that storage every frame: M4 and the type list call nothing
//   else to redraw it. The encoder's word setter (8d5acf28) is not used: while
//   recording it would record a step lock.
// All three message kinds share stock's one GUI-to-audio queue (11a00ec0 on
// 0x841460b4), in order.
#include "multi_lfo.hpp"
#include "stock.hpp"

namespace opxy { namespace platform { namespace multi_lfo {
namespace { const u32 kGuiWords = 0x86bd027c; }
void send(u32 id, i32 value) { stock<void(u32, i32)>(0x8d4dc84c)(id, value); }

void read_stock(u32 track, Settings& out) {
    out.type = stock<u32(u32)>(0x8d587920)(track);
    out.on = stock<u32(u32)>(0x8d5879c0)(track) & 0xffu ? 1u : 0u;
    for (u32 i = 0; i < words; ++i)
        out.words[i] = stock<i32(u32, u32, u32)>(0x8d5ac910)(kGuiWords, track, first_word + i);
}

void load_stock(u32 track, const Settings& s) {
    for (u32 i = 0; i < words; ++i) stock<void(u32, u32, i32)>(0x8d587a20)(track, first_word + i, s.words[i]);
    stock<void(u32, u32)>(0x8d5878e8)(track, s.type);
    stock<void(u32, u32)>(0x8d587984)(track, s.on ? 1u : 0u);
    stock<void(u32, u32, u32)>(0x8d4dc794)(track & 0xffu, s.type, s.on ? 1u : 0u);
    stock<void(u32, u32)>(0x8d5accf8)(kGuiWords, track);
}
}}}
