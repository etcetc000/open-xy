#pragma once
#include <opxy/arrangement.hpp>
namespace opxy { namespace platform {
// SDK-generated callbacks, never authored by the extension. Keeping this in
// the owner's object lets an omitted extension omit all renderer state too.
struct ArrangementDescriptor {
    void (*startup)();
    int (*owned)(u32);
    u32 (*page)(void*);
    bool (*raw)(const void*);
    bool (*encoder)(u32, const void*);
    void (*poll)();
    bool (*intent)(extensions::ArrangementIntent*);
    bool (*reply)(const extensions::ArrangementContext&, const u8*, u32);
};
}}
extern "C" const opxy::platform::ArrangementDescriptor* sdk_arrangement_descriptor() __asm__("sdk_arrangement_descriptor");
extern "C" bool sdk_arrangement_take_intent(opxy::extensions::ArrangementIntent*) __asm__("sdk_arrangement_take_intent");
extern "C" bool sdk_arrangement_post_reply(const opxy::extensions::ArrangementContext&, const opxy::u8*, opxy::u32) __asm__("sdk_arrangement_post_reply");
