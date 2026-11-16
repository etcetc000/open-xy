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

// GCC's size-optimized constant divisions call this high-word product helper.
// Four 16-bit products keep it freestanding, with no 64-bit runtime dependency.
extern "C" unsigned int umulsi3_highpart(unsigned int, unsigned int) __asm__("___umulsi3_highpart");
extern "C" unsigned int umulsi3_highpart(unsigned int a, unsigned int b) {
    const unsigned int al = a & 65535u, bl = b & 65535u;
    const unsigned int ah = a >> 16, bh = b >> 16;
    const unsigned int low = al*bl, left = ah*bl, right = al*bh;
    const unsigned int middle = (low >> 16)+(left & 65535u)+(right & 65535u);
    return ah*bh+(left >> 16)+(right >> 16)+(middle >> 16);
}
