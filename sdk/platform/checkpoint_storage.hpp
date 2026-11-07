#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// Bounded word runs for RAM-only snapshots. A token is a nonzero run length;
// its high bit selects zeros, otherwise the following words are literals.
// Exhaustion leaves the committed prefix and `used` unchanged.
struct CheckpointStorage {
    u32* words;
    u32 capacity, used;

    static u32 encoded_size(const u32* source, u32 count) {
        u32 size = 0;
        bool zero = false;
        for (u32 i = 0; i < count; ++i) {
            const bool next_zero = source[i] == 0;
            if (!i || zero != next_zero) ++size;
            if (!next_zero) ++size;
            zero = next_zero;
        }
        return size;
    }

    bool append(const u32* source, u32 count, u32& offset, u32& size) {
        u32 in = 0, out = used;
        while (in < count) {
            const bool zero = source[in] == 0;
            const u32 start = in;
            while (in < count && (source[in] == 0) == zero) ++in;
            const u32 run = in - start;
            if (out >= capacity || (!zero && run > capacity - out - 1)) return false;
            words[out++] = run | (zero ? 0x80000000u : 0);
            if (!zero) for (u32 i = start; i < in; ++i) words[out++] = source[i];
        }
        offset = used; size = out - used; used = out;
        return true;
    }
    struct Reader {
        const u32* at;
        u32 words_left, values_left, run;
        bool zero;

        bool next(u32& value) {
            if (!values_left) return false;
            if (!run) {
                if (!words_left) return false;
                const u32 token = *at++;
                --words_left;
                run = token & 0x7fffffffu;
                zero = (token & 0x80000000u) != 0;
                if (!run || run > values_left || (!zero && run > words_left)) return false;
            }
            --run;
            --values_left;
            value = 0;
            if (!zero) {
                value = *at++;
                --words_left;
            }
            return true;
        }
        bool done() const { return !values_left && !words_left && !run; }
    };

    bool reader(u32 offset, u32 size, u32 expected, Reader& out) const {
        if (used > capacity || offset > used || size > used - offset) return false;
        out = {words + offset, size, expected, 0, false};
        return true;
    }

    template <class Sink> bool visit(u32 offset, u32 size, u32 expected, Sink sink) const {
        Reader read;
        if (!reader(offset, size, expected, read)) return false;
        for (u32 i = 0; i < expected; ++i) {
            u32 value;
            if (!read.next(value) || !sink(i, value)) return false;
        }
        return read.done();
    }
    bool valid(u32 offset, u32 size, u32 expected) const {
        return visit(offset, size, expected, [](u32, u32) { return true; });
    }

    // Only commit a difference if it beats the full record's encoding. The
    // caller can fall back to a full record after a failed trial.
    bool append_difference(const u32* source, u32 count, Reader base, u32& offset, u32& size) {
        const u32 full = encoded_size(source, count);
        if (full <= 1 || used > capacity) return false;
        const u32 space = capacity - used;
        const u32 limit = used + (space < full ? space : full - 1);
        u32 out = used, token = out, run = 0;
        bool zero = false;
        for (u32 i = 0; i < count; ++i) {
            u32 value;
            if (!base.next(value)) return false;
            value ^= source[i];
            const bool next_zero = value == 0;
            if (!run || zero != next_zero) {
                if (run) words[token] = run | (zero ? 0x80000000u : 0);
                if (out == limit) return false;
                token = out++;
                run = 0;
                zero = next_zero;
            }
            ++run;
            if (!zero) {
                if (out == limit) return false;
                words[out++] = value;
            }
        }
        if (!base.done()) return false;
        words[token] = run | (zero ? 0x80000000u : 0);
        offset = used; size = out - used; used = out;
        return true;
    }
};
}}
