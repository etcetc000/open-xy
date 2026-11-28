#pragma once
#include "queue.hpp"
#include "ring_cursors.hpp"

// Only include in the authenticated XY 1.2.1 native adapter. This header neither
// installs hooks nor accesses a unit from a host build.
#if defined(OPXY_AUDIO_TRANSPORT_121)
#if !defined(__bfin__)
#error OPXY_AUDIO_TRANSPORT_121 telemetry binding requires Blackfin
#endif
namespace opxy_transport { namespace telemetry {
static_assert(sizeof(void*) == 4, "XY stock ABI requires 32-bit pointers");
inline u32 xy_enter(void*) {
    u32 mask;
    __asm__ __volatile__("CLI %0;" : "=d"(mask) : : "memory");
    return mask;
}
inline void xy_leave(void*, u32 mask) {
    __asm__ __volatile__("STI %0;" : : "d"(mask) : "memory");
}
inline Critical xy_critical() { return Critical{0, xy_enter, xy_leave}; }
inline u32 xy_now_ms() {
    // Authenticated stock timer converts CYCLES using its configured clock,
    // then divides microseconds by 1000. ABI returns low/high in R0/R1.
    typedef unsigned long long (*Elapsed)(u32, u32);
    return u32(reinterpret_cast<Elapsed>(0x8d5be290u)(0, 0));
}
class XYQueue {
    volatile u8* object_;
    static volatile u8* endpoint() { return reinterpret_cast<volatile u8*>(0x841460b4u); }
    static u32 word(volatile u8* base, unsigned offset) {
        return *reinterpret_cast<volatile u32*>(base + offset);
    }
public:
    XYQueue() : object_(0) {}
    u32 enter() { return xy_enter(0); }
    void leave(u32 token) { xy_leave(0, token); }
    QueueState snapshot() {
        QueueState s = {};
        u32 pointer = word(endpoint(), 0x3f4f8);
        // The authenticated object belongs to the static intercore endpoint;
        // do not dereference arbitrary values during early initialization.
        if (pointer != 0x84185824u) return s;
        object_ = reinterpret_cast<volatile u8*>(pointer);
        s.vtable = word(object_, 0);
        if (s.vtable != 0x0803f134u) return s;
        s.read = word(object_, 0x10); s.write = word(object_, 0x14);
        s.capacity = word(object_, 0x18); s.batch = object_[0x172];
        opxy_transport::telemetry_ring_cursors::normalize_completed_read(s);
        s.initialized = object_[0x0c] != 0 && endpoint()[0x3f4dd] != 0 && word(endpoint(), 0x3f4f0) != 1;
        s.normal_opcode = (object_[0x38 + (0xb5 >> 3)] & (1u << (0xb5 & 7))) == 0;
        bool gui = *reinterpret_cast<volatile u8*>(0x86c05ca4u) != 0;
        s.routes_valid = endpoint()[0x3f4ee] == (gui ? 1 : 5) && endpoint()[0x3f4ef] == (gui ? 5 : 1);
        return s;
    }
    void batch(u8 value) { object_[0x172] = value; }
    void send(const Packet& p) {
        typedef void (*Sender)(u32, u32, const u8*, u32, u32, u32, u32);
        reinterpret_cast<Sender>(0x11a00ec0u)(0x841460b4u, 0xb5, p.bytes, p.size, 0, 255, 255);
    }
    u32 write_position() { return word(object_, 0x14); }
    void flush() {
        typedef void (*Service)(volatile u8*);
        reinterpret_cast<Service>(0x8c043a24u)(object_);
    }
};
inline bool xy_send(void* context, const Packet& packet) {
    return try_send(*static_cast<XYQueue*>(context), packet);
}
}}
#endif
