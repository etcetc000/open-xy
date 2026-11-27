// The master bus's shared handler (service master_bus): compiled only for a collection with both
// Sample Studio and a master processor (OPXY_MASTER_SHARED, tooling/native.py), whose units then
// name their own entries apart (sample_studio_audio.cpp, master_filter_stock.cpp). A stock site
// has one hook, so this one runs both, in a fixed order. A collection with only one of them
// doesn't link this unit, and its build is the same as without it.
//
// A selection with only one of the two resolves the other's entries to kit_absent
// (tooling.native.service_fallbacks), which answers 0: this handler then runs stock's own code
// in its place. Audio role only.
#include "stock.hpp"

using opxy::i32;
using opxy::u32;
using opxy::platform::stock;

extern "C" {
void sdk_master_process(void* object, i32* left, i32* right) __asm__("sdk_master_process");
void sdk_master_receive(u32 base, u32 id, i32 value) __asm__("sdk_master_receive");
// The master processor's (master_filter_stock.cpp): its stage in place of stock's EQ, and its
// receiver, which takes its own numbers (128..137) and gives stock's setter the rest.
void sdk_processor_process(void* object, i32* left, i32* right) __asm__("sdk_processor_process");
void sdk_processor_receive(u32 base, u32 id, i32 value) __asm__("sdk_processor_receive");
// Sample Studio's player's (sample_studio_audio.cpp): its block added onto the bus, and its
// numbers (146..160), 1 when it took one.
void sdk_studio_master(i32* left, i32* right) __asm__("sdk_studio_master");
u32 sdk_studio_receive(u32 id, i32 value) __asm__("sdk_studio_receive");
int kit_absent() __asm__("kit_absent");
}

namespace {
typedef opxy::platform::Address Address;
// Stock's master EQ (8d511b80's own call) and the audio board's parameter setter.
const u32 kStockEq = 0x11a02c10, kStockSet = 0x8d50eb8c;
// Whether a selection links the master processor. The empty asm keeps the compiler from
// assuming two functions' addresses differ (as device_identity.cpp's forward).
bool processor_linked() {
    Address processor = reinterpret_cast<Address>(&sdk_processor_process);
    Address absent = reinterpret_cast<Address>(&kit_absent);
    __asm__("" : "+r"(processor), "+r"(absent));
    return processor != absent;
}
}  // namespace

// 8d511b80, once a block after dry + FX I + FX II are summed: the master processor's stage
// (stock's EQ while it has none selected, and in its crossfades), then the studio's player adds
// its block, as it does after stock's EQ in a build without a master processor. What the studio
// plays dry is not filtered; its send returns are on the bus already, as every track's are.
void sdk_master_process(void* object, i32* left, i32* right) {
    if (processor_linked()) sdk_processor_process(object, left, right);
    else stock<void(void*, i32*, i32*)>(kStockEq)(object, left, right);
    sdk_studio_master(left, right);
}

// 8d5b145e (after the device identity's receiver in a build with it): the player's numbers,
// then the master processor's receiver, which gives stock's setter every number it doesn't own.
// The ranges are apart, so the order changes nothing.
void sdk_master_receive(u32 base, u32 id, i32 value) {
    if (sdk_studio_receive(id, value)) return;
    if (processor_linked()) sdk_processor_receive(base, id, value);
    else stock<void(u32, u32, i32)>(kStockSet)(base, id, value);
}
