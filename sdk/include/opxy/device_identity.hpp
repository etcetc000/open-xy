#pragma once
#include <opxy/types.hpp>

// The device identity (SDK 1.30): eighteen bytes that the OP-XY's boot
// information holds for this unit. A machine that includes this header must
// declare "deviceIdentity": 1 in definition.json's resources; only a firmware
// with such a machine links the SDK service that reads them. The bytes identify
// the unit, like a serial number: see docs/api.md#device-identity before
// using them, and never publish or transmit them.
//
// On the device the identity is unknown for a moment after boot: the GUI
// processor reads it, then sends it to the audio processor. Until then
// device_identity returns false. Once it returns true it returns the same bytes
// until the device restarts. It is safe to call from any callback, display code
// included. On the host (tests) and in the browser preview the identity is
// preview_device_identity below, from the first call.
namespace opxy {
const u32 device_identity_bytes = 18;
// The identity the browser preview and host builds give: 0xa0, 0xa1, ... 0xb1.
const u8 preview_device_identity[device_identity_bytes] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
    0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1};
}

extern "C" bool opxy_device_identity(opxy::u8* output, opxy::u32 size);

namespace opxy {
#if !defined(__bfin__)
namespace device_identity_detail {
// mode 0: preview_device_identity; 1: bytes; 2: unknown.
struct Preview { u8 bytes[device_identity_bytes]; u32 mode; };
inline Preview& preview() {
    static Preview value = {};
    return value;
}
}
#endif

// Copies the identity's first `size` bytes (all 18 for size 18 or more) to
// output and returns true; returns false, writing nothing, while it is unknown.
static inline bool device_identity(u8* output, u32 size) {
#if defined(__bfin__)
    return opxy_device_identity(output, size);
#else
    const device_identity_detail::Preview& value = device_identity_detail::preview();
    if (value.mode == 2) return false;
    const u8* bytes = value.mode ? value.bytes : preview_device_identity;
    for (u32 i = 0; i < size && i < device_identity_bytes; ++i) output[i] = bytes[i];
    return true;
#endif
}

// Host tests only: the identity device_identity gives from now on, or unknown
// for nullptr (as on the device before the audio processor has it). Returns
// false and changes nothing on the device.
static inline bool configure_preview_device_identity(const u8* bytes) {
#if defined(__bfin__)
    (void)bytes;
    return false;
#else
    device_identity_detail::Preview& value = device_identity_detail::preview();
    value.mode = bytes ? 1 : 2;
    for (u32 i = 0; i < device_identity_bytes; ++i) value.bytes[i] = bytes ? bytes[i] : 0;
    return true;
#endif
}
}
