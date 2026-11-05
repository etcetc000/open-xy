#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace platform { namespace checkpoint {
struct PatternRef { u32 track, index; };
struct Support {
    bool (*save)(u8*, u32&, u32, const PatternRef*, u32);
    bool (*validate)(const u8*, u32);
    void (*load)(const u8*, u32);
};
const Support& support();
bool sample_engine(u32);
// GUI/controller role only, through the extension runtime's granted action.
void sync();
extensions::CheckpointResult command(extensions::CheckpointCommand);
}}}

namespace opxy { namespace platform { struct CheckpointBackend; }}
extern "C" const opxy::platform::CheckpointBackend* sdk_checkpoint_backend() __asm__("sdk_checkpoint_backend");
extern "C" const opxy::platform::checkpoint::Support* sdk_checkpoint_control_support() __asm__("sdk_checkpoint_control_support");
