#include <recording_menu.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
using namespace opxy::platform::recording_menu_detail;

namespace {
Vector menu;
Descriptor original[6], heap[7];
Parameter original_parameters[6];
u32 allocations, constructions, frees, sets;
bool enabled, out_of_memory, bad_constructor, reject_mode, bad;
const char* policy = "replace";
void* freed;
const char* rendered;

bool same_text(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
Descriptor* allocate(u32 bytes) {
    ++allocations;
    bad |= bytes != sizeof(heap);
    return out_of_memory ? nullptr : heap;
}
void release_memory(void* pointer) { ++frees; freed = pointer; }
void construct(Parameter* object, const char* name, i32 initial, i32 low, i32 high) {
    ++constructions;
    bad |= object != &parameter || name || initial || low || high != 1;
    *object = {0x0803f61c, {}, name, low * 1024, high * 1024, initial * 1024};
    if (bad_constructor) object->maximum = 2048;
}
void string_assign(void* out, const char* text) {
    bad |= out != &rendered;
    rendered = text;
}
void stock_menu() {
    const Address titles[] = {0x84009230, 0x8400923c, 0x8400924c, 0x84009254, 0x84009260, 0x84009270};
    const Address formats[] = {0x8c0996f0, 0x8c0996f0, 0x8c0995d0, 0x8c09863a, 0x8c098676, 0x8c0986aa};
    const Address setters[] = {0x8c09961c, 0x8c0996a6, 0x8c09862c, 0x8c098658, 0x8c098694, 0x8c0986de};
    for (u32 i = 0; i < 6; ++i)
        original[i] = {reinterpret_cast<const char*>(titles[i]), original_parameters + i,
                       reinterpret_cast<void(*)(void*, i32)>(formats[i]),
                       reinterpret_cast<void(*)(i32)>(setters[i])};
    menu = {0, original, original + 6, original + 6};
}
}

namespace opxy { namespace platform {
const char* recording_policy_name() { return policy; }
void recording_mode_set(bool value) { ++sets; enabled = value && policy && !reject_mode; }
bool recording_mode_enabled() { return enabled; }
Address host_stock_address(Address address) {
    switch (address) {
        case 0x0801f298: return reinterpret_cast<Address>(&menu);
        case 0x8d4d32a4: return reinterpret_cast<Address>(&allocate);
        case 0x8d4d32b4: return reinterpret_cast<Address>(&release_memory);
        case 0x8c089c40: return reinterpret_cast<Address>(&construct);
        case 0x8c0418f0: return reinterpret_cast<Address>(&string_assign);
    }
    __builtin_trap();
}
}}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const int scenario = argv[1][0] - '0';
    if (scenario == 0) {
        // Startup readiness and missing policy cannot accidentally enable replace.
        enabled = true; policy = nullptr; recording_menu_sync();
        CHECK(!enabled && !allocations && !constructions);
        policy = "replace"; recording_menu_sync();
        CHECK(!allocations && !constructions && !frees);
    }
    stock_menu();
    if (scenario == 1) out_of_memory = true;
    if (scenario == 2) menu.end = original + 5;
    if (scenario == 3) original[2].format = original[0].format;
    if (scenario == 4) original[5].set = nullptr;
    if (scenario == 5) original[1].title = original[0].title;
    if (scenario == 6) original[3].parameter = nullptr;
    if (scenario == 7) bad_constructor = true;
    if (scenario == 9) menu.capacity = original + 5;
    recording_menu_sync();
    if (scenario != 0 && scenario != 8) {
        CHECK(menu.begin == original && !enabled);
        CHECK(constructions == (scenario == 7 ? 1u : 0u));
        CHECK(allocations == (scenario == 1 || scenario == 7 ? 1u : 0u));
        CHECK(frees == (scenario == 7 ? 1u : 0u));
        if (scenario == 7) CHECK(freed == heap);
        // Repairing a failure does not reconstruct a registered object or retry allocation.
        stock_menu(); out_of_memory = bad_constructor = false;
        const u32 before = allocations;
        for (u32 i = 0; i < 100; ++i) recording_menu_sync();
        CHECK(allocations == before && menu.begin == original && !enabled && !bad);
        return 0;
    }
    CHECK(allocations == 1 && constructions == 1 && frees == 1 && freed == original);
    CHECK(menu.begin == heap && menu.end == heap + 7 && menu.capacity == heap + 7);
    for (u32 i = 0; i < 6; ++i) {
        CHECK(heap[i].title == original[i].title && heap[i].parameter == original[i].parameter);
        CHECK(heap[i].format == original[i].format && heap[i].set == original[i].set);
    }
    CHECK(same_text(heap[6].title, "recording") && heap[6].parameter == &parameter);
    CHECK(!parameter.name && !parameter.minimum && parameter.maximum == 1024 && !parameter.value);
    heap[6].format(&rendered, 0); CHECK(same_text(rendered, "overdub"));
    heap[6].format(&rendered, 1); CHECK(same_text(rendered, "replace"));
    heap[6].format(&rendered, 2); CHECK(same_text(rendered, "overdub"));
    heap[6].set(1); CHECK(enabled && parameter.value == 1024);
    for (u32 i = 0; i < 100; ++i) recording_menu_sync();
    CHECK(allocations == 1 && frees == 1 && constructions == 1 && enabled);
    heap[6].set(0); CHECK(!enabled && !parameter.value);
    reject_mode = true; heap[6].set(1); CHECK(!enabled && !parameter.value);
    reject_mode = false; heap[6].set(1); CHECK(enabled);
    policy = nullptr; recording_menu_sync(); CHECK(!enabled && !parameter.value);
    heap[6].format(&rendered, 1); CHECK(same_text(rendered, "overdub"));
    heap[6].set(1); CHECK(!enabled);
    policy = "replace"; heap[6].set(1); CHECK(enabled);
    // A changed vector/descriptor is not ours to reclaim or modify.
    if (scenario == 8) heap[6].set = nullptr;
    else menu = {0, original, original + 6, original + 6};
    recording_menu_sync(); CHECK(!enabled && !parameter.value);
    CHECK(allocations == 1 && frees == 1 && constructions == 1 && !bad);
    return 0;
}
