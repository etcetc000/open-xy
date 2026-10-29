#pragma once
#include <opxy/sample.hpp>
#include <opxy/sample_display.hpp>
extern "C" {
// voiceid is a nonzero monotonic start serial, never a pointer or reused slot ID.
void sdk_sample_visual_render(opxy::u32,opxy::u32,opxy::u32,const opxy::SampleView*,const opxy::SampleVisual*) __asm__("sdk_sample_visual_render");
void sdk_sample_visual_clear(opxy::u32,opxy::u32,opxy::u32) __asm__("sdk_sample_visual_clear");
bool sdk_sample_visual_message(const void*,opxy::u32) __asm__("sdk_sample_visual_message");
void sdk_sample_visual_poll() __asm__("sdk_sample_visual_poll");
}
