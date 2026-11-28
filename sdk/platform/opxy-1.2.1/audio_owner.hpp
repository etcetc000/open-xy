// A synth's or an effect's row in the audio snapshot registry (audio_transport_native.hpp
// owner): its record names it (machine_record.hpp, tooling/audio_telemetry.py owner_row),
// and startup builds the registry from the selected machines' records (machine_tables.cpp).
#pragma once
#include <opxy/types.hpp>

namespace opxy_transport { namespace telemetry { struct Bank; } }
namespace opxy { namespace platform { namespace telemetry {
// Its number is a link-time symbol's value (its engine or effect number); its bank
// accessor is the machine adapter's, and the rest is its declared shape.
struct Owner {
    const char* number;
    bool effect;
    opxy_transport::telemetry::Bank* (*bank)(u32 slot);
    u16 words;
    u8 mode;
    bool reset;
    u8 prefix;
};
}}}

extern "C" {
// The selected machines' rows, built at startup from their records (machine_tables.cpp).
extern const opxy::platform::telemetry::Owner* sdk_audio_owners __asm__("sdk_audio_owners");
extern opxy::u32 sdk_audio_owner_count __asm__("sdk_audio_owner_count");
}
