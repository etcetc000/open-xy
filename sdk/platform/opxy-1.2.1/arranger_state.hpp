#pragma once
#include <opxy/arranger.hpp>

// GUI role only. Edits remain private until published; poll every GUI tick to retry.
opxy::arranger::Model& sdk_arranger_model() __asm__("sdk_arranger_model");
extern "C" {
void sdk_arranger_bind_project(opxy::u32 generation) __asm__("sdk_arranger_bind_project");
bool sdk_arranger_audio_project_matches(opxy::u32 generation) __asm__("sdk_arranger_audio_project_matches");
void sdk_arranger_publish(bool active, bool seek = false) __asm__("sdk_arranger_publish");
void sdk_arranger_publish_poll() __asm__("sdk_arranger_publish_poll");
// GUI telemetry only: sampled playback cursor, never the edit cursor.
opxy::u32 sdk_arranger_playback_position() __asm__("sdk_arranger_playback_position");

// Audio role only, consume at the dispatcher boundary before accessing this model.
// true means a new snapshot arrived; ordinary edits preserve audio's live position.
bool sdk_arranger_consume() __asm__("sdk_arranger_consume");
bool sdk_arranger_audio_active() __asm__("sdk_arranger_audio_active");
void sdk_arranger_audio_seek(opxy::u32 time) __asm__("sdk_arranger_audio_seek");
opxy::u32 sdk_arranger_audio_advance(opxy::u32 delta) __asm__("sdk_arranger_audio_advance");
}

// C++ reference return with a fixed native symbol name.
const opxy::arranger::Model& sdk_arranger_audio_model() __asm__("sdk_arranger_audio_model");
