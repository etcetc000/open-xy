// Exercise selection -> stored Q31 value -> audio destination with the real
// routing code. Boundary doubles fail if the audio path asks a GUI getter.
#include "lfo.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#define require(v) do { if (!(v)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)
using namespace opxy;
using namespace opxy::platform;
static int ownership = 1;
static int ready() { return ownership; }
const LfoMachine sdk_lfo_machines[] = {
    {reinterpret_cast<const char*>(40), ready, 4, {0, 1, 2, 3}},
    {reinterpret_cast<const char*>(41), ready, 5, {0, 1, 2, 3, 3}}, // Braids shares white.
    {reinterpret_cast<const char*>(42), ready, 6, {0, 1, 2, 3, 2, 2}},
    {reinterpret_cast<const char*>(43), ready, 7, {0, 1, 2, 3, 1, 0, 2}},
    {reinterpret_cast<const char*>(44), ready, 8, {0, 1, 2, 3, 0, 1, 2, 3}},
};
const u32 sdk_lfo_machine_count = 5;
static u32 engine, kind, group, routed, gui_reads, delegated, expected_track;
static i32 amount;
static i32 icon_target;
static u32 named_group, named_slot, named_flag, shown_widget;
static bool audio;
static i32 stored, published;
static u32 value_reads, publications;
static u32 telemetry_slot, telemetry_writes;
namespace opxy { namespace platform { namespace lfo {
u32 gui_engine(u32 track) { require(!audio && track == expected_track); ++gui_reads; return engine; }
u32 audio_engine(u32 track) { require(audio && track == expected_track); return engine; }
u32 type(u32 track) { require(!audio && track == expected_track); return kind; }
u32 group_value(u32 track) { require(!audio && track == expected_track); return group; }
u32 selected_track() { require(!audio); return expected_track; }
u32 animate(u32 controller, u32 position, i32 target, u32 duration) {
    require(!audio && controller == 101 && position == 102 && duration == 153);
    icon_target = target;
    return 103;
}
const char* name(u32 group, u32 slot, u32 flag) {
    require(!audio);
    named_group = group; named_slot = slot; named_flag = flag;
    return "slot name";
}
void show(u32 widget) { require(!audio); shown_widget = widget; }
i32 step(u32, u32, i32 value, const i32*) { ++delegated; return value; }
i32 value(u32 project, u32 track, u32 index) {
    require(!audio && project == 123 && track == expected_track && index == (kind == 2 ? 31u : 27u));
    ++value_reads;
    return stored;
}
u32 publish(u32 project, u32 track, u32, i32 value) {
    require(!audio && project == 123 && track == expected_track);
    ++publications; published = value;
    return 456;
}
void modulation_slot(u32 index) {
    require(audio);
    telemetry_slot = index;
    ++telemetry_writes;
}
void modulate(u32 project, u32 track, u32 index, i32 value) {
    require(audio && project == 123 && track == expected_track);
    routed = index; amount = value;
}
}}}
static void route(u32 track, i32 value, u32 expected) {
    audio = true;
    const u32 before = gui_reads;
    const u32 writes = telemetry_writes;
    const u32 stock_slot = static_cast<u32>(value) >> 29;
    telemetry_slot = stock_slot; // the stock store immediately before the hook
    sdk_lfo_modulate(123, track, static_cast<u32>(value) >> 29, -1234, static_cast<u32>(value) >> 8);
    require(routed == expected && amount == -1234 && gui_reads == before);
    require(telemetry_slot == expected);
    const bool owned = track < 8 && engine >= 41 && engine <= 44 && ownership == 1;
    require(telemetry_writes == writes + (owned ? 1 : 0));
    // Stock packs the slot into the high word, and the GUI matches it against
    // raw parameter slots before the engine page maps them to shown encoders.
    const u32 packed = (telemetry_slot << 16) | 0x1234;
    for (u32 slot = 0; slot < 8; ++slot)
        require((slot == (packed >> 16)) == (slot == expected));
    audio = false;
}
int main() {
    const i32 up[] = {0, 1, 0}, down[] = {0, -1, 0};
    for (u32 track = 0; track < 8; ++track) {
        expected_track = track;
        for (u32 count = 5; count <= 8; ++count) {
            engine = 36 + count;
            for (u32 lfo_type : {1u, 2u, 3u}) {
                kind = lfo_type; group = 0;
                const u32 index = kind == 2 ? 31 : 27;
                require(sdk_lfo_range(track, 0, 0) == 1);
                require(sdk_lfo_range(track, 1, 1) == 0);
                i32 value = 0;
                for (u32 slot = 0; slot < count; ++slot) {
                    if (slot) value = sdk_lfo_step(track, index, value, up);
                    require(static_cast<u32>(value) >> 28 == slot);
                    route(track, value, slot);
                    require(sdk_lfo_icon_slide(101, 102, -13312 * static_cast<i32>(slot), 153, 0, slot) == 103);
                    const u32 control = sdk_lfo_machines[count - 4].controls[slot];
                    require(icon_target == -13312 * static_cast<i32>(control));
                    require(sdk_lfo_icon_slot(slot, 1) == control);
                    require(sdk_lfo_icon_slot(slot, 0) == slot);
                    stored = value;
                    const u32 reads = value_reads, writes = publications;
                    require(sdk_lfo_cycle(123, track, index, 0x70000000) == 456);
                    require(value_reads == reads + 1 && publications == writes + 1);
                    require(static_cast<u32>(published) == (((slot + 1) % count) << 28) + 0x08000000u);
                    route(track, published, (slot + 1) % count);
                    shown_widget = 0;
                    require(sdk_lfo_element_label(0, slot, 1, 104) != nullptr);
                    require(named_group == 0 && named_slot == slot && named_flag == 1);
                    require(sdk_lfo_label(0, slot, 1) != nullptr && named_slot == slot);
                    require(shown_widget == (slot >= 4 ? 104u : 0u));
                }
                value = sdk_lfo_step(track, index, value, up);
                require(static_cast<u32>(value) >> 28 == count - 1);
                for (u32 slot = count - 1; slot > 0; --slot) {
                    value = sdk_lfo_step(track, index, value, down);
                    require(static_cast<u32>(value) >> 28 == slot - 1);
                    route(track, value, slot - 1);
                }
                require(static_cast<u32>(sdk_lfo_step(track, index, value, down)) >> 28 == 0);
                const i32 fast[] = {0, 1, 1}, huge[] = {0, 0x7fffffff, 1}, negative[] = {0, (-2147483647 - 1), 1};
                require(static_cast<u32>(sdk_lfo_step(track, index, 0, fast)) >> 28 == 2);
                require(static_cast<u32>(sdk_lfo_step(track, index, 0, huge)) >> 28 == count - 1);
                require(static_cast<u32>(sdk_lfo_step(track, index, 0x7fffffff, negative)) >> 28 == 0);
                route(track, 0x7fffffff, count - 1); // a saved destination from a larger machine
                stored = 0x7fffffff;
                sdk_lfo_cycle(123, track, index, 123);
                require(published == 0x08000000); // recover an out-of-range saved slot
                const u32 calls = delegated;
                sdk_lfo_step(track, index == 27 ? 31 : 27, 123, up);
                sdk_lfo_step(track, index, 123, nullptr);
                group = 0x7fffffff;
                sdk_lfo_step(track, index, 123, up);
                require(delegated == calls + 3);
                const u32 reads = value_reads;
                sdk_lfo_cycle(123, track, index, 123);
                require(published == 123 && value_reads == reads);
                group = 0;
                sdk_lfo_cycle(123, track, index == 27 ? 31 : 27, 123);
                require(published == 123 && value_reads == reads);
            }
        }
    }
    expected_track = 0; group = 0; kind = 1;
    for (u32 id : {2u, 3u, 18u, 33u, 40u, 44u, 62u}) {
        engine = id;
        for (int state : {0, 1, 2, -1}) {
            ownership = state;
            const bool owned = id == 44 && state == 1;
            require(sdk_lfo_range(0, 0, 1) == static_cast<u32>(owned));
            const u32 calls = delegated;
            sdk_lfo_step(0, 27, 0, up);
            require(delegated == calls + (owned ? 0 : 1));
            stored = 0x78000000;
            const u32 reads = value_reads;
            sdk_lfo_cycle(123, 0, 27, 123);
            require(published == (owned ? 0x08000000 : 123) && value_reads == reads + (owned ? 1 : 0));
            route(0, 0x78000000, owned ? 7 : 3);
            sdk_lfo_icon_slide(101, 102, -7000, 153, 0, 7);
            require(icon_target == (owned ? -3000 : -7000));
            shown_widget = 0;
            sdk_lfo_element_label(0, 7, 1, 104);
            require(shown_widget == (owned ? 104u : 0u) && named_slot == 7);
        }
    }
    engine = 44; ownership = 1;
    for (u32 other_group : {1u, 2u, 3u, 0xffffffffu}) {
        sdk_lfo_icon_slide(101, 102, -7000, 153, other_group, 7);
        require(icon_target == -7000);
        shown_widget = 0;
        sdk_lfo_element_label(other_group, 7, 0, 104);
        require(shown_widget == 0 && named_group == other_group && named_slot == 7 && named_flag == 0);
    }
    // Retained selectors from a larger machine must name, animate and
    // modulate the same last declared slot on the new machine.
    for (u32 count = 5; count <= 7; ++count) {
        engine = 36 + count;
        const u32 last = count - 1;
        const u32 control = sdk_lfo_machines[count - 4].controls[last];
        for (u32 slot = count; slot < 8; ++slot) {
            require(sdk_lfo_icon_slot(slot, 1) == control);
            sdk_lfo_icon_slide(101, 102, -1000 * static_cast<i32>(slot), 153, 0, slot);
            require(icon_target == -1000 * static_cast<i32>(control));
            shown_widget = 0;
            sdk_lfo_element_label(0, slot, 1, 104);
            require(shown_widget == 104 && named_slot == last);
            sdk_lfo_label(0, slot, 1);
            require(named_slot == last);
            route(0, static_cast<i32>((slot << 28) + 0x08000000u), last);
        }
    }
    engine = 41;
    for (u32 slot : {8u, 0xffffffffu}) {
        require(sdk_lfo_icon_slot(slot, 1) == slot);
        sdk_lfo_icon_slide(101, 102, -7000, 153, 0, slot);
        require(icon_target == -7000);
        shown_widget = 0;
        sdk_lfo_element_label(0, slot, 1, 104);
        require(shown_widget == 0 && named_slot == slot);
        sdk_lfo_label(0, slot, 1);
        require(named_slot == slot);
    }
    engine = 44;
    for (u32 other : {0u, 4u, 5u, 6u, 7u}) {
        kind = other;
        const u32 calls = delegated;
        sdk_lfo_step(0, 27, 123, up); sdk_lfo_step(0, 31, 123, up);
        require(delegated == calls + 2);
        const u32 reads = value_reads;
        sdk_lfo_cycle(123, 0, 27, 123); sdk_lfo_cycle(123, 0, 31, 123);
        require(published == 123 && value_reads == reads);
    }
    for (u32 track : {8u, 10u, 14u, 15u, 0xffffffffu}) {
        expected_track = track;
        require(sdk_lfo_range(track, 0, 0) == 0);
        const u32 calls = delegated;
        sdk_lfo_step(track, 27, 123, up);
        require(delegated == calls + 1);
        const u32 reads = value_reads;
        sdk_lfo_cycle(123, track, 27, 123);
        require(published == 123 && value_reads == reads);
        route(track, 0x78000000, 3);
        sdk_lfo_icon_slide(101, 102, -7000, 153, 0, 7);
        require(icon_target == -7000);
    }
}
