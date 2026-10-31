// Shared retained drawing host. Typed binding and author source are generated.
#include "master_filter.hpp"
#include "retained_drawing.hpp"
#include "screen_header.hpp"
using namespace opxy;
using namespace opxy::screen;
namespace opxy {
namespace platform {
namespace master_filter {
namespace {
using Renderer = RetainedDrawing<binding::Frame>;
using Object = Renderer::Object;
Object object;
binding::DisplayState state;
binding::Frame output;
bool constructed, bound, reset_pending = true;
void init(Object *o) {
    clear(o);
    if (o->base.svg) {
        Renderer::bind(o, master_display_ids);
        bind_authored_header(&o->base, master_header_ids);
    }
}
void unused_frame(Object *, i32) {}
const VTable<Object> methods = {&destroy<Object>,
                                &destroy<Object>,
                                reinterpret_cast<const void *>(0x8d51fcd4),
                                unused_frame,
                                init,
                                reinterpret_cast<const void *>(0x8c0867f0)};
void blank() {
    i32 a[2] = {-4096, 110 * 1024}, b[2] = {484 * 1024, 110 * 1024};
    const u32 black = 0xff000000;
    stock<void(i32 *, i32 *, i32, const u32 *)>(0x8d5fe884)(a, b, 224 * 1024, &black);
}
} // namespace
void frame(i32 elapsed) {
    if (!descriptor()) {
        blank();
        return;
    }
    if (!constructed) {
        base_construct(&object.base, 0, asset());
        object.base.vtable = &methods;
        constructed = true;
    }
    if (!bound) {
        if (!object.base.svg)
            stock<void(Base *)>(0x8d520ce0)(&object.base);
        if (object.base.svg) {
            init(&object);
            bound = true;
        }
    }
    if (!bound) {
        blank();
        return;
    }
    if (reset_pending) {
        binding::display_reset(&state, &output);
        for (u32 i = 0; i < binding::Frame::path_count; ++i)
            object.keys[i] = 0;
        reset_pending = false;
    }
    draw::Input input = {};
    input.elapsed = elapsed;
    input.track = 0xffffffffu;
    input.held_notes = draw::notes_unavailable;
    for (u32 i = 0; i < parameter_slots; ++i)
        input.slots[i] = menu.values[i];
    for (u32 i = 0; i < page_controls; ++i) {
        input.shown[i] = menu.shown[i];
        input.parameters[i] = menu.values[menu.shown[i]];
    }
    u32 count = 0;
    const Parameter* declared = parameters(count);
    refresh_authored_header(&object.base, declared, count, input);
    binding::display_render(&state, &input, &output);
    Renderer::apply(&object, output);
    apply_header(&object.base, output);
    opxy::screen::draw(&object.base);
}
void parameter_reset(u32 slot) {
    if (!reset_pending)
        draw::parameter_reset(state, slot);
}
void reset_project() { reset_pending = true; }
} // namespace master_filter
} // namespace platform
} // namespace opxy
