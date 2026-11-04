#pragma once
#include <opxy/types.hpp>

// Stock's per-track pattern note list: 120 linked slots, a free list, and the
// count stock reads at +0x786. Stock has no remove call; its editor unlinks a
// slot inline, and unlink() below does exactly that. SDK-only, controller path.
namespace opxy { namespace platform { namespace notes {

const u32 capacity = 120;
const u8 held_flag = 2;  // A note stock is still recording.
struct Note {
    i32 position;
    u32 length;
    u8 pitch, velocity, flags, padding;
};
struct Slot { i16 previous, next; Note note; };
struct Container { Slot slots[capacity]; i16 free, head, tail, count; };
static_assert(sizeof(Note) == 12 && sizeof(Slot) == 16, "Stock note slot layout");
static_assert(sizeof(Container) == 0x788 && __builtin_offsetof(Container, count) == 0x786,
              "Stock note list layout");

inline bool index(i32 value) { return value >= 0 && value < static_cast<i32>(capacity); }

// Validate the complete disjoint used/free partition before touching any slot.
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

// Whether clear() may run: the list is valid and holds no note stock is still
// recording. Check before opening an undo transaction.
inline bool clearable(const Container& container) {
    bool used[capacity];
    if (!valid(container, used)) return false;
    for (u32 i = 0; i < capacity; ++i)
        if (used[i] && (container.slots[i].note.flags & held_flag)) return false;
    return true;
}

// Remove every note, or none if the list is not clearable. Undo and
// publication belong to the caller.
inline bool clear(Container& container) {
    bool used[capacity];
    if (!clearable(container) || !valid(container, used)) return false;
    for (u32 i = 0; i < capacity; ++i)
        if (used[i]) unlink(container, i);
    return true;
}

}}}
