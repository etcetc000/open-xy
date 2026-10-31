// Retained drawing renderer: applies the machine's validated draw::Frame to its
// artwork's path outlines, node transforms and paint, text slots and header
// values. Include after the generated display_binding.hpp, which names the
// frame shape.
#pragma once
#include "retained_drawing.hpp"
#include "screen_header.hpp"
#include "note_activity.hpp"

extern "C" opxy::u32 sdk_screen_activation(opxy::u32, opxy::u32) __asm__("sdk_screen_activation");

namespace opxy { namespace screen { namespace {
// One typed GUI state, input and frame; never accessed by audio callbacks.
binding::DisplayState display_state;
u32 display_activation;
draw::Input display_input;
binding::Frame display_frame;

// Machine: ids[] names the artwork's nodes (the first are the mutable paths),
// then its text slots, then its palette entries.
template <class Machine> struct Drawing {
    typedef binding::Frame Frame;
    enum { path_count = Frame::path_count, point_count = Frame::point_count, node_count = Frame::node_count,
           text_count = Frame::text_count, palette_count = Frame::palette_count };
    static_assert(sizeof(Machine::ids) / sizeof(Machine::ids[0]) == node_count + text_count + palette_count,
                  "Drawing artwork IDs");
    using Renderer = RetainedDrawing<Frame>;
    using Object = typename Renderer::Object;
    static void init(Object* o) {
        clear(o);
        display_activation=0;
        binding::display_reset(&display_state,&display_frame);
        if(!o->base.svg)return;
        bind_controls(&o->base);
        Renderer::bind(o,Machine::ids);
    }

    static void animate(Object* o, const Controls& controls) {
        if (!controls.valid) return;
        const u32 activation = sdk_screen_activation(o->base.page, controls.track);
        if (activation && activation != display_activation) {
            binding::display_reset(&display_state, &display_frame);
            // A reset may reuse earlier nonzero geometry keys with new points.
            for (u32 p = 0; p < path_count; ++p) o->keys[p] = 0;
            display_activation = activation;
        }
        for (u32 i = 0; i < page_controls; ++i) {
            display_input.parameters[i] = controls.values[i];
            display_input.shown[i] = controls.shown[i];
        }
        for (u32 i = 0; i < parameter_slots; ++i) display_input.slots[i] = controls.slots[i];
        display_input.elapsed = o->delta;
        display_input.track = controls.track;
        display_input.note_serial = controls.track < 8 ? note_serial(controls.track) : 0;
        sdk_display_note_activity(controls.track, &display_input);
        display_input.reduced_motion = 0;
        binding::display_render(&display_state, &display_input, &display_frame);
        if (!binding::display_valid(&display_frame)) return;
        Renderer::apply(o,display_frame);
        // Stock wrote every value this frame; a header string replaces its own.
        apply_header(&o->base, display_frame);
    }

    // Incoming elapsed time is the dispatcher's, in Q10 seconds.
    static void frame(Object* o, i32 elapsed) {
        o->delta = elapsed;
        Controls controls;
        if (!refresh_controls(&o->base, controls)) return;
        animate(o, controls);
        draw(&o->base);
    }
};
}}} // namespace opxy::screen::<unnamed>
