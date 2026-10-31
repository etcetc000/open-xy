#pragma once
#include "arranger_bounce_wire.hpp"
extern "C" {
void sdk_arranger_bounce_message(const void*, opxy::u32) __asm__("sdk_arranger_bounce_message");
void sdk_arranger_bounce_remote_poll() __asm__("sdk_arranger_bounce_remote_poll");
void sdk_arranger_bounce_trim(opxy::u32) __asm__("sdk_arranger_bounce_trim");
void sdk_arranger_bounce_remote_saved(opxy::u32) __asm__("sdk_arranger_bounce_remote_saved");
void sdk_arranger_bounce_send(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_arranger_bounce_send");
void sdk_arranger_bounce_receive(const opxy::platform::bounce_wire::Message*)
    __asm__("sdk_arranger_bounce_receive");
}
