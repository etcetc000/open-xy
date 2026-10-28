// Stock OS 1.2.1 side of the sample-analysis cache: hooks, and the stock
// functions the cache uses. Most hooks replace a CALL to the stock function they
// forward to, so stock calls them with the ordinary C ABI; the two load hooks
// arrive through sample_cache_hooks.s. The firmware profile authenticates every
// site. Device-only: the host test fixture supplies its own doubles.
#include "sample_cache.hpp"
#include "sample_display.hpp"
#include "sample_machines.hpp"
#include "stock.hpp"

extern "C" {
opxy::u32 sdk_sample_cache_old_sum(opxy::u32) __asm__("sdk_sample_cache_old_sum");
opxy::u32 sdk_sample_cache_message_priority(const unsigned char*) __asm__("sdk_sample_cache_message_priority");
void* sdk_sample_cache_alloc(opxy::u32) __asm__("sdk_sample_cache_alloc");
void sdk_sample_cache_free(void*) __asm__("sdk_sample_cache_free");
opxy::u32 sdk_sample_cache_stock_hash(const char*) __asm__("sdk_sample_cache_stock_hash");
void sdk_sample_cache_keys_lock() __asm__("sdk_sample_cache_keys_lock");
void sdk_sample_cache_keys_unlock() __asm__("sdk_sample_cache_keys_unlock");
const void* sdk_sample_cache_sample_acquire(opxy::u32) __asm__("sdk_sample_cache_sample_acquire");
void sdk_sample_cache_sample_release(opxy::u32) __asm__("sdk_sample_cache_sample_release");
opxy::u32 sdk_sample_cache_destroy(void*) __asm__("sdk_sample_cache_destroy");
opxy::u32 sdk_sample_cache_gui_add(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_sample_cache_gui_add");
opxy::u32 sdk_sample_cache_audio_add(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_sample_cache_audio_add");
opxy::u32 sdk_sample_cache_reset(opxy::u32) __asm__("sdk_sample_cache_reset");
opxy::u32 sdk_sample_cache_worker(opxy::u32) __asm__("sdk_sample_cache_worker");
opxy::u32 sdk_sample_cache_send() __asm__("sdk_sample_cache_send");
}

namespace {
using opxy::i32;
using opxy::u32;
using opxy::platform::stock;
using opxy::platform::word;
const u32 kHeap = 0x08036aa4;           // stock heap: mutex, then pool
const u32 kSampleManager = 0x84048894;  // stock sample cache manager pointer
const u32 kSendState = 0x84297630;      // desired-list send state: +12 sent sum, +16 current sum
const u32 kOldSum = 0x840393d0;         // the gather's previous path sum
const u32 kPendingTracks = 0x8429d1ac;  // gather's pending track list (sample_cache.cpp PendingTrack)
const u32 kPendingCount = 0x8429d5ac;
bool analysis_ready() { return opxy::platform::any_sample_analysis(); }
}

// Gather completion (8d4daf7e): the stock track array is stable while its busy
// flag is set. Ownership-only changes must also enter stock desired-list
// rebuilding. Stock auditions replace the current region while its browser is
// open, so this GUI-role capture publishes an empty request set then; the
// stock leave callback marks the desired list dirty, so closing republishes
// the final assignment even when the path sum is unchanged. Existing ready
// cache stays valid.
u32 sdk_sample_cache_old_sum(u32 new_sum) {
    const u32 old_sum = word(kOldSum);
    if (!analysis_ready()) return old_sum;
    const bool browsing = word(opxy::platform::kScreen) == opxy::platform::kSampleBrowserScreen;
    return sdk_sample_cache_snapshot_keys(new_sum, old_sum, browsing ? nullptr : reinterpret_cast<const void*>(kPendingTracks),
                                          browsing ? 0 : word(kPendingCount));
}
// Cache message construction (8d4da57c) loads the priority byte at +100 of the
// message whose key is at +96. Only this IPC copy carries the assigned marker.
u32 sdk_sample_cache_message_priority(const unsigned char* message) {
    const u32 priority = message[100];
    if (!analysis_ready()) return priority;
    return sdk_sample_cache_tag_priority(*reinterpret_cast<const u32*>(message + 96), priority);
}

// 8d4d03fc's lock, allocate and unlock on the stock heap, returning null on
// exhaustion instead of calling its fatal error reporter.
void* sdk_sample_cache_alloc(u32 bytes) {
    stock<void(u32, i32)>(0x8c03e6f4)(word(kHeap), -1);
    void* const block = stock<void*(u32, u32)>(0x8c048788)(word(kHeap + 4), bytes);
    stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(word(kHeap), 0, 0, 0);
    return block;
}
void sdk_sample_cache_free(void* block) { stock<void(void*)>(0x8d4d2910)(block); }
u32 sdk_sample_cache_stock_hash(const char* path) { return stock<u32(const char*)>(0x8d4e813a)(path); }
void sdk_sample_cache_keys_lock() { stock<void(u32)>(0x8d5be032)(word(kSampleManager)); }
void sdk_sample_cache_keys_unlock() { stock<void(u32)>(0x8d5be060)(word(kSampleManager)); }
const void* sdk_sample_cache_sample_acquire(u32 key) {
    return stock<const void*(u32, u32)>(0x8d509d70)(word(kSampleManager), key);
}
void sdk_sample_cache_sample_release(u32 key) { stock<void(u32, u32)>(0x8d509df8)(word(kSampleManager), key); }

// 8d50bc58: eviction found no remaining references. Release the analysis
// before stock destroys the PCM.
u32 sdk_sample_cache_destroy(void* object) {
    sdk_sample_display_release(object);
    sdk_sample_cache_release(object);
    return stock<u32(void*)>(0x8d508230)(object);
}
// Desired-list additions from the GUI (8d4daa06) and audio (8d4d8544) roles.
// Only GUI additions mark assigned keys; both admit them to the wanted list.
u32 sdk_sample_cache_gui_add(u32 manager, u32 entry, u32 key, u32 priority) {
    if (analysis_ready()) priority = sdk_sample_cache_accept_key(key, sdk_sample_cache_tag_priority(key, priority));
    return stock<u32(u32, u32, u32, u32)>(0x8d50b7f4)(manager, entry, key, priority);
}
u32 sdk_sample_cache_audio_add(u32 manager, u32 entry, u32 key, u32 priority) {
    if (analysis_ready()) priority = sdk_sample_cache_accept_key(key, priority);
    return stock<u32(u32, u32, u32, u32)>(0x8d50b7f4)(manager, entry, key, priority);
}
// Stock reset locks manager+0 until commit; wanted keys reset in the same
// transaction (8d4da9be, 8d4d84d8).
u32 sdk_sample_cache_reset(u32 manager) {
    const u32 result = stock<u32(u32)>(0x8d509d4c)(manager);
    if (analysis_ready()) sdk_sample_cache_reset_keys();
    return result;
}
// 8d50c59c: after the stock file worker loads assigned samples, prepare analysis.
u32 sdk_sample_cache_worker(u32 manager) {
    const u32 result = stock<u32(u32)>(0x8d50bca0)(manager);
    if (analysis_ready()) sdk_sample_cache_prepare_keys();
    sdk_sample_display_prepare();
    return result;
}
// 8d4da9b2: stock's second path-sum gate ignores ownership. Re-send the exact
// desired list when assignment membership changed without changing paths.
u32 sdk_sample_cache_send() {
    const bool unchanged = word(kSendState + 12) == word(kSendState + 16);
    const u32 result = stock<u32()>(0x8d4da8c8)();
    if (analysis_ready() && sdk_sample_cache_take_force_send() && unchanged) stock<void(u32)>(0x8d4da2d0)(1);
    return result;
}
