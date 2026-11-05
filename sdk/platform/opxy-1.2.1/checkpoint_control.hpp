#pragma once
#include "checkpoint.hpp"
#include "track_control.hpp"

namespace opxy { namespace platform { namespace checkpoint {
namespace kept {
// A RAM-only stream bound to this build's immutable control-machine table.
// Words are copied explicitly; no file format or contributor addresses escape.
inline void put(u8* data, u32& at, u32 value) {
    for (u32 i = 0; i < 4; ++i) data[at++] = static_cast<u8>(value >> (8 * i));
}
inline u32 get(const u8* data, u32& at) {
    u32 value = 0;
    for (u32 i = 0; i < 4; ++i) value |= static_cast<u32>(data[at++]) << (8 * i);
    return value;
}
inline bool save(u8* data, u32& bytes, u32 capacity, const PatternRef* patterns, u32 count) {
    bytes = 0;
    if (capacity < 8) return false;
    u32 machines = 0, aux = 0;
    put(data, bytes, 0); put(data, bytes, 0);
    for (u32 i = 0; i < sdk_control_machine_count; ++i) {
        const ControlMachine& m = sdk_control_machines[i];
        if (!m.save || !m.load || m.ready() != 1) continue;
        if (m.saved_bytes > capacity - bytes || capacity - bytes - m.saved_bytes < 8) return false;
        put(data, bytes, i);
        const u32 size_at = bytes;
        put(data, bytes, 0);
        const u32 n = m.save(data + bytes);
        if (n > m.saved_bytes) return false;
        u32 cursor = size_at; put(data, cursor, n);
        bytes += n;
        ++machines;
    }
    if (sdk_aux.kept && sdk_aux.restore)
        for (u32 i = 0; i < count; ++i) {
            const PatternRef& p = patterns[i];
            if (p.track < control::tracks) continue;
            const i32* values = sdk_aux.kept(p.track, p.index);
            if (capacity - bytes < 44) return false;
            put(data, bytes, p.track); put(data, bytes, p.index);
            put(data, bytes, values ? 1 : 0);
            for (u32 k = 0; k < 8; ++k) put(data, bytes, values ? static_cast<u32>(values[k]) : 0);
            ++aux;
        }
    u32 at = 0; put(data, at, machines); put(data, at, aux);
    return true;
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
