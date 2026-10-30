#pragma once
#include <opxy/sample_analysis.hpp>

// Only the stock background worker/destructor call prepare/release. Audio
// lookup borrows a completed immutable entry while its stock voice owns the
// corresponding sample reference. No cache allocation or work starts here.
extern "C" {
// Entries are keyed by sample object and engine: each registered analysis
// machine prepares its own result within its own budget.
void sdk_sample_cache_prepare(const void*) __asm__("sdk_sample_cache_prepare");
void sdk_sample_cache_release(const void*) __asm__("sdk_sample_cache_release");
const opxy::SampleAnalysis* sdk_sample_cache_find(const void*, opxy::u32) __asm__("sdk_sample_cache_find");
opxy::u32 sdk_sample_cache_failed(const void*, opxy::u32) __asm__("sdk_sample_cache_failed");
void sdk_sample_gui_tick() __asm__("sdk_sample_gui_tick");
// The GUI wrapper supplies null/zero while browsing: stock audition mutates
// region paths before confirmation. An empty snapshot also removes old intent.
opxy::u32 sdk_sample_cache_snapshot_keys(opxy::u32, opxy::u32, const void*, opxy::u32) __asm__("sdk_sample_cache_snapshot_keys");
opxy::u32 sdk_sample_cache_tag_priority(opxy::u32, opxy::u32) __asm__("sdk_sample_cache_tag_priority");
opxy::u32 sdk_sample_cache_accept_key(opxy::u32, opxy::u32) __asm__("sdk_sample_cache_accept_key");
void sdk_sample_cache_reset_keys() __asm__("sdk_sample_cache_reset_keys");
void sdk_sample_cache_prepare_keys() __asm__("sdk_sample_cache_prepare_keys");
opxy::u32 sdk_sample_cache_take_force_send() __asm__("sdk_sample_cache_take_force_send");
}
