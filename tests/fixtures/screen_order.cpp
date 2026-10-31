// Host contract checks for the compositor traversal (screen_order.cpp.in),
// simulating stock's loop: paint page R7 from [P4++], then the hooked advance.
#include <screen_order.cpp>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

static const opxy::u32 kSlots = 256;  // the relocated screen slots (stock_tables.SCREENS)
static opxy::u32 owned_page, asked, refreshes;
static int ready = 1;
int sdk_screen_owned(opxy::u32 page) { ++asked; return page == owned_page ? ready : 0; }
void sdk_send_refresh() { ++refreshes; }

// One frame; returns the pages in paint order. selected changes after the
// first advance to check that the frame keeps its snapshot.
static unsigned frame(opxy::u32 selected, opxy::u32* order) {
    static opxy::u32 slots[kSlots];
    const opxy::u32 base = opxy::u32(reinterpret_cast<__UINTPTR_TYPE__>(slots));
    opxy::platform::ScreenCursor cursor{0, base};
    unsigned count = 0;
    for (;;) {
        require(count < kSlots && cursor.slot == base + cursor.page * 4);
        order[count++] = cursor.page;
        cursor.slot += 4;  // the loop head's post-increment
        const bool more = sdk_screen_next(&cursor, count == 1 ? selected : 127 - selected % 2) != 0;
        if (!more) break;
    }
    require(cursor.page == kSlots);
    return count;
}

int main() {
    opxy::u32 order[kSlots];
    for (int state : {1, 0, 2}) {
        ready = state;
        for (opxy::u32 owned = 0; owned <= kSlots + 1; ++owned)
            for (opxy::u32 selected : {0u, 1u, 2u, 7u, 80u, 81u, 82u, 83u, 126u, 127u, 128u, 200u, 254u, 255u, 256u, 300u, owned}) {
                owned_page = owned;
                asked = refreshes = 0;
                require(frame(selected, order) == kSlots);
                require(asked <= 1);  // ownership is asked once per frame at most
                require(refreshes == 1);  // the sends' effect controls, once a frame
                const bool promoted = ready == 1 && selected == owned && selected > 1 && selected < kSlots;
                // Every page exactly once; the promoted page second, others ascending.
                unsigned expected = 0;
                for (unsigned i = 0; i < kSlots; ++i) {
                    if (promoted && i == 1) { require(order[i] == selected); continue; }
                    if (promoted && expected == selected) ++expected;
                    require(order[i] == expected++);
                }
            }
    }
    opxy::platform::ScreenActivation empty = {};
    require(empty.token(84, 0) == 0);
    ready = 1; owned_page = 84; frame(84, order);
    const auto first = sdk_screen_activation(84, 0);
    require(first != 0 && sdk_screen_activation(83, 7) == 0);
    // Multiple calls, background renderers and later frames preserve state.
    require(sdk_screen_activation(84, 0) == first);
    frame(84, order); require(sdk_screen_activation(84, 0) == first);
    const auto other_track = sdk_screen_activation(84, 1);
    require(other_track != first && other_track != 0);
    require(sdk_screen_activation(84, 1) == other_track);
    frame(2, order); require(sdk_screen_activation(84, 7) == 0);
    frame(84, order);
    const auto reopened = sdk_screen_activation(84, 1);
    require(reopened != other_track && reopened != 0);
    // Losing readiness and selecting another owned page are fresh activations.
    ready = 0; frame(84, order); require(sdk_screen_activation(84, 1) == 0);
    ready = 1; frame(84, order);
    const auto ready_again = sdk_screen_activation(84, 1);
    require(ready_again != reopened && ready_again != 0);
    owned_page = 85; frame(85, order);
    require(sdk_screen_activation(84, 1) == 0 && sdk_screen_activation(85, 1) != 0);
    owned_page = 84; frame(84, order);
    require(sdk_screen_activation(84, 1) != ready_again);
    opxy::platform::ScreenActivation wrap = {84, 0, 0xffffffffu, 1};
    require(wrap.token(84, 1) == 1);
    wrap.serial = 0xffffffffu; wrap.select(85);
    require(wrap.serial == 1 && wrap.token(85, 1) != 0);
    std::puts("screen order and page/track activation ok");
    return 0;
}
