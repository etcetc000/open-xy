#pragma once
#include <opxy/arrangement.hpp>
extern "C" {
const opxy::extensions::ArrangementSurface* sdk_arrangement_owner() __asm__("sdk_arrangement_owner");
void sdk_arrangement_startup() __asm__("sdk_arrangement_startup");
int sdk_arrangement_owned(opxy::u32) __asm__("sdk_arrangement_owned");
opxy::u32 sdk_arrangement_current_page(void*) __asm__("sdk_arrangement_current_page");
bool sdk_arrangement_consume_raw(const void*) __asm__("sdk_arrangement_consume_raw");
bool sdk_arrangement_consume_encoder(opxy::u32, const void*) __asm__("sdk_arrangement_consume_encoder");
void sdk_arrangement_poll() __asm__("sdk_arrangement_poll");
}
