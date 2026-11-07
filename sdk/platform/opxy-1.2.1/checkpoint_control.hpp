#pragma once
#include "checkpoint.hpp"
#include "track_control.hpp"

namespace opxy { namespace platform { namespace checkpoint {
namespace kept {
// A RAM-only stream bound to this build's immutable control-machine table.
// Words are copied explicitly; no file format or contributor addresses escape.
struct Stream {
    u8* destination;
    const u8* expected;
    u32 capacity, at;

    bool transfer(const u8* data, u32 bytes) {
        if (bytes > capacity - at) return false;
        if (destination)
            for (u32 i = 0; i < bytes; ++i) destination[at + i] = data[i];
        else
            for (u32 i = 0; i < bytes; ++i) if (expected[at + i] != data[i]) return false;
        at += bytes;
        return true;
    }

    bool put(u32 value) {
        u8 bytes[4];
        for (u32 i = 0; i < 4; ++i) bytes[i] = static_cast<u8>(value >> (8 * i));
        return transfer(bytes, 4);
    }
};
inline u32 get(const u8* data, u32& at) {
    u32 value = 0;
    for (u32 i = 0; i < 4; ++i) value |= static_cast<u32>(data[at++]) << (8 * i);
    return value;
}
inline bool transfer(Stream& stream, const PatternRef* patterns, u32 count) {
    if (stream.capacity < 8) return false;
    // Each machine serializes into one reusable buffer. Snapshot payloads use
    // only the bytes the selected machines actually return.
    static u8 scratch[control::saved_capacity];
    stream.at = 8;
    u32 machines = 0, aux = 0;
    for (u32 i = 0; i < sdk_control_machine_count; ++i) {
        const ControlMachine& m = sdk_control_machines[i];
        if (!m.save || !m.load || m.ready() != 1) continue;
        if (m.saved_bytes > sizeof scratch) return false;
        const u32 n = m.save(scratch);
        if (n > m.saved_bytes || !stream.put(i) || !stream.put(n) || !stream.transfer(scratch, n)) return false;
        ++machines;
    }
    if (sdk_aux.kept && sdk_aux.restore)
        for (u32 i = 0; i < count; ++i) {
            const PatternRef& p = patterns[i];
            if (p.track < control::tracks) continue;
            const i32* values = sdk_aux.kept(p.track, p.index);
            if (!stream.put(p.track) || !stream.put(p.index) || !stream.put(values ? 1 : 0)) return false;
            for (u32 k = 0; k < 8; ++k)
                if (!stream.put(values ? static_cast<u32>(values[k]) : 0)) return false;
            ++aux;
        }
    Stream header = stream;
    header.at = 0;
    return header.put(machines) && header.put(aux);
}
inline bool save(u8* data, u32& bytes, u32 capacity, const PatternRef* patterns, u32 count) {
    Stream stream = {data, nullptr, capacity, 0};
    if (!transfer(stream, patterns, count)) return false;
    bytes = stream.at;
    return true;
}
inline bool matches(const u8* data, u32 bytes, const PatternRef* patterns, u32 count) {
    Stream stream = {nullptr, data, bytes, 0};
    return transfer(stream, patterns, count) && stream.at == bytes;
}
inline bool validate(const u8* data, u32 bytes) {
    if (bytes < 8) return false;
    u32 at = 0;
    const u32 machines = get(data, at), aux = get(data, at);
    if (machines > sdk_control_machine_count || aux > 64) return false;
    for (u32 i = 0; i < machines; ++i) {
        if (bytes - at < 8) return false;
        const u32 index = get(data, at), n = get(data, at);
        if (index >= sdk_control_machine_count || n > bytes - at) return false;
        const ControlMachine& m = sdk_control_machines[index];
        if (!m.load || !m.save || m.ready() != 1 || n > m.saved_bytes) return false;
        at += n;
    }
    if (aux && !sdk_aux.restore) return false;
    if (bytes - at != aux * 44) return false;
    for (u32 i = 0; i < aux; ++i) {
        const u32 track = get(data, at), pattern = get(data, at), present = get(data, at);
        if (track < 8 || track >= 16 || pattern >= 16 || present > 1) return false;
        at += 32;
    }
    return at == bytes;
}
inline void load(const u8* data, u32) {
    sdk_control_forget();
    u32 at = 0;
    const u32 machines = get(data, at), aux = get(data, at);
    for (u32 i = 0; i < machines; ++i) {
        const u32 index = get(data, at), n = get(data, at);
        const ControlMachine& m = sdk_control_machines[index];
        control_loaded(m);
        m.load(data + at, n);
        at += n;
    }
    for (u32 i = 0; i < aux; ++i) {
        const u32 track = get(data, at), pattern = get(data, at), present = get(data, at);
        i32 values[8];
        for (u32 k = 0; k < 8; ++k) values[k] = static_cast<i32>(get(data, at));
        sdk_aux.restore(track, pattern, present ? values : nullptr);
    }
}
}
}}}
