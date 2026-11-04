// Safe mode (safe_mode.hpp): is Shift held at power-on?
//
// The key co-chip reports only changes (message 0x42), so a key held since
// power-on never reaches stock's key state. The bootloader asks for the held
// keys at every power-on (88a20852: type 0x41, no payload, 100 ms); the reply
// carries the 72-key bitmap from byte 4, laid out like a 0x42 report
// (8d5b2d90: key n is bit n % 8 of byte 4 + n / 8). The same request goes
// through stock's request routine (8c0461c6, as stock calls it at 8c089f3e)
// on the key co-chip's link (key object 84185998, link at +4). Shift is key
// 0x37 (stock's key handlers 8c0b5b10 / 8c0b5bbc).
#include "safe_mode.hpp"
#include "stock.hpp"
using namespace opxy;
using namespace opxy::platform;
namespace {
const u32 key_link = 0x84185998 + 4, key_status = 0x41, key_shift = 0x37, timeout_ms = 100;
alignas(4) unsigned char reply[256];
bool decided, on;

bool shift_held_at_power_on() {
    if (!stock<u32(u32, u32, const void*, u32, void*, u32)>(0x8c0461c6)(
            key_link, key_status, nullptr, 0, reply, timeout_ms)) return false;  // no answer: normal boot
    return (reply[4 + key_shift / 8] >> (key_shift % 8)) & 1;
}
}

bool sdk_safe_mode() {
    if (!decided) {
        decided = true;
        const bool gui = stock<u32()>(0x8d5c3eac)() != 0;
        on = gui && shift_held_at_power_on();
    }
    return on;
}
