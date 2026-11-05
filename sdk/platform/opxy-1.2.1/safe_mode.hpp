// Safe mode: Shift held at power-on. The firmware then loads no module files,
// so a module that breaks the boot can be removed again over MTP.
//
// Anything else in the firmware may ask it, to behave as stock does while it
// is on; what else it turns off is each part's own choice.
//
// Decided once, on the first call, and the same answer on both roles from
// then on: the GUI role asks the keys when it draws the boot label (which
// shows SAFE-1.2.1 then) and tells the audio role over the board link
// before that one loads its modules.
#pragma once
extern "C" bool sdk_safe_mode() __asm__("sdk_safe_mode");
namespace opxy { namespace platform {
inline bool safe_mode() { return sdk_safe_mode(); }
}}
