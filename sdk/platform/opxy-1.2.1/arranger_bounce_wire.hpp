#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace bounce_wire {
const u32 kSize = 24;
enum Kind { Prepare = 1, Stop, Release, Ready, Progress, Stopped, WriteResult, Released, Error };
struct Message { Kind kind; u32 cookie; u32 args[3]; };

static inline u32 read_word(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}
static inline void write_word(u8* p, u32 value) {
    for (u32 i = 0; i < 4; ++i) p[i] = static_cast<u8>(value >> (i * 8));
}
static inline bool encode(const Message& message, u8 (&out)[kSize]) {
    if (message.kind < Prepare || message.kind > Error || !message.cookie) return false;
    out[0] = 16; out[1] = 'O'; out[2] = 'X'; out[3] = 1;
    out[4] = static_cast<u8>(message.kind);
    out[5] = out[6] = out[7] = 0;
    write_word(out + 8, message.cookie);
    for (u32 i = 0; i < 3; ++i) write_word(out + 12 + i * 4, message.args[i]);
    return true;
}
// Session ownership is checked by the caller after decoding; Prepare starts a new cookie.
static inline bool decode(const void* data, u32 size, bool receiving_master, Message& out) {
    if (!data || size != kSize) return false;
    const u8* p = static_cast<const u8*>(data);
    if (p[0] != 16 || p[1] != 'O' || p[2] != 'X' || p[3] != 1 || p[5] || p[6] || p[7] ||
        p[4] < Prepare || p[4] > Error || receiving_master != (p[4] >= Ready)) return false;
    const u32 cookie = read_word(p + 8);
    if (!cookie) return false;
    const Message message = {static_cast<Kind>(p[4]), cookie,
        {read_word(p + 12), read_word(p + 16), read_word(p + 20)}};
    out = message;
    return true;
}
}}}
