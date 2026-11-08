// The GUI role's B3 global-parameter publication 8d4dc84c(id, value), as the
// master processor sends its snapshots; the audio role's receiver
// (b3_receive.cpp) takes the copies' virtual IDs before stock's setter.
#include "multi_lfo.hpp"
#include "stock.hpp"

namespace opxy { namespace platform { namespace multi_lfo {
void send(u32 id, i32 value) { stock<void(u32, i32)>(0x8d4dc84c)(id, value); }
}}}
