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
