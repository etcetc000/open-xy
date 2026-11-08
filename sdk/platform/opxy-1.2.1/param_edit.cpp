// Stock's encoder edit, 8d5ad564 (reached from its controllers through 8d5ad5bc),
// calls the setter 8d5acf28 at 8d5ad5ae; that call routes through here, so a build
// links it for track control or extensions without linking the other.
#include "param_edit.hpp"
#include "stock.hpp"

using namespace opxy;
using namespace opxy::platform;

void (*sdk_param_turned)(u32, u32);
void (*sdk_parameter_edits)(u32, u32, i32);

extern "C" void sdk_param_edit_set(u32 project, u32 track, u32 index, i32 value) {
    if (sdk_param_turned) sdk_param_turned(track, index);
    stock<void(u32, u32, u32, i32)>(0x8d5acf28)(project, track, index, value);
    if (sdk_parameter_edits) sdk_parameter_edits(track, index, value);
}
