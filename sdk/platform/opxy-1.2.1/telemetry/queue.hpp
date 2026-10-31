#pragma once
#include "protocol.hpp"

namespace opxy_transport { namespace telemetry {
struct QueueState {
    u32 vtable;
    u32 read;
    u32 write;
    u32 capacity;
    u8 batch;
    bool initialized;
    bool normal_opcode;
    bool routes_valid;
};
// Port contract: every operation except flush occurs under the local IRQ guard.
// snapshot() validates/caches the role-local object pointer; send() must use the
// authenticated stock normal packet path. No bank lock may enclose try_send().
template <class Port> bool try_send(Port& port, const Packet& packet) {
    Header h;
    if (!decode(packet.bytes, packet.size, h)) return false;
    u32 token = port.enter();
    QueueState s = port.snapshot();
    if (s.vtable != 0x0803f134u || !s.initialized || !s.normal_opcode || !s.routes_valid ||
        s.capacity != 0x8000u || s.read >= s.capacity || s.write >= s.capacity) {
        port.leave(token); return false;
    }
    u32 used = (s.write - s.read) & (s.capacity - 1);
    if (used + 256u >= s.capacity) { port.leave(token); return false; }
    port.batch(1);
    port.send(packet);
    u32 after = port.write_position();
    port.batch(s.batch);
    port.leave(token);
    bool queued = after != s.write;
    if (queued && !s.batch) port.flush();
    return queued;
}
}}
