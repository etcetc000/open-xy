#pragma once
#include <opxy/types.hpp>

// GUI producer. Audio applies the latest command; consume the corresponding key.
extern "C" bool sdk_arranger_request_start(opxy::u32 position, bool resume, bool record_held = false)
    __asm__("sdk_arranger_request_start");
extern "C" bool sdk_arranger_request_seek(opxy::u32 position)
    __asm__("sdk_arranger_request_seek");
// Bind REC to one lane; pattern 0xffffffff follows clips under the playhead.
extern "C" bool sdk_arranger_request_record(opxy::u32 track, opxy::u32 pattern)
    __asm__("sdk_arranger_request_record");
// Audio telemetry: 0 off, 1 armed/stopped, 2 recording, 3 target unavailable.
extern "C" opxy::u32 sdk_arranger_record_status() __asm__("sdk_arranger_record_status");

// Audio dispatcher only. Begin stops/isolates/seeks; Start follows capture-ready.
extern "C" bool sdk_arranger_bounce_begin(opxy::u32 track, opxy::u32 start, opxy::u32 end)
    __asm__("sdk_arranger_bounce_begin");
extern "C" bool sdk_arranger_bounce_start() __asm__("sdk_arranger_bounce_start");
extern "C" void sdk_arranger_bounce_stop() __asm__("sdk_arranger_bounce_stop");
extern "C" void sdk_arranger_bounce_end() __asm__("sdk_arranger_bounce_end");
extern "C" bool sdk_arranger_bounce_transport_running() __asm__("sdk_arranger_bounce_transport_running");

// Capture adapters: Song delegates unchanged; Linear requires a current clip source.
extern "C" bool sdk_arranger_extension_target(opxy::u32 track)
    __asm__("sdk_arranger_extension_target");
