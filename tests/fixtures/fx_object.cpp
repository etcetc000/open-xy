// SDK ABI fixture: exercise dispatch through the actual generated method table.
#include "fx_object_probefx.cpp"

int main() {
    if (probefx_fx_factory(nullptr)) return 1;
    Object storage = {};
    Object* object = static_cast<Object*>(probefx_fx_factory(&storage));
    if (object != &storage || object->state.initialized) return 2;
    const void* const* table = object->vtable;
    typedef void (*Init)(void*, opxy::u32);
    typedef opxy::i32 (*Dry)(void*, const opxy::i32*);
    typedef void (*Process)(void*, opxy::i32*, opxy::i32*, const opxy::i32*);
    reinterpret_cast<Init>(const_cast<void*>(table[5]))(object, 14);
    const Dry dry = reinterpret_cast<Dry>(const_cast<void*>(table[7]));
    const Process process = reinterpret_cast<Process>(const_cast<void*>(table[4]));
    const int slot = @SLOT@;
    const opxy::i32 values[] = {0, 1, 65535, 0x12345678, 0x40000000, 0x7fffffff, -1};
    for (unsigned n = 0; n < sizeof(values)/sizeof(values[0]); ++n) {
        opxy::i32 params[8] = {101, 202, 303, 404, 505, 606, 707, 808};
        // Without a declared control, the fourth word must not become Dry.
        if (slot < 0) params[3] = values[n];
        else params[static_cast<unsigned>(slot)] = values[n];
        const opxy::i32 expected = slot < 0 ? 0x7fffffff : values[n];
        if (dry(object, params) != expected) return 3;
        opxy::i32 left[64] = {}, right[64] = {};
        process(object, left, right, params);
        if (left[0] != 123 || right[0] != -456) return 4;
        if (dry(object, params) != expected) return 5;
    }
    return 0;
}
