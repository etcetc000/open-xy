// 8d5b145e: the audio role's B3 receiver hands each global-parameter message to
// stock's setter 8d50eb8c(base, id, value). The SDK's virtual IDs stop here:
// the copies of stock's LFO (160-170, multi_lfo.hpp) and the master processor
// (128-137, master_filter_stock.cpp, kit_absent in a build without it, which
// declines). Everything else reaches stock unchanged.
#include "multi_lfo.hpp"
#include "stock.hpp"

extern "C" {
opxy::u32 sdk_master_b3(opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_master_b3");
void sdk_b3_receive(opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_b3_receive");
}

void sdk_b3_receive(opxy::u32 base, opxy::u32 id, opxy::i32 value) {
    using namespace opxy;
    if (platform::multi_lfo::receive(id, value) || sdk_master_b3(base, id, value)) return;
    platform::stock<void(u32, u32, i32)>(0x8d50eb8c)(base, id, value);
}
