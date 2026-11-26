// The device identity (opxy/device_identity.hpp, SDK 1.30): linked only with a
// machine declaring resources.deviceIdentity, as the device_identity service.
//
// The GUI processor reads the identity from stock's boot information; the
// audio processor cannot, so the GUI sends it there in stock's own global
// parameter message (the B3 message stock sends for its master EQ), under
// parameter numbers stock never uses. Each processor keeps its own copy, final
// once marked known: the GUI's from its read, the audio processor's from a
// complete, checked message. docs/abi-1.2.1.md#device-identity.
#include "barrier.hpp"
#include "stock.hpp"
#include <opxy/device_identity.hpp>

using namespace opxy;
using namespace opxy::platform;

extern "C" {
void sdk_identity_gui_service() __asm__("sdk_identity_gui_service");
void sdk_identity_receive(u32 base, u32 id, i32 value) __asm__("sdk_identity_receive");
// The master processor's receiver (master_filter_stock.cpp) in a build that links
// it; in any other, kit_absent (startup.cpp, linked in every build), as
// tooling.native.service_fallbacks resolves it.
void sdk_master_receive(u32 base, u32 id, i32 value) __asm__("sdk_master_receive");
int kit_absent() __asm__("kit_absent");
}

namespace {
const u32 kBytes = device_identity_bytes, kWords = 5;
// Byte, nonzero on the GUI processor.
const u32 kRole = 0x86c05ca4;
// Stock's boot information: the identity, and the word after it, which holds
// an address in a 1000-byte window at 0x84000 once the information is in place.
const u32 kBootIdentity = 0x08010e52, kBootMarker = 0x08010e68;
// The global parameter message: the GUI's sender (u8 id, i32 value), and the
// audio processor's setter that the receiver calls for it, which stores
// any id's value at base + 0x54 + 4 * id unchecked.
const u32 kSend = 0x8d4dc84c, kStockSet = 0x8d50eb8c;
// The clock: the time in microseconds, written as 64 bits through its argument.
const u32 kClock = 0x8d5be3a0;
// Parameter numbers: stock sends 0..3, the master processor 128..137.
// A message is begin(sequence), five words, commit(check).
const u32 kBegin = 138, kFirstWord = 139, kCommit = 144;
// The GUI looks for the identity every 100 ms until it has it, then sends it
// once a second, 32 times.
const u32 kProbeMicroseconds = 100000, kSendMicroseconds = 1000000, kSends = 32;

// Each processor's own copy: identity is final once known is set.
u8 identity[kBytes];
volatile u32 known;

// GUI processor.
u32 next_probe, next_send, sends_left, sequence;
bool probed;
// Audio processor: the message being received.
u32 staged[kWords], staged_mask, staged_sequence;
bool staging;

u32 now_low() {
    u32 time[2] = {0, 0};
    stock<void(u32*)>(kClock)(time);
    return time[0];
}

bool due(u32 now, u32 at) { return static_cast<i32>(now - at) >= 0; }

bool marker_valid(u32 marker) { return (marker & 0xfffffc00u) == 0x84000u && (marker & 0x3ffu) < 1000u; }

// The boot information's identity, when it is in place and did not change while read.
bool read_boot(u8* output) {
    const u32 before = word(kBootMarker);
    if (!marker_valid(before)) return false;
    for (u32 i = 0; i < kBytes; ++i) output[i] = static_cast<u8>(byte(kBootIdentity + i));
    return word(kBootMarker) == before;
}

void words(u32* output) {
    for (u32 w = 0; w < kWords; ++w) {
        u32 value = 0;
        for (u32 b = 0; b < 4; ++b) {
            const u32 i = w * 4 + b;
            value |= static_cast<u32>(i < kBytes ? identity[i] : 0) << (8 * b);
        }
        output[w] = value;
    }
}

// FNV-1a over the sequence and the words, little-endian.
u32 check(u32 sequence_value, const u32* values) {
    u32 hash = 2166136261u;
    for (u32 w = 0; w <= kWords; ++w) {
        const u32 value = w ? values[w - 1] : sequence_value;
        for (u32 b = 0; b < 4; ++b) hash = (hash ^ ((value >> (8 * b)) & 255u)) * 16777619u;
    }
    return hash;
}

// Every other number: to the master processor's receiver when the build links
// it, else to stock's setter, as the call did without this service. The empty
// asm keeps the compiler from assuming two functions' addresses differ.
void forward(u32 base, u32 id, i32 value) {
    typedef void Receiver(u32, u32, i32);
    Receiver* master = &sdk_master_receive;
    Address absent = reinterpret_cast<Address>(&kit_absent);
    __asm__("" : "+r"(master), "+r"(absent));
    if (reinterpret_cast<Address>(master) == absent) stock<Receiver>(kStockSet)(base, id, value);
    else master(base, id, value);
}

void send() {
    u32 values[kWords];
    words(values);
    const u32 number = ++sequence;
    stock<void(u32, i32)>(kSend)(kBegin, static_cast<i32>(number));
    for (u32 w = 0; w < kWords; ++w) stock<void(u32, i32)>(kSend)(kFirstWord + w, static_cast<i32>(values[w]));
    stock<void(u32, i32)>(kSend)(kCommit, static_cast<i32>(check(number, values)));
}
}

// The GUI processor's main loop, every pass (device_identity_hooks.s).
void sdk_identity_gui_service() {
    if (!byte(kRole)) return;
    const u32 now = now_low();
    if (!known) {
        if (probed && !due(now, next_probe)) return;
        probed = true;
        next_probe = now + kProbeMicroseconds;
        if (!read_boot(identity)) return;
        barrier();
        known = 1;
        sends_left = kSends;
        next_send = now;
    }
    if (!sends_left || !due(now, next_send)) return;
    --sends_left;
    next_send = now + kSendMicroseconds;
    send();
}

// Stock's global parameter receiver call (8d5b145e): this service's numbers end
// here, every other goes on as it would without it. A message counts only when
// its five words follow its begin and its check matches; the first one sets the
// audio processor's copy, which later ones never change.
void sdk_identity_receive(u32 base, u32 id, i32 value) {
    if (id < kBegin || id > kCommit) {
        forward(base, id, value);
        return;
    }
    if (byte(kRole)) return;
    const u32 bits = static_cast<u32>(value);
    if (id == kBegin) {
        staging = true;
        staged_sequence = bits;
        staged_mask = 0;
        return;
    }
    if (!staging) return;
    if (id < kCommit) {
        staged[id - kFirstWord] = bits;
        staged_mask |= 1u << (id - kFirstWord);
        return;
    }
    staging = false;
    if (known || staged_mask != (1u << kWords) - 1 || bits != check(staged_sequence, staged) ||
        staged[kWords - 1] >> 16)
        return;
    for (u32 i = 0; i < kBytes; ++i) identity[i] = static_cast<u8>(staged[i / 4] >> (8 * (i % 4)));
    barrier();
    known = 1;
}

bool opxy_device_identity(u8* output, u32 size) {
    if (!known) return false;
    barrier();
    for (u32 i = 0; i < size && i < kBytes; ++i) output[i] = identity[i];
    return true;
}
