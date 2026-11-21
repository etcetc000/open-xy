#pragma once
#include <opxy/types.hpp>

// USB host class drivers for extensions (OS 1.2.1 only, experimental).
//
// The stock host stack (smxUSBH) matches each device's interfaces against a list
// of drivers and calls the first match's probe. An extension that takes the
// usb_host_ready event (opxy/extension.hpp) adds drivers to that list while the
// stack starts up, before any device is enumerated.
//
// The stack tries a device's configurations in order and keeps the first one for
// which a driver's probe returns non-zero. A probe that returns 0 declines the
// interface, and the stack goes on to the next interface, configuration or driver.
//
// probe and remove run on the host stack's own task, not the GUI's: they must not
// allocate or do file I/O. services.log writes to the device's own log (the logs/ file it
// starts at each boot, visible over MTP), tagged usb_host_ext.
//
// The services block the caller until the device answers, as the stack's own drivers do
// from their probes. A copy of the Device that probe received stays usable with them, from
// any task, until remove runs for it; after that the device is gone.
namespace opxy { namespace usb {

// Which fields of a match entry the stack compares. A device-level flag compares the
// device descriptor's field, an interface-level flag the interface descriptor's.
namespace match {
const u16 interface_class = 1u << 0;
const u16 interface_subclass = 1u << 1;
const u16 interface_protocol = 1u << 2;
const u16 device_class = 1u << 3;
const u16 device_subclass = 1u << 4;
const u16 device_protocol = 1u << 5;
const u16 vendor = 1u << 6;
const u16 product = 1u << 7;
}

// One entry of a driver's match table, laid out as the stock stack reads it.
// A table ends with an all-zero entry.
struct Match {
    u16 flags, vendor_id, product_id;
    u8 device_class, device_subclass, device_protocol;
    u8 interface_class, interface_subclass, interface_protocol;
};
static_assert(sizeof(Match) == 12, "The stack reads 12-byte match entries");

// A device the stack has enumerated, as probe sees it. Values are the stack's own:
// a speed of 3 is high speed, and the configuration fields describe the
// configuration being tried.
struct Device {
    u8 speed, address;
    u16 vendor_id, product_id;
    u8 device_class, device_subclass, device_protocol;
    u8 configuration_count;  // bNumConfigurations
    // The configuration being tried: its value, interface count and bMaxPower.
    u8 configuration_value, interface_count, max_power;
    // The value of the configuration the device is set to, or -1 before any.
    i16 current_configuration;
    u32 handle;  // the stack's device pointer, which the services use
};

struct Services {
    // Appends one line (up to 120 characters; a newline is added) to the log.
    void (*log)(const char* line);
    // A control transfer on the device's default pipe (bmRequestType, bRequest, wValue,
    // wIndex, then the data stage, if any, in `data` of `length` bytes; 1 s timeout). Returns
    // the stack's status: negative on error. `data` should be 32-byte aligned.
    i32 (*control)(const Device&, u8 request_type, u8 request, u16 value, u16 index, void* data, u16 length);
    // SET_INTERFACE through the stack, which also opens the alternate setting's endpoints.
    // Returns 0, or a negative status.
    i32 (*set_interface)(const Device&, u8 interface, u8 alternate);
    // One bulk (type 2) or interrupt (type 3) transfer on `endpoint` (its address, 0x80 for IN),
    // waiting up to `timeout_ms`. Returns the bytes transferred, or a negative status. `data`
    // must be 32-byte aligned and `length` a multiple of the endpoint's packet size for IN.
    i32 (*transfer)(const Device&, u8 endpoint, u8 type, void* data, u16 length, u32 timeout_ms);
};

// probe returns non-zero to claim the interface. `interface` is the interface's
// index in the configuration being tried. The non-zero value is kept by the stack and
// handed back to remove as `context`, so a driver may return a pointer to its own state.
using Probe = u32 (*)(const Services&, const Device&, u32 interface);
using Remove = void (*)(const Services&, const Device&, u32 context);

struct Driver {
    const char* name;      // 1-15 characters, for the stack's own use
    const Match* matches;  // terminated by an all-zero entry
    Probe probe;
    Remove remove;         // optional
};

// Passed to usb_host_ready. At most two drivers per build.
struct Registry {
    // Adds a driver; false when the table is full or the driver is incomplete. The
    // Driver and its match table must stay valid for the life of the firmware.
    bool (*add)(const Driver&);
};

using OnUsbHostReady = void (*)(const Registry&);
}}
