// The firmware's memset and memcpy, built under other names, against byte
// loops at every alignment and size a struct copy is likely to hit; and
// q15::product against the plain 16 x 16 product.
#include <opxy/q15.hpp>
#include <stddef.h>
#include <stdio.h>

extern "C" void* opxy_memset(void*, int, size_t);
extern "C" void* opxy_memcpy(void*, const void*, size_t);

static unsigned char got[128], want[128], source[128];

static void reset() {
    for (unsigned i = 0; i < sizeof got; ++i) got[i] = want[i] = static_cast<unsigned char>(i * 7 + 3);
    for (unsigned i = 0; i < sizeof source; ++i) source[i] = static_cast<unsigned char>(i * 13 + 101);
}

static bool same() {
    for (unsigned i = 0; i < sizeof got; ++i) if (got[i] != want[i]) return false;
    return true;
}

int main() {
    int failures = 0;
    for (unsigned to = 0; to < 8; ++to)
        for (unsigned size = 0; size < 80; ++size) {
            static const int fills[] = {0, 0x5a, 0xff, 0x1a5};
            for (int value : fills) {
                reset();
                void* result = opxy_memset(got + to, value, size);
                for (unsigned i = 0; i < size; ++i) want[to + i] = static_cast<unsigned char>(value);
                if (result != got + to || !same()) { printf("memset to %u size %u value %d\n", to, size, value); ++failures; }
            }
            for (unsigned from = 0; from < 8; ++from) {
                reset();
                void* result = opxy_memcpy(got + to, source + from, size);
                for (unsigned i = 0; i < size; ++i) want[to + i] = source[from + i];
                if (result != got + to || !same()) { printf("memcpy to %u from %u size %u\n", to, from, size); ++failures; }
            }
        }
    const opxy::i32 values[] = {0, 1, -1, 32767, -32768, 0x12345678, static_cast<opxy::i32>(0x8000ffffu), 0x00017fff};
    for (opxy::i32 a : values)
        for (opxy::i32 b : values) {
            const opxy::i32 expect = static_cast<opxy::i32>(static_cast<opxy::i16>(a)) * static_cast<opxy::i16>(b);
            if (opxy::q15::product(a, b) != expect) { printf("product %d %d\n", a, b); ++failures; }
        }
    if (opxy::q15::top(static_cast<opxy::i32>(0x7fff0000)) != 0x7fff || opxy::q15::top(-1) != 0xffff) { printf("top\n"); ++failures; }
    return failures ? 1 : 0;
}
