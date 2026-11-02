#pragma once
#include <opxy/arranger.hpp>

// .oxyl codec only, with no file I/O or firmware hooks. Unknown .oxy sections stay untouched.
// Runtime decode uses distinct caller-owned scratch; the convenience overload uses a stack State.
namespace opxy { namespace arranger { namespace storage {

const u32 header_bytes = 64;
const u32 clip_bytes = 24;
const u32 max_bytes = header_bytes + Model::capacity * clip_bytes;

struct Binding {
    u32 profile;
    u32 time_units;  // Adapter-defined version, not an assumed PPQ.
    u32 project_bytes;
    u32 project_crc32;
};

struct State {
    Model model;
    bool enabled = false;
    u8 track = 0;
};

inline u32 read32(const u8* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 |
        static_cast<u32>(p[2]) << 16 | static_cast<u32>(p[3]) << 24;
}

inline void write32(u8* p, u32 value) {
    for (u32 i = 0; i < 4; ++i) p[i] = static_cast<u8>(value >> (i * 8));
}

inline u32 crc32(u32 crc, const u8* data, u32 length) {
    crc = ~crc;
    while (length--) {
        crc ^= *data++;
        for (u32 bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320u : 0);
    }
    return ~crc;
}

// Checksum covers header and records, excluding the checksum word at byte 60.
inline u32 checksum(const u8* data, u32 length) {
    return crc32(crc32(0, data, 60), data + header_bytes, length - header_bytes);
}

inline bool validBinding(const Binding& value) {
    return value.profile && value.time_units && value.project_bytes;
}

// Return zero without writing on invalid metadata or insufficient output capacity.
inline u32 encode(const State& state, const Binding& binding, u8* output, u32 available) {
    if (!output || state.track >= 8 || !validBinding(binding)) return 0;
    u32 count = 0;
    for (u32 slot = 0; slot < Model::capacity; ++slot) if (state.model.clip(slot)) ++count;
    const u32 length = header_bytes + count * clip_bytes;
    if (available < length) return 0;
    const u8 magic[8] = {'O', 'X', 'Y', 'L', 13, 10, 26, 10};
    for (u32 i = 0; i < 8; ++i) output[i] = magic[i];
    write32(output + 8, 3);
    write32(output + 12, length);
    write32(output + 16, binding.profile);
    write32(output + 20, binding.time_units);
    write32(output + 24, binding.project_bytes);
    write32(output + 28, binding.project_crc32);
    write32(output + 32, (state.enabled ? 1u : 0u) | (state.model.loopEnabled() ? 2u : 0u));
    write32(output + 36, state.model.position());
    write32(output + 40, state.model.loopStart());
    write32(output + 44, state.model.loopEnd());
    write32(output + 48, state.track);
    write32(output + 52, count);
    write32(output + 56, 0);
    u32 offset = header_bytes;
    for (u32 slot = 0; slot < Model::capacity; ++slot) {
        const Clip* clip = state.model.clip(slot);
        if (!clip) continue;
        output[offset] = static_cast<u8>(slot);
        output[offset + 1] = static_cast<u8>(slot >> 8);
        output[offset + 2] = clip->track;
        output[offset + 3] = clip->pattern;
        write32(output + offset + 4, clip->start);
        write32(output + offset + 8, clip->length);
        write32(output + offset + 12, clip->offset);
        write32(output + offset + 16, clip->source_length);
        // Schema 3 keeps 24-byte records: bit31 is per-instance mute;
        // the remaining bits retain the bounded fractional phase.
        write32(output + offset + 20, clip->phase_remainder | (clip->muted ? 0x80000000u : 0));
        offset += clip_bytes;
    }
    write32(output + 60, checksum(output, length));
    return length;
}

// The destination changes only after complete validation; playback is not represented.
inline bool decode(const u8* input, u32 length, const Binding& expected, State& destination, State& candidate) {
    if (!input || length < header_bytes || length > max_bytes || !validBinding(expected)) return false;
    const u8 magic[8] = {'O', 'X', 'Y', 'L', 13, 10, 26, 10};
    for (u32 i = 0; i < 8; ++i) if (input[i] != magic[i]) return false;
    const u32 version = read32(input + 8), count = read32(input + 52);
    const u32 record_bytes = version == 1 ? 20 : clip_bytes;
    if ((version != 1 && version != 2 && version != 3) || read32(input + 12) != length || count > Model::capacity ||
        length != header_bytes + count * record_bytes || read32(input + 56) != 0 ||
        read32(input + 16) != expected.profile || read32(input + 20) != expected.time_units ||
        read32(input + 24) != expected.project_bytes || read32(input + 28) != expected.project_crc32 ||
        read32(input + 60) != checksum(input, length)) return false;
    const u32 flags = read32(input + 32), track = read32(input + 48);
    if (flags > 3 || track >= 8) return false;
    // Caller-owned scratch avoids a full Model on the firmware worker stack.
    if (&candidate == &destination) return false;
    static const State blank;
    candidate = blank;
    candidate.enabled = (flags & 1) != 0;
    candidate.track = static_cast<u8>(track);
    candidate.model.seek(read32(input + 36));
    if (!candidate.model.setLoop((flags & 2) != 0, read32(input + 40), read32(input + 44))) return false;
    u32 previous = 0;
    for (u32 i = 0; i < count; ++i) {
        const u8* record = input + header_bytes + i * record_bytes;
        const u32 slot = static_cast<u32>(record[0]) | static_cast<u32>(record[1]) << 8;
        const u32 phase_flags = version == 1 ? 0 : read32(record + 20);
        const u32 phase = version == 3 ? phase_flags & 0x7fffffffu : phase_flags;
        const bool muted = version == 3 && (phase_flags & 0x80000000u) != 0;
        const Clip clip = {record[2], record[3], read32(record + 4), read32(record + 8),
            read32(record + 12), read32(record + 16), phase, muted};
        if ((i && slot <= previous) || !candidate.model.insertAt(slot, clip)) return false;
        previous = slot;
    }
    destination = candidate;
    return true;
}

inline bool decode(const u8* input, u32 length, const Binding& expected, State& destination) {
    State scratch;
    return decode(input, length, expected, destination, scratch);
}

}}}
