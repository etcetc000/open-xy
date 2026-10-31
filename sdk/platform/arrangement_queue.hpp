#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
template<class T> struct ArrangementMailbox {
    T value;
    volatile u32 pending;
    template<class Fence> bool put(const T& next, Fence fence) {
        if (pending) return false;
        value = next; fence(); pending = 1; return true;
    }
    template<class Fence> bool take(T& next, Fence fence) {
        if (!pending) return false;
        fence(); next = value; fence(); pending = 0; return true;
    }
};
// One controller producer, one GUI consumer. Reserve a release slot for every
// captured press; encoder traffic cannot consume these reservations.
struct ArrangementEvent { u32 generation, identity, track, key; i32 value; bool turn, shifted; };
template<u32 Capacity> struct ArrangementQueue {
    ArrangementEvent events[Capacity];
    volatile u32 read, written;
    u32 reserved; // producer only
    template<class Fence> bool push(const ArrangementEvent& event, bool release, Fence fence) {
        const u32 used = written - read;
        const u32 needed = release ? 1 : event.turn ? reserved + 1 : reserved + 2;
        if (used >= Capacity || needed > Capacity - used) return false;
        events[written % Capacity] = event;
        if (release) { if (reserved) --reserved; }
        else if (!event.turn) ++reserved;
        fence(); ++written;
        return true;
    }
    template<class Fence> bool pop(ArrangementEvent& event, Fence fence) {
        if (read == written) return false;
        fence(); event = events[read % Capacity]; fence(); ++read;
        return true;
    }
};
}}

namespace opxy { namespace platform {
struct ArrangementPublication { u32 generation, identity, page, enabled, visible; };
struct ArrangementPublicationSlot {
    volatile u32 sequence;
    ArrangementPublication value;
    template<class Fence> void publish(const ArrangementPublication& next, Fence fence) {
        ++sequence; fence(); value = next; fence(); ++sequence;
    }
    template<class Fence> bool read(ArrangementPublication& out, Fence fence) const {
        for (u32 attempt = 0; attempt < 2; ++attempt) {
            const u32 before = sequence;
            if (before & 1) return false;
            fence(); out = value; fence();
            if (before == sequence) return true;
        }
        return false;
    }
};
}}
