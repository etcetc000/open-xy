#include "arrangement.hpp"
extern "C" {
void sdk_arrangement_stock_raw(const void*) __asm__("sdk_arrangement_stock_raw");
void sdk_arrangement_stock_encoder(opxy::u32, const void*) __asm__("sdk_arrangement_stock_encoder");
void sdk_arrangement_raw(const void*) __asm__("sdk_arrangement_raw");
void sdk_arrangement_encoder(opxy::u32, const void*) __asm__("sdk_arrangement_encoder");
opxy::u32 sdk_arrangement_page(void*) __asm__("sdk_arrangement_page");
}
void sdk_arrangement_raw(const void* raw) {
    if (!sdk_arrangement_consume_raw(raw)) sdk_arrangement_stock_raw(raw);
}
void sdk_arrangement_encoder(opxy::u32 encoder, const void* raw) {
    if (!sdk_arrangement_consume_encoder(encoder, raw)) sdk_arrangement_stock_encoder(encoder, raw);
}
opxy::u32 sdk_arrangement_page(void* controller) { return sdk_arrangement_current_page(controller); }
