#include "recording_menu.hpp"
#include "stock.hpp"

namespace opxy { namespace platform {
namespace recording_menu_detail {
// Stock constructs these six descriptors at 8c099848. Its page retains the
// vector object, reads its current descriptors, and derives selection bounds
// from end - begin on entry (8c0987c4), including added rows.
struct Parameter {
    Address vtable;
    u8 flags[4];
    const char* name;
    i32 minimum, maximum, value;
};
struct Descriptor {
    const char* title;
    Parameter* parameter;
    void (*format)(void*, i32);
    void (*set)(i32);
};
struct Vector {
    Address allocator;
    Descriptor* begin;
    Descriptor* end;
    Descriptor* capacity;
};
static_assert(sizeof(Address) != 4 || sizeof(Parameter) == 24, "Stock integer parameter layout");
static_assert(sizeof(Descriptor) == 4 * sizeof(Address), "Stock setting descriptor layout");
static_assert(sizeof(Vector) == 4 * sizeof(Address), "Stock settings vector layout");

enum State { waiting, installed, failed };
State state;
Parameter parameter;
Descriptor* owned;
const char title[] = "recording";

void disable() {
    if (recording_mode_enabled()) recording_mode_set(false);
    parameter.value = 0;
}

void format(void* out, i32 value) {
    const char* policy = recording_policy_name();
    stock<void(void*, const char*)>(0x8c0418f0)(out, value == 1 && policy ? policy : "overdub");
}

void set(i32 value) {
    recording_mode_set(value == 1 && recording_policy_name() != nullptr);
    // The runtime may reject an unavailable policy. Keep the displayed value
    // consistent without scheduling a save: this parameter has no storage key.
    parameter.value = recording_mode_enabled() ? 1024 : 0;
}

bool expected(const Vector& vector) {
    const Address begin = reinterpret_cast<Address>(vector.begin);
    const Address end = reinterpret_cast<Address>(vector.end);
    if (!begin || begin % 4 || end < begin ||
        end - begin != 6 * sizeof(Descriptor) || vector.capacity != vector.end) return false;
    const Address titles[] = {0x84009230, 0x8400923c, 0x8400924c, 0x84009254, 0x84009260, 0x84009270};
    const Address formats[] = {0x8c0996f0, 0x8c0996f0, 0x8c0995d0, 0x8c09863a, 0x8c098676, 0x8c0986aa};
    const Address setters[] = {0x8c09961c, 0x8c0996a6, 0x8c09862c, 0x8c098658, 0x8c098694, 0x8c0986de};
    for (u32 i = 0; i < 6; ++i) {
        const Descriptor& row = vector.begin[i];
        if (reinterpret_cast<Address>(row.title) != titles[i] || !row.parameter ||
            reinterpret_cast<Address>(row.parameter) % 4 ||
            reinterpret_cast<Address>(row.format) != formats[i] ||
            reinterpret_cast<Address>(row.set) != setters[i]) return false;
    }
    return true;
}

bool still_owned(const Vector& vector) {
    return vector.begin == owned && vector.end == owned + 7 && vector.capacity == owned + 7 &&
        owned[6].title == title && owned[6].parameter == &parameter &&
        owned[6].format == format && owned[6].set == set;
}
}

void recording_menu_sync() {
    using namespace recording_menu_detail;
    if (state == failed) { disable(); return; }
    if (!recording_policy_name()) { disable(); return; }
    Vector& vector = *at<Vector>(0x0801f298);
    if (state == installed) {
        if (!still_owned(vector)) { state = failed; disable(); return; }
        parameter.value = recording_mode_enabled() ? 1024 : 0;
        return;
    }
    disable();
    // Wait for the stock static initializer; do not guess its execution order.
    if (!vector.begin && !vector.end && !vector.capacity) return;
    state = failed; // No allocation/registration retry after an unexpected state.
    if (!expected(vector)) return;
    Descriptor* next = stock<Descriptor*(u32)>(0x8d4d32a4)(7 * sizeof(Descriptor));
    if (!next) return;
    // Construct once, in process-lifetime storage: stock registers this object.
    // A null name makes stock's persistence load/save paths return immediately.
    stock<void(Parameter*, const char*, i32, i32, i32)>(0x8c089c40)(&parameter, nullptr, 0, 0, 1);
    if (parameter.vtable != 0x0803f61c || parameter.name || parameter.minimum ||
        parameter.maximum != 1024 || parameter.value) {
        stock<void(void*)>(0x8d4d32b4)(next);
        return;
    }
    for (u32 i = 0; i < 6; ++i) next[i] = vector.begin[i];
    next[6] = {title, &parameter, format, set};
    Descriptor* previous = vector.begin;
    vector.begin = next;
    vector.end = vector.capacity = next + 7;
    owned = next;
    state = installed;
    // Stock's vector destructor owns its allocation. Never give it static rows.
    stock<void(void*)>(0x8d4d32b4)(previous);
}
}}
