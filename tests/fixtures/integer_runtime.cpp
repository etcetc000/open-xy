#include <cstdlib>
#include <cstdint>
#define memset opxy_test_memset
#define memcpy opxy_test_memcpy
#include "../../sdk/platform/blackfin/memory.cpp"
#undef memset
#undef memcpy

static void require(bool value) { if (!value) std::abort(); }
int main() {
    const unsigned int edges[] = {0,1,2,65535,65536,65537,0x7fffffffu,0x80000000u,0xffff0000u,0xffffffffu};
    for (auto a : edges) for (auto b : edges)
        require(umulsi3_highpart(a,b) == unsigned((std::uint64_t(a)*b)>>32));
    unsigned int state = 0x81937abc;
    for (unsigned int i = 0; i < 100000; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const unsigned int a = state;
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        require(umulsi3_highpart(a,state) == unsigned((std::uint64_t(a)*state)>>32));
    }
}
