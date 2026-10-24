// Shared display behavior. The SDK applies this bounded frame to the retained
// segment, glow and dial nodes and the header values, in the browser and on the device.
#include "display.h"
#include "fm.h"
#include "models.hpp"
#include "segments.hpp"

namespace macroosc {
// Four characters show the model; its code replaces the first control's value,
// and the fourth shows the FM ratio or, after a press of its encoder, the FM depth.
// The knobs turn 144 degrees either side of 12 o'clock, end to end of their ticks.
const opxy::i32 kSweep = 1440;
const unsigned kModels = sizeof(models) / sizeof(models[0]);
// Parameter slots (definition.json).
const unsigned kModelSlot = 0, kTimbreSlot = 1, kColorSlot = 2, kRatioSlot = 3, kDepthSlot = 4;
// One decimal place, matching fine turns and the browser preview.
inline void amount(char* out, unsigned capacity, opxy::i32 value) {
  unsigned p = value < 0 ? 0 : ((static_cast<unsigned>(value) >> 10) * 1000 + (1u << 20)) >> 21;
  char text[6];
  unsigned n = 0;
  if (p >= 1000) text[n++] = '1';
  if (p >= 100) text[n++] = static_cast<char>('0' + p / 100 % 10);
  text[n++] = static_cast<char>('0' + p / 10 % 10);
  text[n++] = '.';
  text[n++] = static_cast<char>('0' + p % 10);
  text[n] = 0;
  opxy::draw::copy(out, capacity, text);
}
// One decimal place; audio retains the full interpolated Q16 ratio.
inline void ratio(char* out, unsigned capacity, unsigned q16) {
  const unsigned tenths = (q16 * 10 + 32768) >> 16;
  char text[4] = {static_cast<char>('0' + tenths / 10), '.', static_cast<char>('0' + tenths % 10), 0};
  opxy::draw::copy(out, capacity, text);
}
inline void display_reset(DisplayState& state) { state.model = 0; }

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
  const opxy::i32 model = input.slots[kModelSlot];
  unsigned index = model < 0 ? 0 : ((static_cast<unsigned>(model) >> 16) * kModels) >> 15;
  // The characters repaint only when the model changes; the layers are the
  // segments (palette 0 and 1), then each glow layer (2 and 3, 4 and 5, 6 and 7).
  if (state.model != index + 1) {
    for (unsigned layer = 0; layer < kLayers; ++layer)
      paint_segments(frame, layer * kCharacters * kSegments, models[index].masks, kCharacters, 2 * layer,
                     2 * layer + 1);
    state.model = index + 1;
  }
  opxy::draw::header(frame, 0, models[index].code);
  amount(frame.header[1], sizeof(frame.header[1]), input.parameters[1]);
  amount(frame.header[2], sizeof(frame.header[2]), input.parameters[2]);
  if (input.shown[3] == kRatioSlot)
    ratio(frame.header[3], sizeof(frame.header[3]), RatioQ16FromParameter(input.parameters[3]));
  else
    amount(frame.header[3], sizeof(frame.header[3]), input.parameters[3]);
  // Knobs: Timbre and Color along the bottom; on the right, the linked pair
  // the fourth encoder switches between, each showing its own parameter: FM
  // Ratio below, stepping between ratios, and FM Depth above.
  const unsigned slots[4] = {kTimbreSlot, kColorSlot, kRatioSlot, kDepthSlot};
  for (unsigned i = 0; i < 4; ++i) {
    opxy::draw::place(frame, kDialNode + i, kDialX[i], kDialY[i], opxy::draw::unit, 0, opxy::draw::keep);
    opxy::draw::turn(frame, kDialNode + i, opxy::draw::sweep(input.slots[slots[i]], kSweep));
  }
}
}  // namespace macroosc
