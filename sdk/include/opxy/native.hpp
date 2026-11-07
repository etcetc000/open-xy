#pragma once

// SDK-owned platform boundary: a machine's own Blackfin routines (docs/api.md,
// "Native routines"). A machine keeps each in native/<name>.s with its manifest
// native/<name>.json and a header declaring the device symbol beside the
// portable C reference that defines what it computes:
//
//   extern "C" void mymachine_native_gain(opxy::i32* samples, unsigned count, opxy::i32 gain);
//   inline void gain_reference(opxy::i32* samples, unsigned count, opxy::i32 gain) { ... }
//   ...
//   OPXY_NATIVE(mymachine_native_gain, gain_reference)(samples, count, gain);
//
// OPXY_NATIVE names the device routine in a firmware build, which assembles and
// links native/<name>.s into that machine, and the reference everywhere else
// (host tests, CPU estimates, browser previews), where no device symbol exists
// and none is referenced. Both must have the same type, which is checked
// without referencing either.

namespace opxy { namespace native { namespace detail {
template <typename Device, typename Reference> struct same_signature;  // undefined: the types differ
template <typename Function> struct same_signature<Function, Function> { typedef Function* pointer; };
}}} // namespace opxy::native::detail

#define OPXY_NATIVE_POINTER(device, reference) \
    ::opxy::native::detail::same_signature<decltype(device), decltype(reference)>::pointer

#if defined(__bfin__)
#define OPXY_NATIVE(device, reference) (static_cast<OPXY_NATIVE_POINTER(device, reference)>(device))
#else
#define OPXY_NATIVE(device, reference) (static_cast<OPXY_NATIVE_POINTER(device, reference)>(reference))
#endif
