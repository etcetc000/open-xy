// Host contract checks for the compositor traversal (screen_order.cpp.in),
// simulating stock's loop: paint page R7 from [P4++], then the hooked advance.
#include <screen_order.cpp>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

static const opxy::u32 kSlots = 128;
static opxy::u32 owned_page, asked, substituted_from, substituted_to, substitutions;
static int ready = 1;
int sdk_screen_owned(opxy::u32 page) { ++asked; return page == owned_page ? ready : 0; }
static opxy::u32 substitute(opxy::u32 selected) { ++substitutions; return selected == substituted_from ? substituted_to : 0; }

// One frame; returns the pages in paint order and, in slot[], the slot each
// was painted from. selected changes after the first advance to check that
// the frame keeps its snapshot.
static unsigned frame(opxy::u32 selected, opxy::u32* order, opxy::u32* slot) {
    static opxy::u32 slots[kSlots];
    const opxy::u32 base = opxy::u32(reinterpret_cast<__UINTPTR_TYPE__>(slots));
    opxy::platform::ScreenCursor cursor{0, base};
    unsigned count = 0;
    for (;;) {
        require(count < kSlots && cursor.slot >= base && cursor.slot < base + kSlots * 4 && (cursor.slot - base) % 4 == 0);
        order[count] = cursor.page;
        slot[count++] = (cursor.slot - base) / 4;
        cursor.slot += 4;  // the loop head's post-increment
        const bool more = sdk_screen_next(&cursor, count == 1 ? selected : 127 - selected % 2) != 0;
        if (!more) break;
    }
    require(cursor.page == kSlots);
    return count;
}

int main() {
    opxy::u32 order[kSlots], slot[kSlots];
    // Owned selected pages, without a substitution.
    for (int state : {1, 0, 2}) {
        ready = state;
        for (opxy::u32 owned = 0; owned <= kSlots + 1; ++owned)
            for (opxy::u32 selected : {0u, 1u, 2u, 7u, 80u, 81u, 82u, 83u, 126u, 127u, 128u, 200u, owned}) {
                owned_page = owned;
                asked = 0;
                require(frame(selected, order, slot) == kSlots);
                require(asked <= 1);  // ownership is asked once per frame at most
                const bool promoted = ready == 1 && selected == owned && selected > 1 && selected < kSlots;
                // Every page exactly once from its own slot; the promoted page second, others ascending.
                unsigned expected = 0;
                for (unsigned i = 0; i < kSlots; ++i) {
                    require(slot[i] == order[i]);
                    if (promoted && i == 1) { require(order[i] == selected); continue; }
                    if (promoted && expected == selected) ++expected;
                    require(order[i] == expected++);
                }
            }
    }
    // A substitution: the selected page paints the substitute's slot in its
    // own place, and the substitute's own turn is left out.
    ready = 1;
    sdk_screen_substitute = substitute;
    for (opxy::u32 to : {0u, 1u, 2u, 46u, 84u, 127u, 128u, 300u})
        for (opxy::u32 selected : {0u, 1u, 2u, 46u, 84u, 127u}) {
            substituted_from = 46; substituted_to = to; owned_page = 84;
            substitutions = asked = 0;
            const unsigned count = frame(selected, order, slot);
            require(substitutions == 1 && asked <= 1);
            const bool active = selected == 46 && to > 1 && to < kSlots && to != 46;
            require(count == (active ? kSlots - 1 : kSlots));
            unsigned expected = 0;
            const bool promoted = !active && selected == 84;
            for (unsigned i = 0; i < count; ++i) {
                if (promoted && i == 1) { require(order[i] == 84 && slot[i] == 84); continue; }
                if (promoted && expected == 84) ++expected;
                if (active && expected == to) ++expected;
                require(order[i] == expected);
                require(slot[i] == (active && expected == 46 ? to : expected));
                ++expected;
            }
        }
    sdk_screen_substitute = nullptr;
    std::puts("screen order ok");
    return 0;
}
