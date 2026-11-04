// Safe mode: Shift held at power-on. The firmware then loads no module files,
// so a module that breaks the boot can be removed again over MTP.
//
// Anything else in the firmware may ask it, to behave as stock does while it
// is on; what else it turns off is each part's own choice.
//
// Decided once, on the first call, which the boot label makes at startup
// (it shows SAFE-1.2.1 then); every later call returns the same answer. For
// now only the GUI role asks the keys; the audio role always answers false
// (not passed on yet).
#pragma once
extern "C" bool sdk_safe_mode() __asm__("sdk_safe_mode");
namespace opxy { namespace platform {
inline bool safe_mode() { return sdk_safe_mode(); }
}}
