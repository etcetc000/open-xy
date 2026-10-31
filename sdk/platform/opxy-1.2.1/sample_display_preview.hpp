#pragma once
#include <opxy/sample_display.hpp>
#include <opxy/sample.hpp>
// Processor-local, selected-source preview. Poll runs outside audio rendering;
// only the stock file worker acquires/releases the cached PCM reference.
void sdk_sample_preview_request(opxy::u32 cookie, opxy::u32 key);
void sdk_sample_preview_poll(opxy::u32 time);
bool sdk_sample_preview_snapshot(opxy::u32 cookie, opxy::SampleDisplaySnapshot& out);
bool sdk_sample_preview_matches(opxy::u32 cookie, const opxy::SampleView& sample);
