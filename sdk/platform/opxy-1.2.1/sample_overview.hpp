#pragma once
#include <opxy/sample_overview.hpp>
// The file worker's side of the sample overview (sample_overview.cpp), called
// from the sample cache's stock hooks (sample_cache_stock.cpp).
extern "C" {
void sdk_sample_overview_update(opxy::u32) __asm__("sdk_sample_overview_update");
void sdk_sample_overview_prepare() __asm__("sdk_sample_overview_prepare");
void sdk_sample_overview_release(const void*) __asm__("sdk_sample_overview_release");
void sdk_sample_overview_wake() __asm__("sdk_sample_overview_wake");
}
