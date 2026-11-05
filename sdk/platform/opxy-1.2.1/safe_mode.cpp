// Safe mode (safe_mode.hpp): is Shift held at power-on?
//
// The GUI role asks the keys. The key co-chip reports only changes (message
// 0x42), so a key held since power-on never reaches stock's key state. The
// bootloader asks for the held keys at every power-on (88a20852: type 0x41,
// no payload, 100 ms); the reply carries the 72-key bitmap from byte 4, laid
// out like a 0x42 report (8d5b2d90: key n is bit n % 8 of byte 4 + n / 8).
// The same request goes through stock's request routine (8c0461c6, as stock
// calls it at 8c089f3e) on the key co-chip's link (key object 84185998, link
// at +4). Shift is key 0x37 (stock's key handlers 8c0b5b10 / 8c0b5bbc).
//
// The audio role learns the answer from the GUI: once decided (at the boot
// label), the GUI sends board message 0xe0 {safe} on the board link (board
// object 841460b0, link at +4). Above stock's message types, it reaches the
// default case of stock's board receiver (8d5b18c4), which the module runtime
// hooks (sdk_module_board_receive). Measured on the device: it arrives before
// the audio role loads its modules. Without it the audio role loads normally.
#include "safe_mode.hpp"
#include "stock.hpp"
using namespace opxy;
using namespace opxy::platform;
namespace {
const u32 key_link = 0x84185998 + 4, key_status = 0x41, key_shift = 0x37, timeout_ms = 100;
const u32 board_link = 0x841460b0 + 4, board_safe_mode = 0xe0;
alignas(4) unsigned char reply[256];
bool decided, on;
volatile u32 told, told_on;  // audio role: what the GUI sent

bool shift_held_at_power_on() {
    if (!stock<u32(u32, u32, const void*, u32, void*, u32)>(0x8c0461c6)(
            key_link, key_status, nullptr, 0, reply, timeout_ms)) return false;  // no answer: normal boot
    return (reply[4 + key_shift / 8] >> (key_shift % 8)) & 1;
}

void board_send(u32 type, const unsigned char* payload, u32 bytes) {
    stock<u32(u32, u32, const void*, u32, u32, u32, u32)>(0x11a00ec0)(board_link, type, payload, bytes, 0, 255, 255);
}
}

// The boot label (startup_label.s.in calls this in builds that load modules):
// stock draws it before the module loader runs, so on the GUI role this call
// decides.
extern "C" const char kit_startup_label_text[] __asm__("kit_startup_label_text");
extern "C" const char* sdk_startup_label_text() __asm__("sdk_startup_label_text");
const char* sdk_startup_label_text() { return sdk_safe_mode() ? "SAFE-1.2.1" : kit_startup_label_text; }

// Stock's board receiver, default case (8d5b18c4): the module runtime's
// messages, every other type to stock's handler (8d5aeb58) unchanged.
extern "C" u32 sdk_module_board_receive(void* board, const unsigned char* message) __asm__("sdk_module_board_receive");
u32 sdk_module_board_receive(void* board, const unsigned char* message) {
    if (message && message[2] == board_safe_mode && message[3] >= 1) {
        told_on = message[4] != 0;
        told = 1;
        return 0;
    }
    return stock<u32(void*, const unsigned char*)>(0x8d5aeb58)(board, message);
}

bool sdk_safe_mode() {
    if (!decided) {
        decided = true;
        const bool gui = stock<u32()>(0x8d5c3eac)() != 0;
        on = gui ? shift_held_at_power_on() : told && told_on;
        if (gui) {
            const unsigned char payload[1] = {static_cast<unsigned char>(on)};
            board_send(board_safe_mode, payload, sizeof payload);
        }
    }
    return on;
}
