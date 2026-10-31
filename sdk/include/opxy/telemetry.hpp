#pragma once
#include "types.hpp"

namespace opxy {
const u32 telemetry_capacity = 16;
inline i32 telemetry_lane(u32 word) { return (word & 0xffffu) < 0x8000u ? i32(word & 0xffffu) : i32(word & 0xffffu) - 65536; }
inline u32 telemetry_packed_add(u32 a, u32 b) {
    i32 low = telemetry_lane(a) + telemetry_lane(b);
    i32 high = telemetry_lane(a >> 16) + telemetry_lane(b >> 16);
    low = low < -32768 ? -32768 : low > 32767 ? 32767 : low;
    high = high < -32768 ? -32768 : high > 32767 ? 32767 : high;
    return (u32(low) & 0xffffu) | (u32(high) << 16);
}
// Owned producer storage, never a stock firmware pointer. Native GUI transport
// uses the explicit audioTransport extension; host consumers run between quanta. Authors
// publish a completed contribution, never retain/read shared pending state.
class AudioContext {
    volatile u32* words_;
    u32 count_;
    volatile u32* requested_;
    volatile u32* applied_;
    volatile u32* target_;
public:
    const u32 track;
    // One frame ID per audio quantum, shared by all voices. Zero is valid.
    const u32 frame_id;
    const bool frame_valid;
    // Borrowed only during an ordinary producer callback. Includes prepared
    // envelope/LFO amplitude, before velocity/default/note/output gain shaping.
    // Null means unavailable. Never retain this pointer after render returns.
    const i32* const prepared_amplitude64;
    AudioContext(u32 id, volatile u32* words, u32 count,
                 volatile u32* requested = nullptr, volatile u32* applied = nullptr, volatile u32* target = nullptr, u32 frame = 0, bool frame_is_valid = false, const i32* amplitude = nullptr)
        : words_(words), count_(count), requested_(requested), applied_(applied), target_(target), track(id), frame_id(frame), frame_valid(frame_is_valid), prepared_amplitude64(amplitude) {}
    u32 pending_command() const {
        if (!requested_ || !applied_) return 0;
        const u32 request = *requested_;
        return request != *applied_ ? request : 0;
    }
    u32 pending_target() const { return pending_command() ? (target_ ? *target_ : 1) : 0; }
    void command_applied(u32 command) const {
        if (requested_ && applied_ && command && command == *requested_) *applied_ = command;
    }
    void publish_word(u32 index, u32 value) const { if (index < count_) words_[index] = value; }
    // Publish a contiguous span of packed signed16 sample pairs. Bounds are
    // checked once; an invalid first index writes nothing and never wraps.
    // A null source publishes zeros. Otherwise the source must contain two
    // readable samples per returned word and needs only signed16 alignment.
    // Like publish_word, call only inside the owned serialized audio callback.
    u32 publish_packed16(u32 first, const i16* samples, u32 length) const {
        if (!words_ || first >= count_) return 0;
        const u32 available = count_ - first;
        const u32 count = length < available ? length : available;
        volatile u32* destination = words_ + first;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if (samples && !(reinterpret_cast<__UINTPTR_TYPE__>(samples) & 3u)) {
            // memcpy preserves aliasing rules for an int16 source. The tested
            // alignment permits native word loads; the fallback handles 2-byte
            // aligned buffers without widening their readable extent.
            samples = static_cast<const i16*>(__builtin_assume_aligned(samples, 4));
            for (u32 i = 0; i < count; ++i) {
                u32 packed;
                __builtin_memcpy(&packed, samples, sizeof packed);
                *destination++ = packed;
                samples += 2;
            }
        } else
#endif
        if (samples) {
            for (u32 i = 0; i < count; ++i) {
                const u32 packed = u32(u16(samples[0])) | (u32(u16(samples[1])) << 16);
                *destination++ = packed;
                samples += 2;
            }
        } else {
            for (u32 i = 0; i < count; ++i) *destination++ = 0;
        }
        return count;
    }

    void add_packed16(u32 index, u32 contribution) const {
        if (index < count_) words_[index] = telemetry_packed_add(words_[index], contribution);
    }
};

// One bank per machine, sized by its declaration. Native GUI code must not
// access this legacy bank. The native transport extension owns separate banks.
template <u32 Words> struct TelemetryBank {
    static_assert(Words <= telemetry_capacity, "At most sixteen telemetry words");
    volatile u32 tracks[8][Words > 0 ? Words : 1];
    AudioContext context(u32 track) {
        return track < 8 ? AudioContext(track, tracks[track], Words) : AudioContext(8, nullptr, 0);
    }
    u32 take(u32 track, u32* output) {
        for (u32 i = 0; i < telemetry_capacity; ++i) output[i] = 0;
        if (track >= 8) return 0;
        for (u32 i = 0; i < Words; ++i) {
            output[i] = tracks[track][i];
            tracks[track][i] = 0;
        }
        return Words;
    }
};
}
