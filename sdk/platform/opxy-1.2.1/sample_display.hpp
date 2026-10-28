#pragma once
#include <opxy/sample_display.hpp>
extern "C" {
bool sdk_sample_display_read(opxy::u32, opxy::u32, opxy::SampleDisplay*) __asm__("sdk_sample_display_read");
void sdk_sample_display_prepare() __asm__("sdk_sample_display_prepare");
void sdk_sample_display_wake() __asm__("sdk_sample_display_wake");
void sdk_sample_display_release(const void*) __asm__("sdk_sample_display_release");
}
