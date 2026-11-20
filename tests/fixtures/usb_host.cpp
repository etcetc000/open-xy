// The USB host glue of the 1.2.1 adapter with stock doubles at their mapped addresses: the
// usb_host_ready event, driver registration, the probe and remove trampolines, the log, and
// the control, SET_INTERFACE and bulk transfer services. Compiled by tests/test_usb_host.py.
#include <extension_runtime.cpp>
#include <usb_host.cpp>
#include <cstdio>
#include <cstring>
#include <string>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;

namespace {
// Stock memory the glue reads and writes: a device structure with the configuration record it
// points at, and a request object.
const u32 kDevice = 0x08040000, kRecord = 0x08040400, kRequest = 0x08041000;
alignas(4) unsigned char device_image[0x200], record_image[0x40], request_image[0x100];

std::string events;  // The order the stock calls and the event arrive in.
usbhost::StockDriver* registered[4];
u32 register_calls;
int register_result;
u32 start_result = 0x1234;

int fake_register(usbhost::StockDriver* driver) {
    events += 'R';
    if (register_calls < 4) registered[register_calls] = driver;
    ++register_calls;
    return register_result;
}
void fake_class_init() { events += 'I'; }
u32 fake_start() { events += 'S'; return start_result; }

// The stock logger: (level, file, line, format, string).
std::string logged, log_file;
u32 log_level;
void fake_log(u32 level, const char* file, u32, const char* format, const char* text) {
    if (std::strcmp(format, "%s") != 0) return;
    log_level = level;
    log_file = file;
    logged += text;
    logged += '|';
}

// The stack's control function: the arguments it was called with, and what it returns.
struct ControlCall { u32 device, pipe, request, type, value, index, length, timeout; void* data; };
ControlCall control_call;
i32 control_result = 9;
i32 fake_control(u32 device, u32 pipe, u32 request, u32 type, u32 value, u32 index, void* data, u32 length, u32 timeout) {
    control_call = {device, pipe, request, type, value, index, length, timeout, data};
    return control_result;
}
u32 set_interface_args[3];
i32 fake_set_interface(u32 device, u32 interface, u32 alternate) {
    set_interface_args[0] = device; set_interface_args[1] = interface; set_interface_args[2] = alternate;
    return 0;
}

// A request object: allocation, submission, the semaphore wait, cancellation and release.
std::string request_calls;
u32 request_new_result = kRequest, submit_result, wait_result = 1, request_status, request_actual;
u32 fake_request_new(u32 packets) {
    request_calls += 'N' + std::to_string(packets);
    if (!request_new_result) return 0;
    std::memset(request_image, 0, sizeof request_image);
    const u32 semaphore = 0x5e5e;
    std::memcpy(request_image + 0x44, &semaphore, 4);
    return request_new_result;
}
i32 fake_submit(u32 request) {
    request_calls += request == kRequest ? 'U' : '?';
    std::memcpy(request_image + 0x10, &request_status, 4);  // the completion's result
    std::memcpy(request_image + 0x18, &request_actual, 4);
    return static_cast<i32>(submit_result);
}
u32 fake_wait(u32 semaphore, u32 timeout) { request_calls += 'W' + std::to_string(timeout); return semaphore == 0x5e5e ? wait_result : 99; }
void fake_cancel(u32) { request_calls += 'C'; }
void fake_free(u32) { request_calls += 'F'; }

struct Seen {
    u32 probes, removes;
    usb::Device device;
    u32 interface, context;
};
Seen seen[2];

u32 probe_a(const usb::Services& services, const usb::Device& device, u32 interface) {
    ++seen[0].probes;
    seen[0].device = device;
    seen[0].interface = interface;
    services.log("probe a");
    return 7;
}
void remove_a(const usb::Services&, const usb::Device& device, u32 context) {
    ++seen[0].removes;
    seen[0].device = device;
    seen[0].context = context;
}
u32 probe_b(const usb::Services&, const usb::Device& device, u32 interface) {
    ++seen[1].probes;
    seen[1].device = device;
    seen[1].interface = interface;
    return 0;
}

const usb::Match table_a[] = {{usb::match::vendor | usb::match::product, 0x0bda, 0x8153, 0, 0, 0, 0, 0, 0},
                              {0, 0, 0, 0, 0, 0, 0, 0, 0}};
const usb::Match table_b[] = {{usb::match::interface_class | usb::match::interface_subclass, 0, 0, 0, 0, 0, 2, 6, 0},
                              {0, 0, 0, 0, 0, 0, 0, 0, 0}};
const usb::Driver driver_a = {"probe-a", table_a, probe_a, remove_a};
const usb::Driver driver_b = {"probe-b", table_b, probe_b, nullptr};
const usb::Driver incomplete = {"incomplete", table_a, nullptr, nullptr};
const usb::Driver unnamed = {"", table_a, probe_a, nullptr};
const usb::Driver no_table = {"no-table", nullptr, probe_a, nullptr};

bool add_results[6];
u32 ready_calls;
void on_ready(const usb::Registry& registry) {
    events += 'E';
    ++ready_calls;
    add_results[0] = registry.add(incomplete);
    add_results[1] = registry.add(unnamed);
    add_results[2] = registry.add(no_table);
    add_results[3] = registry.add(driver_a);
    add_results[4] = registry.add(driver_b);
    add_results[5] = registry.add(driver_a);  // the table holds two
}
void on_detail(const extensions::InputActivity&) {}
void on_activity() {}
const extensions::Hooks hooks = extensions::Hooks().with_usb_host_ready(on_ready)
    .with_input_activity(on_detail).with_input_activity(on_activity);
const extensions::Hooks hooks_reverse = extensions::Hooks().with_input_activity(on_detail)
    .with_input_activity(on_activity).with_usb_host_ready(on_ready);
u32 claim_keys[] = {0}, action_claims[] = {0};
const ExtensionRegistry tables = {claim_keys, 0, action_claims, 0};
ExtensionEntry collection[] = {{"usbtest", 0, 0, &hooks}};
}

namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& count) { count = 1; return collection; }
const ExtensionRegistry& extension_registry() { return tables; }
TrackPattern extension_pattern(u32) { return {0, 0, 0, false}; }
RecordingContext extension_context(bool& playing) { playing = false; return {}; }
i32 extension_position(u32) { return 0; }
u32 extension_scaled_delta(u32, u32 delta) { return delta; }
i32 extension_collision_radius(u32) { return 0; }
bool extension_record_held() { return false; }
u32 extension_write(u32, const RecordedNote*, u32, bool) { return 0; }
void extension_notify(const char*) {}

// The test translates the stock addresses the glue uses: functions and the memory they touch.
Address host_stock_address(Address address) {
    switch (address) {
        case usbhost::kRegister: return reinterpret_cast<Address>(fake_register);
        case usbhost::kHostClassInit: return reinterpret_cast<Address>(fake_class_init);
        case usbhost::kHostStart: return reinterpret_cast<Address>(fake_start);
        case usbhost::kLog: return reinterpret_cast<Address>(fake_log);
        case usbhost::kControl: return reinterpret_cast<Address>(fake_control);
        case usbhost::kSetInterface: return reinterpret_cast<Address>(fake_set_interface);
        case usbhost::kRequestNew: return reinterpret_cast<Address>(fake_request_new);
        case usbhost::kRequestSubmit: return reinterpret_cast<Address>(fake_submit);
        case usbhost::kRequestCancel: return reinterpret_cast<Address>(fake_cancel);
        case usbhost::kRequestFree: return reinterpret_cast<Address>(fake_free);
        case usbhost::kSemaphoreTake: return reinterpret_cast<Address>(fake_wait);
        default: break;
    }
    if (address >= kDevice && address < kDevice + sizeof device_image)
        return reinterpret_cast<Address>(device_image) + (address - kDevice);
    if (address >= kRecord && address < kRecord + sizeof record_image)
        return reinterpret_cast<Address>(record_image) + (address - kRecord);
    if (address >= kRequest && address < kRequest + sizeof request_image)
        return reinterpret_cast<Address>(request_image) + (address - kRequest);
    return 0;
}
}}

namespace {
void put16(unsigned char* image, u32 offset, u32 value) { image[offset] = value & 0xff; image[offset + 1] = value >> 8; }
void put32(unsigned char* image, u32 offset, u32 value) { put16(image, offset, value & 0xffff); put16(image, offset + 2, value >> 16); }
u32 request_word(u32 offset) { u32 value; std::memcpy(&value, request_image + offset, 4); return value; }
}

int main() {
    CHECK(hooks.usb_host_ready == on_ready && hooks.input_activity_details == on_detail &&
          hooks.input_activity == on_activity && hooks_reverse.usb_host_ready == on_ready &&
          hooks_reverse.input_activity_details == on_detail && hooks_reverse.input_activity == on_activity);
    // An RTL8153 on a high-speed link: its second configuration is being tried.
    device_image[0x00] = 3;
    device_image[0x04] = 0x85;  // the stack keeps the address in the low seven bits
    device_image[0x60] = 0; device_image[0x61] = 0; device_image[0x62] = 0;
    put16(device_image, 0x64, 0x0bda);
    put16(device_image, 0x66, 0x8153);
    device_image[0x6d] = 2;
    put32(device_image, 0x74, kRecord);
    put32(device_image, 0x78, 0xffffffffu);
    record_image[4] = 2; record_image[5] = 2; record_image[8] = 0x20;

    // The hook replaces two stock calls: both run, in order, with the event and the
    // registrations between them, and the second call's result is the hook's.
    CHECK(sdk_extensions_usb_host_init() == 0x1234);
    CHECK(events == "IERRS");
    CHECK(logged == "usb host: hook ran, drivers=2|" && log_level == 3 && log_file == "../src/bf/usb_host_ext.cpp");
    CHECK(ready_calls == 1);
    CHECK(!add_results[0] && !add_results[1] && !add_results[2] && add_results[3] && add_results[4] && !add_results[5]);
    CHECK(register_calls == 2 && usbhost::slot_count == 2);
    CHECK(registered[0] != registered[1]);
    CHECK(std::strcmp(registered[0]->name, "probe-a") == 0 && std::strcmp(registered[1]->name, "probe-b") == 0);
    CHECK(registered[0]->matches == table_a && registered[1]->matches == table_b);
    CHECK(registered[0]->probe && registered[0]->remove && registered[1]->probe && registered[1]->remove);
    CHECK(registered[0]->next == nullptr && registered[0]->prev == nullptr && registered[0]->mutex == nullptr);

    // The stack runs its host init again after a role change: the drivers are added again.
    CHECK(sdk_extensions_usb_host_init() == 0x1234);
    CHECK(events == "IERRSIERRS" && ready_calls == 2 && register_calls == 4 && usbhost::slot_count == 2);
    CHECK(registered[2] == registered[0] && registered[3] == registered[1]);

    // The stack calls probe with the device and an interface index: the extension sees the
    // device's fields and the configuration being tried, and its return value comes back.
    CHECK(registered[0]->probe(kDevice, 1, 1) == 7);
    CHECK(seen[0].probes == 1 && seen[0].interface == 1);
    const usb::Device& d = seen[0].device;
    CHECK(d.speed == 3 && d.address == 5 && d.vendor_id == 0x0bda && d.product_id == 0x8153 && d.handle == kDevice);
    CHECK(d.device_class == 0 && d.device_subclass == 0 && d.device_protocol == 0);
    CHECK(d.configuration_count == 2 && d.current_configuration == -1);
    CHECK(d.configuration_value == 2 && d.interface_count == 2 && d.max_power == 0x20);
    CHECK(registered[1]->probe(kDevice, 0, 1) == 0 && seen[1].probes == 1 && seen[1].interface == 0);
    put32(device_image, 0x78, 2);
    registered[0]->remove(kDevice, 7);
    CHECK(seen[0].removes == 1 && seen[0].context == 7 && seen[0].device.current_configuration == 2);
    registered[1]->remove(kDevice, 0);  // a driver without remove is skipped

    // A device whose configuration is not set yet has no record.
    put32(device_image, 0x74, 0);
    registered[0]->probe(kDevice, 0, 1);
    CHECK(seen[0].device.interface_count == 0 && seen[0].device.configuration_value == 0);
    put32(device_image, 0x74, kRecord);

    // The log: probe a's line reaches the stock logger, and a long line is cut to 120 characters.
    CHECK(logged.find("probe a|") != std::string::npos);
    logged.clear();
    usbhost::services.log(std::string(300, 'x').c_str());
    CHECK(logged == std::string(120, 'x') + "|");

    // Control: the device, the pipe (address in bits 8 to 14, bit 7 for IN), the setup fields
    // and a 1 s timeout reach the stack's function in its argument order; its result comes back.
    usb::Device device = d;
    unsigned char buffer[64];
    CHECK(usbhost::services.control(device, 0x80, 6, 0x0300 | 3, 0x0409, buffer, 2) == 9);
    CHECK(control_call.device == kDevice && control_call.pipe == (5u << 8 | 0x80) && control_call.request == 6);
    CHECK(control_call.type == 0x80 && control_call.value == 0x0303 && control_call.index == 0x0409);
    CHECK(control_call.length == 2 && control_call.timeout == 1000 && control_call.data == buffer);
    CHECK(usbhost::services.control(device, 0x21, 0x43, 0x0c, 0, nullptr, 0) == 9);
    CHECK(control_call.pipe == (5u << 8) && control_call.data == nullptr);  // OUT: no direction bit
    device.speed = 1;
    usbhost::services.control(device, 0x00, 9, 1, 0, nullptr, 0);
    CHECK(control_call.pipe == (5u << 8 | 1u << 26));  // low speed
    device.speed = 3;
    control_result = -13;
    CHECK(usbhost::services.control(device, 0x80, 6, 0, 0, buffer, 9) == -13);
    CHECK(usbhost::services.control(device, 0x80, 6, 0, 0, nullptr, 9) == -22);  // a data stage needs a buffer
    device.handle = 0;
    CHECK(usbhost::services.control(device, 0x00, 9, 1, 0, nullptr, 0) == -22);
    device.handle = kDevice;

    // SET_INTERFACE goes to the stack with the interface and the alternate setting.
    CHECK(usbhost::services.set_interface(device, 1, 1) == 0);
    CHECK(set_interface_args[0] == kDevice && set_interface_args[1] == 1 && set_interface_args[2] == 1);
    device.handle = 0;
    CHECK(usbhost::services.set_interface(device, 1, 1) == -22);
    device.handle = kDevice;

    // A bulk IN transfer: a request object with the pipe word (address, endpoint, type in the top
    // two bits, IN), the length, the buffer and the stack's completion; submitted, waited for,
    // its actual length returned and the object freed.
    request_actual = 60;
    CHECK(usbhost::services.transfer(device, 0x81, 2, buffer, 64, 3000) == 60);
    CHECK(request_calls == "N0UW3000F");
    CHECK(request_word(0x08) == (5u << 8 | 1u << 15 | 2u << 30 | 0x80));
    CHECK(request_word(0x14) == 64 && request_word(0x34) == 0 && request_word(0x40) == usbhost::kRequestDone);
    CHECK(request_word(0x48) == kDevice);
    CHECK(request_word(0x2c) == static_cast<u32>(reinterpret_cast<Address>(buffer)));

    // A bulk OUT and an interrupt IN differ in their pipe words.
    request_calls.clear();
    CHECK(usbhost::services.transfer(device, 0x02, 2, buffer, 301, 1000) == 60);
    CHECK(request_word(0x08) == (5u << 8 | 2u << 15 | 2u << 30));
    CHECK(usbhost::services.transfer(device, 0x83, 3, buffer, 16, 1000) == 60);
    CHECK(request_word(0x08) == (5u << 8 | 3u << 15 | 3u << 30 | 0x80));

    // A request that fails to complete: the stack's negative status comes back.
    request_calls.clear();
    request_status = static_cast<u32>(-13);
    CHECK(usbhost::services.transfer(device, 0x81, 2, buffer, 64, 100) == -13);
    CHECK(request_calls == "N0UW100F");
    request_status = 0;

    // A timeout: the request is cancelled, and -14 comes back.
    request_calls.clear();
    wait_result = 0;
    CHECK(usbhost::services.transfer(device, 0x81, 2, buffer, 64, 100) == -14);
    CHECK(request_calls == "N0UW100CF");
    wait_result = 1;

    // A submission the stack refuses, a transfer type that is not bulk or interrupt, no buffer,
    // no device and no memory for the request are all refused.
    request_calls.clear();
    submit_result = static_cast<u32>(-25);
    CHECK(usbhost::services.transfer(device, 0x81, 2, buffer, 64, 100) == -25);
    CHECK(request_calls == "N0UF");
    submit_result = 0;
    CHECK(usbhost::services.transfer(device, 0x81, 1, buffer, 64, 100) == -22);
    CHECK(usbhost::services.transfer(device, 0x81, 2, nullptr, 64, 100) == -22);
    request_new_result = 0;
    CHECK(usbhost::services.transfer(device, 0x81, 2, buffer, 64, 100) == -22);
    request_new_result = kRequest;
    device.handle = 0;
    CHECK(usbhost::services.transfer(device, 0x81, 2, buffer, 64, 100) == -22);

    // A stack that refuses a driver leaves it out, and says so in the log.
    usbhost::slot_count = 0;
    register_result = -1;
    logged.clear();
    CHECK(!usbhost::registry.add(driver_a) && usbhost::slot_count == 0);
    CHECK(logged == "usb host: register refused|");
    std::puts("ok");
    return 0;
}
