#include <recording_container.hpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform::recording;

namespace {
Container container, before;
Selection selected;
bool active[capacity], used[capacity];
void reset(u32 occupied = capacity) {
    for (u32 i = 0; i < capacity; ++i) {
        container.slots[i] = {static_cast<i16>(i ? i - 1 : -1),
            static_cast<i16>(i + 1 == occupied || i + 1 == capacity ? -1 : i + 1),
            {static_cast<i32>(i * 480), i * 17, static_cast<u8>(i), 100, 0, 0}};
        active[i] = selected.slots[i] = false;
    }
    container.free = occupied == capacity ? -1 : static_cast<i16>(occupied);
    container.head = occupied ? 0 : -1;
    container.tail = occupied ? static_cast<i16>(occupied - 1) : -1;
    container.count = static_cast<i16>(occupied);
}
bool unchanged() {
    const u8* a = reinterpret_cast<const u8*>(&container);
    const u8* b = reinterpret_cast<const u8*>(&before);
    for (u32 i = 0; i < sizeof(Container); ++i) if (a[i] != b[i]) return false;
    return true;
}
bool payloads_unchanged() {
    for (u32 i = 0; i < capacity; ++i) {
        const u8* a = reinterpret_cast<const u8*>(&container.slots[i].note);
        const u8* b = reinterpret_cast<const u8*>(&before.slots[i].note);
        for (u32 j = 0; j < sizeof(Note); ++j) if (a[j] != b[j]) return false;
    }
    return true;
}
}

int main() {
    u32 removed = 99;
    // Every single removal, including head/tail, preserves stable payloads.
    for (u32 slot = 0; slot < capacity; ++slot) {
        reset(); before = container; selected.slots[slot] = true;
        CHECK(erase(container, selected, active, removed));
        CHECK(removed == 1 && container.count == 119 && container.free == static_cast<i16>(slot));
        CHECK(container.slots[slot].next == -1 && valid(container, used) && !used[slot]);
        CHECK(payloads_unchanged());
    }
    reset(); before = container;
    for (u32 i = 0; i < capacity; i += 2) selected.slots[i] = true;
    CHECK(erase(container, selected, active, removed) && removed == 60 && valid(container, used));
    CHECK(payloads_unchanged());
    for (u32 i = 0; i < capacity; ++i) selected.slots[i] = used[i];
    CHECK(erase(container, selected, active, removed) && removed == 60 && valid(container, used));
    CHECK(container.count == 0 && container.head == -1 && container.tail == -1);

    // Reject the whole batch when even its last selection is unsafe.
    reset(); selected.slots[0] = selected.slots[119] = true; active[119] = true; before = container;
    CHECK(!erase(container, selected, active, removed) && removed == 0 && unchanged());
    active[119] = false; container.slots[119].note.flags = 2; before = container;
    CHECK(!erase(container, selected, active, removed) && unchanged());
    reset(20); selected.slots[0] = selected.slots[20] = true; before = container;
    CHECK(!erase(container, selected, active, removed) && unchanged());

    // Empty, malformed, cyclic and overlapping partitions fail without writes.
    reset(0); CHECK(valid(container, used));
    CHECK(erase(container, selected, active, removed) && removed == 0);
    for (u32 failure = 0; failure < 9; ++failure) {
        reset(20); selected.slots[0] = true;
        switch (failure) {
            case 0: container.head = 120; break;
            case 1: container.slots[3].next = 0; break;
            case 2: container.slots[3].previous = 0; break;
            case 3: container.tail = 18; break;
            case 4: container.count = 19; break;
            case 5: container.free = 0; break;
            case 6: container.slots[21].next = 20; break;
            case 7: container.slots[21].next = -2; break;
            case 8: container.free = -1; break;
        }
        before = container;
        CHECK(!erase(container, selected, active, removed) && removed == 0 && unchanged());
    }
    reset(); before = container;
    CHECK(!erase(container, selected, nullptr, removed) && unchanged());
    CHECK(!valid(container, nullptr));
    return 0;
}
