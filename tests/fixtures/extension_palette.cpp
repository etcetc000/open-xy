#include <extension_palette.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
u32 first_revision = 1, second_revision = 1;
void first_map(extensions::Color& color) { color.red = static_cast<u8>(color.red + first_revision); }
void second_map(extensions::Color& color) { color.blue = static_cast<u8>(color.blue + second_revision); }
u32 first_epoch() { return first_revision; }
u32 second_epoch() { return second_revision; }
void context(const extensions::Context&) {}
constexpr extensions::Palette first = {first_epoch, first_map};
constexpr extensions::Palette second = {second_epoch, second_map};
const extensions::Hooks first_hooks = extensions::Hooks().palette(&first).with_context(context);
const extensions::Hooks second_hooks = extensions::Hooks().with_context(context).palette(&second);
ExtensionEntry entries[] = {{"first", 0, 0, &first_hooks}, {"second", 0, 0, &second_hooks}};
extensions::Color fills[] = {{10, 20, 30, 255}, {40, 50, 60, 255}};
struct Stroke { extensions::Color color; u32 rest[4]; };
Stroke strokes[] = {{{70, 80, 90, 255}, {0, 0, 0, 0}}};
PaletteDocument live_documents[] = {{reinterpret_cast<const void*>(1), {fills, 2, 4}, {&strokes[0].color, 1, sizeof(Stroke)}}};
u32 document_count = 1;
}
namespace opxy { namespace platform {
u32 extension_palette_documents(PaletteDocument* out, u32 capacity) {
    if (capacity < document_count) return 0;
    for (u32 i = 0; i < document_count; ++i) out[i] = live_documents[i];
    return document_count;
}
}}
int main() {
    extension_palette_sync(entries, 2);
    CHECK(fills[0].red == 11 && fills[0].blue == 31 && fills[1].red == 41 && fills[1].blue == 61);
    CHECK(strokes[0].color.red == 71 && strokes[0].color.blue == 91);
    // A new revision always maps the cached source, not the already themed colour.
    first_revision = 7; second_revision = 9;
    extension_palette_sync(entries, 2);
    CHECK(fills[0].red == 17 && fills[0].blue == 39 && strokes[0].color.red == 77 && strokes[0].color.blue == 99);
    // A document loaded after the revision receives the current composition once.
    extensions::Color later[] = {{1, 2, 3, 255}};
    live_documents[0] = {reinterpret_cast<const void*>(2), {later, 1, 4}, {}};
    extension_palette_sync(entries, 2);
    CHECK(later[0].red == 8 && later[0].blue == 12);
    return 0;
}
