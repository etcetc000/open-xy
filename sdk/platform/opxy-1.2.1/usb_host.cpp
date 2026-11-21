// Stock OS 1.2.1 USB host stack (smxUSBH): the usb_host_ready event and the services a host
// class driver gets. A driver is a 28-byte record the stack links onto a list at 0x080113d4;
// the stack calls its probe and remove on its own task (docs/abi-1.2.1.md, "USB host
// drivers"). This file copies the stack's device fields into an opxy::usb::Device, calls the
// extension, and offers it control, SET_INTERFACE and bulk/interrupt transfers through the
// stack's own functions.
#include "../extension_runtime.hpp"
#include "stock.hpp"

extern "C" {
opxy::u32 sdk_extensions_usb_host_init() __asm__("sdk_extensions_usb_host_init");
}

namespace opxy { namespace platform {
namespace usbhost {
// The stack's functions, by their OS 1.2.1 addresses.
const u32 kRegister = 0x8c0734a4;       // (driver record) -> 0, or -1 when it cannot be added
const u32 kHostClassInit = 0x8c071a50;  // the last class-driver initialiser, which the hook follows
const u32 kHostStart = 0x8c070350;      // starts the host; returns a status the caller tests
const u32 kControl = 0x8c07389c;        // (device, pipe, bRequest, bmRequestType, wValue, wIndex, data, wLength, ms) -> bytes, or < 0
const u32 kSetInterface = 0x8c0729b8;   // (device, interface, alternate) -> 0, or < 0
const u32 kRequestNew = 0x8c0736e4;     // (isochronous packets) -> a zeroed request object with a semaphore, or 0
const u32 kRequestSubmit = 0x8c0737e4;  // (request) -> 0, or < 0
const u32 kRequestCancel = 0x8c073840;  // (request)
const u32 kRequestFree = 0x8c07375c;    // (request): frees the object and its semaphore
const u32 kRequestDone = 0x8c07347c;    // the completion of a waited request: gives its semaphore
const u32 kSemaphoreTake = 0x8c03e6f4;  // (semaphore, ms) -> 0 on timeout
const u32 kLog = 0x8c042efc;            // (level, file, line, format, ...): the device's log

// A request object's fields, as the stack's own control function and audio driver fill them.
const u32 kPipe = 0x08, kUnknown = 0x0c, kStatus = 0x10, kLength = 0x14, kActual = 0x18, kMinusOne = 0x1c,
          kData = 0x2c, kContext = 0x34, kCompletion = 0x40, kSemaphore = 0x44, kDevice = 0x48;
const u32 kPending = static_cast<u32>(-20);  // a request's status until it completes
const i32 kInvalid = -22, kTimeout = -14;
const u32 kDrivers = 2;
const u32 kLineBytes = 120;

// The stock driver record: the stack's list links, the driver's name, its two callbacks and
// match table, and a mutex the registration creates.
struct StockDriver {
    const void* next;
    const void* prev;
    const char* name;
    u32 (*probe)(u32 device, u32 interface, u32 matched);
    void (*remove)(u32 device, u32 context);
    const usb::Match* matches;
    const void* mutex;
};
#if __SIZEOF_POINTER__ == 4
static_assert(sizeof(StockDriver) == 28, "The stack's driver record is 28 bytes");
#endif

struct Slot {
    StockDriver stock;
    usb::Driver driver;
};
Slot slots[kDrivers];
u32 slot_count;

// A line in the device's own log (the logs/ file it starts at each boot), tagged usb_host_ext.
void append_log(const char* line) {
    if (!line) return;
    char text[kLineBytes + 1];
    u32 n = 0;
    while (line[n] && n < kLineBytes) { text[n] = line[n]; ++n; }
    text[n] = 0;
    stock<void(u32, const char*, u32, const char*, const char*)>(kLog)(3, "../src/bf/usb_host_ext.cpp", 1, "%s", text);
}

// A transfer on the default pipe. The pipe word is the device address in bits 8 to 14, bit 26
// for a low-speed device and, for an IN request, bit 7: without that bit the stack's controller
// fails every IN request at once with -13.
i32 control(const usb::Device& device, u8 request_type, u8 request, u16 value, u16 index, void* data, u16 length) {
    if (!device.handle || (length && !data)) return kInvalid;
    const u32 pipe = (static_cast<u32>(device.address) << 8) | (device.speed == 1 ? 1u << 26 : 0u) | (request_type & 0x80u);
    return stock<i32(u32, u32, u32, u32, u32, u32, void*, u32, u32)>(kControl)(
        device.handle, pipe, request, request_type, value, index, data, length, 1000);
}

i32 set_interface(const usb::Device& device, u8 interface, u8 alternate) {
    return device.handle ? stock<i32(u32, u32, u32)>(kSetInterface)(device.handle, interface, alternate) : kInvalid;
}

void put(u32 address, u32 value) { *at<volatile u32>(address) = value; }

// A bulk or interrupt transfer: a request object filled as the stock audio driver fills its
// bulk requests, submitted and waited for the way the stack's control function does, with the
// completion that gives the object's semaphore. The pipe word adds the endpoint number (bits 15
// to 18), the endpoint's transfer type (bits 30 and 31: 2 bulk, 3 interrupt) and bit 7 for IN.
i32 transfer(const usb::Device& device, u8 endpoint, u8 type, void* data, u16 length, u32 timeout_ms) {
    if (!device.handle || !data || (type != 2 && type != 3)) return kInvalid;
    const u32 request = stock<u32(u32)>(kRequestNew)(0);
    if (!request) return kInvalid;
    const u32 pipe = (static_cast<u32>(device.address) << 8) | ((endpoint & 0xfu) << 15) |
                     (device.speed == 1 ? 1u << 26 : 0u) | (static_cast<u32>(type) << 30) | (endpoint & 0x80u);
    put(request + kPipe, pipe);
    put(request + kUnknown, 8);  // the audio driver sets 8 on its bulk requests; its meaning is not known
    put(request + kStatus, kPending);
    put(request + kLength, length);
    put(request + kMinusOne, 0xffffffffu);
    put(request + kData, static_cast<u32>(reinterpret_cast<Address>(data)));
    put(request + kContext, 0);
    put(request + kCompletion, kRequestDone);
    put(request + kDevice, device.handle);
    i32 result = stock<i32(u32)>(kRequestSubmit)(request);
    if (result == 0) {
        const u32 semaphore = word(request + kSemaphore);
        if (semaphore && stock<u32(u32, u32)>(kSemaphoreTake)(semaphore, timeout_ms) == 0) {
            stock<void(u32)>(kRequestCancel)(request);
            result = kTimeout;
        } else {
            result = static_cast<i32>(word(request + kStatus));
            if (result >= 0) result = static_cast<i32>(word(request + kActual));
        }
    }
    stock<void(u32)>(kRequestFree)(request);
    return result;
}

const usb::Services services = {append_log, control, set_interface, transfer};

u8 b(u32 base, u32 offset) { return static_cast<u8>(byte(base + offset)); }
u16 h(u32 base, u32 offset) { return static_cast<u16>(b(base, offset) | b(base, offset + 1) << 8); }

// The stack's device structure (inferred from its enumeration code; docs/abi-1.2.1.md).
usb::Device describe(u32 device) {
    usb::Device d = {};
    d.handle = device;
    d.speed = b(device, 0x00);
    d.address = b(device, 0x04) & 0x7f;
    d.device_class = b(device, 0x60);
    d.device_subclass = b(device, 0x61);
    d.device_protocol = b(device, 0x62);
    d.vendor_id = h(device, 0x64);
    d.product_id = h(device, 0x66);
    d.configuration_count = b(device, 0x6d);
    d.current_configuration = static_cast<i16>(word(device + 0x78));
    const u32 record = word(device + 0x74);  // the configuration being tried: a raw descriptor
    if (record) {
        d.interface_count = b(record, 4);
        d.configuration_value = b(record, 5);
        d.max_power = b(record, 8);
    }
    return d;
}

u32 probe_slot(u32 index, u32 device, u32 interface) {
    const usb::Driver& driver = slots[index].driver;
    return driver.probe ? driver.probe(services, describe(device), interface) : 0;
}
void remove_slot(u32 index, u32 device, u32 context) {
    const usb::Driver& driver = slots[index].driver;
    if (driver.remove) driver.remove(services, describe(device), context);
}
// The stack gives a callback no context, so each slot has its own pair.
u32 probe0(u32 device, u32 interface, u32) { return probe_slot(0, device, interface); }
u32 probe1(u32 device, u32 interface, u32) { return probe_slot(1, device, interface); }
void remove0(u32 device, u32 context) { remove_slot(0, device, context); }
void remove1(u32 device, u32 context) { remove_slot(1, device, context); }

bool printable_name(const char* name) {
    if (!name) return false;
    u32 n = 0;
    while (name[n] && n < 16) {
        if (name[n] < 0x20 || name[n] > 0x7e) return false;
        ++n;
    }
    return n > 0 && n < 16;
}

bool add(const usb::Driver& driver) {
    if (slot_count >= kDrivers || !driver.probe || !driver.matches || !printable_name(driver.name)) return false;
    Slot& slot = slots[slot_count];
    slot.driver = driver;
    slot.stock = StockDriver{nullptr, nullptr, driver.name, slot_count ? probe1 : probe0,
                             slot_count ? remove1 : remove0, driver.matches, nullptr};
    if (stock<int(StockDriver*)>(kRegister)(&slot.stock) != 0) {
        append_log("usb host: register refused");
        return false;
    }
    ++slot_count;
    return true;
}
const usb::Registry registry = {add};
}

void usb_host_start() {
    // The stack builds its driver list again each time it restarts the host (a role change).
    usbhost::slot_count = 0;
    extensions_usb_host_ready(usbhost::registry);
    char line[] = "usb host: hook ran, drivers=0";  // one line per host init, whatever the extensions added
    line[sizeof line - 2] = static_cast<char>('0' + usbhost::slot_count);
    usbhost::append_log(line);
}
}}

using namespace opxy;
using namespace opxy::platform;

// Replaces the two stock calls at 0x8c073c34, the last of the host stack's class-driver
// initialisers before it starts: they run in the same order, with the drivers added between.
u32 sdk_extensions_usb_host_init() {
    stock<void()>(usbhost::kHostClassInit)();
    usb_host_start();
    return stock<u32()>(usbhost::kHostStart)();
}
