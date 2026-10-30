// SDK ABI fixture: a buffered effect gets its send slot's stock scratch memory
// through the generated method table, and no buffer anywhere else.
#include <cstdlib>
#include <initializer_list>
#include "fx_object_probefx.cpp"

static opxy::i32 scratch[2][16];
static opxy::u32 asked_slot, asked_bytes;
static opxy::i32* stock_scratch(opxy::u32 slot, opxy::u32 bytes) {
    asked_slot = slot; asked_bytes = bytes;
    return scratch[slot == 14];
}
opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d50e048) return reinterpret_cast<Address>(&stock_scratch);
    std::abort();
}

int main() {
    Object storage = {};
    Object* object = static_cast<Object*>(probefx_fx_factory(&storage));
    if (object != &storage || object->buffer) return 1;
    const void* const* table = object->vtable;
    typedef void (*Init)(void*, opxy::u32);
    typedef void (*Process)(void*, opxy::i32*, opxy::i32*, const opxy::i32*);
    const Init init = reinterpret_cast<Init>(const_cast<void*>(table[5]));
    const Process process = reinterpret_cast<Process>(const_cast<void*>(table[4]));
    const opxy::i32 params[8] = {};
    opxy::i32 left[64] = {}, right[64] = {};
    // The sends (14, 15) each get stock's scratch for their slot, at the declared size.
    for (opxy::u32 slot : {14u, 15u}) {
        asked_slot = asked_bytes = 0;
        init(object, slot);
        if (asked_slot != slot || asked_bytes != 64 || object->buffer != scratch[slot == 14]) return 2;
        process(object, left, right, params);
        if (left[0] != 16 || scratch[slot == 14][0] != 7) return 3;
    }
    // Any other slot asks stock for nothing and gets an empty buffer.
    asked_slot = 99;
    init(object, 3);
    process(object, left, right, params);
    if (asked_slot != 99 || object->buffer || left[0] != 0) return 4;
    return 0;
}
