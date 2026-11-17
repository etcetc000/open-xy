// Experimental Morph HUD callbacks, checked by the compositor.
#pragma once
#include <opxy/types.hpp>
extern "C" {
// Visual-only request. Honored only for an actually selected Morph replacement.
extern bool (*sdk_morph_hud_hide_menu)() __asm__("sdk_morph_hud_hide_menu");
extern void (*sdk_morph_scramble_done)(opxy::u32) __asm__("sdk_morph_scramble_done");
opxy::u32 sdk_morph_gui_route_trace(opxy::u32 index) __asm__("sdk_morph_gui_route_trace");
void sdk_morph_gui_turns_reset() __asm__("sdk_morph_gui_turns_reset");
extern opxy::u32 (*sdk_morph_hud_substitute)(opxy::u32) __asm__("sdk_morph_hud_substitute");
opxy::u32 sdk_morph_track_held(opxy::u32 track) __asm__("sdk_morph_track_held");
opxy::u32 sdk_morph_gui_track() __asm__("sdk_morph_gui_track");
opxy::u32 sdk_morph_gui_module() __asm__("sdk_morph_gui_module");
opxy::u32 sdk_morph_gui_key_down(opxy::u32 key) __asm__("sdk_morph_gui_key_down");
opxy::u32 sdk_morph_gui_encoder_down(opxy::u32 index) __asm__("sdk_morph_gui_encoder_down");
extern bool (*sdk_morph_hud_press_route)(opxy::u32) __asm__("sdk_morph_hud_press_route");
extern bool (*sdk_morph_hud_turn_suppress)(opxy::u32) __asm__("sdk_morph_hud_turn_suppress");
extern bool (*sdk_morph_hud_raw_turn_route)(opxy::u32,opxy::u32,opxy::i32,opxy::u32)
    __asm__("sdk_morph_hud_raw_turn_route");
opxy::u32 sdk_morph_gui_ready() __asm__("sdk_morph_gui_ready");
opxy::u32 sdk_morph_gui_shift_held() __asm__("sdk_morph_gui_shift_held");
opxy::u32 sdk_morph_other_track_held(opxy::u32 track) __asm__("sdk_morph_other_track_held");
extern void (*sdk_morph_hud_frame_witness)(opxy::u32,opxy::u32)
    __asm__("sdk_morph_hud_frame_witness");
}
