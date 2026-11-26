// Freestanding byte operations. Kept out of contributor DSP and browser builds.
// GCC calls these for struct initialization and copies, which are almost
// always whole aligned words: those go a word at a time.
#include <stddef.h>
typedef unsigned int __attribute__((may_alias)) word;
extern "C" void* memset(void* destination, int value, size_t size) {
    unsigned char* d = static_cast<unsigned char*>(destination);
    const unsigned char byte = static_cast<unsigned char>(value);
    if (((reinterpret_cast<size_t>(d) | size) & 3) == 0) {
        word* w = reinterpret_cast<word*>(d);
        const word fill = byte * 0x01010101u;
        for (size_t n = size >> 2; n; --n) *w++ = fill;
        return destination;
    }
    while (size--) *d++ = byte;
    return destination;
}
extern "C" void* memcpy(void* destination, const void* source, size_t size) {
    unsigned char* d = static_cast<unsigned char*>(destination);
    const unsigned char* s = static_cast<const unsigned char*>(source);
    if (((reinterpret_cast<size_t>(d) | reinterpret_cast<size_t>(s) | size) & 3) == 0) {
        word* w = reinterpret_cast<word*>(d);
        const word* r = reinterpret_cast<const word*>(s);
        for (size_t n = size >> 2; n; --n) *w++ = *r++;
        return destination;
    }
    while (size--) *d++ = *s++;
    return destination;
}
