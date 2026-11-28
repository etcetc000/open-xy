// The audio snapshot registry: the selected synths' and effects' rows, which startup
// builds from their records (machine_tables.cpp); empty until then.
#include "audio_transport_native.hpp"
namespace opxy { namespace platform { namespace telemetry {
const Owner* owner(u32 engine) { return find_owner(sdk_audio_owners, sdk_audio_owner_count, engine); }
}}}
