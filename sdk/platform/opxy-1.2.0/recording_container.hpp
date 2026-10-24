#pragma once
#include <opxy/recording_policy.hpp>

namespace opxy { namespace platform { namespace recording {

const u32 capacity = 120;
struct Note {
    i32 position;
    u32 length;
    u8 pitch, velocity, flags, padding;
};
struct Slot { i16 previous, next; Note note; };
struct Container { Slot slots[capacity]; i16 free, head, tail, count; };
using Selection = extensions::RecordingSelection;
static_assert(sizeof(Note) == 12 && sizeof(Slot) == 16, "Stock note slot layout");
static_assert(sizeof(Container) == 0x788 && __builtin_offsetof(Container, count) == 0x786,
              "Stock note list layout");

inline bool index(i32 value) { return value >= 0 && value < static_cast<i32>(capacity); }

// Validate the complete disjoint used/free partition before exposing any slot.
// All loops are bounded even if stock memory contains malformed links.
inline bool valid(const Container& container, bool* used) {
    if (!used || container.count < 0 || container.count > static_cast<i32>(capacity)) return false;
    for (u32 i = 0; i < capacity; ++i) used[i] = false;
    i32 count = 0, previous = -1;
    for (i32 i = container.head; i != -1;) {
        if (!index(i) || used[i] || container.slots[i].previous != previous) return false;
        used[i] = true;
        previous = i;
        i = container.slots[i].next;
        if (++count > static_cast<i32>(capacity)) return false;
    }
    if (count != container.count || previous != container.tail) return false;
    bool free_seen[capacity];
    for (u32 i = 0; i < capacity; ++i) free_seen[i] = false;
    for (i32 i = container.free; i != -1;) {
        if (!index(i) || used[i] || free_seen[i]) return false;
        free_seen[i] = true;
        i = container.slots[i].next;
        if (++count > static_cast<i32>(capacity)) return false;
    }
    return count == static_cast<i32>(capacity);
}

// Equivalent to the stock editor's inline unlink: preserve payloads and every
// remaining slot address. The caller has already validated this used slot.
inline void unlink(Container& container, u32 slot) {
    Slot& entry = container.slots[slot];
    if (entry.previous == -1) container.head = entry.next;
    else container.slots[entry.previous].next = entry.next;
    if (entry.next == -1) container.tail = entry.previous;
    else container.slots[entry.next].previous = entry.previous;
    entry.next = container.free;
    container.free = static_cast<i16>(slot);
    --container.count;
}

// SDK-only controller-path operation. Independently build active[] from stock
// recorder payload pointers, and validate context/generations before this call.
// Validate the entire mask before the first write; rejection changes no bytes.
// Undo and publication belong to the caller and happen once per transaction.
inline bool erase(Container& container, const Selection& selected, const bool* active, u32& removed) {
    removed = 0;
    bool used[capacity];
    if (!active || !valid(container, used)) return false;
    for (u32 i = 0; i < capacity; ++i)
        if (selected.slots[i] && (!used[i] || active[i] || (container.slots[i].note.flags & 2)))
            return false;
    for (u32 i = 0; i < capacity; ++i) {
        if (!selected.slots[i]) continue;
        unlink(container, i);
        ++removed;
    }
    return true;
}

}}}
