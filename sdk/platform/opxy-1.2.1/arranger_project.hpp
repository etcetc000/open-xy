#pragma once
#include <opxy/types.hpp>
extern "C" {
// GUI role: persisted native Project > General arranger mode.
bool sdk_arranger_project_edit_ready() __asm__("sdk_arranger_project_edit_ready");
bool sdk_arranger_project_linear() __asm__("sdk_arranger_project_linear");
bool sdk_arranger_project_set_linear(bool linear) __asm__("sdk_arranger_project_set_linear");
// 0 normal, 1 failed restore (saves blocked), 2 save failed, 3 failed companion rename (saves blocked).
opxy::u32 sdk_arranger_project_status() __asm__("sdk_arranger_project_status");
// MTP file allowlist keeps stock paths and admits their exact arrangement companions.
opxy::u32 sdk_arranger_mtp_match(const char* pattern, const void* path) __asm__("sdk_arranger_mtp_match");
void sdk_arranger_general_rows(void* rows, const void* row) __asm__("sdk_arranger_general_rows");
void sdk_arranger_general_encoder(void* object, opxy::u32 encoder, const void* event) __asm__("sdk_arranger_general_encoder");
// After successful changes, and once each GUI tick respectively.
bool sdk_arranger_project_changed(bool active, opxy::u32 track) __asm__("sdk_arranger_project_changed");
void sdk_arranger_project_poll() __asm__("sdk_arranger_project_poll");
// Stock file workflow wrappers; no audio-role I/O.
opxy::u32 sdk_arranger_project_rename(const char* from, const char* to) __asm__("sdk_arranger_project_rename");
opxy::u32 sdk_arranger_project_save(const char* path) __asm__("sdk_arranger_project_save");
opxy::u32 sdk_arranger_project_load(const char* path, opxy::u32 commit) __asm__("sdk_arranger_project_load");
}
