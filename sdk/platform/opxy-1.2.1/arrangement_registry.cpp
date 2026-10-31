#include "arrangement.hpp"
#include "arrangement_descriptor.hpp"
void sdk_arrangement_startup() { if (const auto* d = sdk_arrangement_descriptor()) d->startup(); }
int sdk_arrangement_owned(opxy::u32 page) { const auto* d = sdk_arrangement_descriptor(); return d ? d->owned(page) : 0; }
opxy::u32 sdk_arrangement_current_page(void* controller) {
    const auto* d = sdk_arrangement_descriptor(); return d ? d->page(controller) : 47;
}
bool sdk_arrangement_consume_raw(const void* event) {
    const auto* d = sdk_arrangement_descriptor(); return d && d->raw(event);
}
bool sdk_arrangement_consume_encoder(opxy::u32 encoder, const void* event) {
    const auto* d = sdk_arrangement_descriptor(); return d && d->encoder(encoder, event);
}
void sdk_arrangement_poll() { if (const auto* d = sdk_arrangement_descriptor()) d->poll(); }

bool sdk_arrangement_take_intent(opxy::extensions::ArrangementIntent* intent) {
    const auto* d = sdk_arrangement_descriptor(); return d && d->intent(intent);
}
bool sdk_arrangement_post_reply(const opxy::extensions::ArrangementContext& context, const opxy::u8* data, opxy::u32 size) {
    const auto* d = sdk_arrangement_descriptor(); return d && d->reply(context, data, size);
}
