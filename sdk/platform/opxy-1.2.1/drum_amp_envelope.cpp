// Audio role: an ampEnvelope kit's note-off (SDK 1.23, drum_kit_voice.hpp). Linked and
// hooked only with the amp_envelope service, which a kit declaring ampEnvelope selects.
//
// The drum renderer calls stock's per-block ADSR stepper (8d50e358) for every drum
// voice at 8d51b38a, just before the voice's render (its method +12 at 8d51b55a): its
// filter envelope at voice +0x2c and track words 32..35. That envelope's level drives
// the track filter (11b01e2c), not the gain, so its words pass unchanged for every kit,
// as for Drumline. The amp envelope (M2, words 8..11) is the kit adapter's, which needs
// the stock voice's note-off: its amp release request at voice +0x29, set by the
// note-off (8d51a928) or at the note's start when its gate has already closed
// (8d51afc4). It is published here for the render that follows.
#include <opxy/types.hpp>
#include "stock.hpp"

extern "C" {
volatile unsigned char sdk_drum_released __asm__("sdk_drum_released");
opxy::i32 sdk_drum_envelope_setup(void* envelope, opxy::i32 attack, opxy::i32 decay, opxy::i32 sustain,
                                  opxy::i32 release) __asm__("sdk_drum_envelope_setup");
}

volatile unsigned char sdk_drum_released;

opxy::i32 sdk_drum_envelope_setup(void* envelope, opxy::i32 attack, opxy::i32 decay, opxy::i32 sustain, opxy::i32 release) {
    using namespace opxy::platform;
    sdk_drum_released = static_cast<const volatile unsigned char*>(envelope)[0x29 - 0x2c];
    return stock<opxy::i32(void*, opxy::i32, opxy::i32, opxy::i32, opxy::i32)>(0x8d50e358)(envelope, attack, decay, sustain,
                                                                                          release);
}
