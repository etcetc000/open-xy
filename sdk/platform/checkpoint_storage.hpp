#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// Bounded word runs for RAM-only snapshots. A token is a nonzero run length;
// its high bit selects zeros, otherwise the following words are literals.
// Exhaustion leaves the committed prefix and `used` unchanged.
template <u32 Capacity> struct CheckpointStorage {
    u32 words[Capacity], used;
    bool append(const u32* source, u32 count, u32& offset, u32& size) {
        u32 in = 0, out = used;
        while (in < count) {
            const bool zero = source[in] == 0;
            const u32 start = in;
            while (in < count && (source[in] == 0) == zero) ++in;
            const u32 run = in - start;
            if (out >= Capacity || (!zero && run > Capacity - out - 1)) return false;
            words[out++] = run | (zero ? 0x80000000u : 0);
            if (!zero) for (u32 i = start; i < in; ++i) words[out++] = source[i];
        }
        offset = used; size = out - used; used = out;
        return true;
    }
    template <class Sink> bool visit(u32 offset, u32 size, u32 expected, Sink sink) const {
        if (used > Capacity || offset > used || size > used - offset) return false;
        const u32 end = offset + size;
        u32 at = offset, index = 0;
        while (at < end) {
            const u32 token = words[at++], run = token & 0x7fffffffu;
            const bool zero = (token & 0x80000000u) != 0;
            if (!run || run > expected - index || (!zero && run > end - at)) return false;
            for (u32 i = 0; i < run; ++i)
                if (!sink(index++, zero ? 0 : words[at++])) return false;
        }
        return index == expected;
    }
    bool valid(u32 offset, u32 size, u32 expected) const {
        return visit(offset, size, expected, [](u32, u32) { return true; });
    }
};
}}
