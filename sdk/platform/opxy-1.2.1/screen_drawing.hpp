// Retained drawing renderer: applies the machine's validated draw::Frame to its
// artwork's path outlines, node transforms and paint, text slots and header
// values. Include after the generated display_binding.hpp, which names the
// frame shape.
#pragma once
#include "retained_drawing.hpp"
#include "screen_header.hpp"
#include "note_activity.hpp"
#include "amp_envelope.hpp"
#include "drum_send.hpp"

extern "C" opxy::u32 sdk_screen_activation(opxy::u32, opxy::u32) __asm__("sdk_screen_activation");
// Refreshes opxy_sample_overview for a sample engine's own screen (sample_overview.cpp).
extern "C" void sdk_sample_overview_update(opxy::u32) __asm__("sdk_sample_overview_update");

namespace opxy { namespace screen { namespace {
// One typed GUI state, input and frame; never accessed by audio callbacks.
binding::DisplayState display_state;
u32 display_activation;
draw::Input display_input;
binding::Frame display_frame;

// Stock counts both live and sequenced note starts. Selection stays in the
// control snapshot; the latest played drum is supplied independently below.
u32 display_note_serial(const Controls& controls) {
    return controls.track < 8 ? note_serial(controls.track) : 0;
}
extern "C" void sdk_drum_trigger(u32, draw::Input*) __asm__("sdk_drum_trigger");

// Machine: ids[] names mutable paths, other nodes, text slots, then palette entries.
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
        display_input.model = controls.model;
        display_input.pan = static_cast<i8>(controls.pan);
        display_input.level = static_cast<i8>(controls.level);
        display_input.line = static_cast<u8>(controls.line);
        display_input.reserved = 0;
        const u32 pads = sizeof(controls.pads) / sizeof(controls.pads[0]);
        for (u32 i = 0; i < 4; ++i) display_input.pad[i] = static_cast<i8>(i < pads ? controls.pads[i] : 0);
        // FX I and FX II: a send's index (drum_send.hpp) as the kit's SDK reads it.
        for (u32 i = platform::kDrumKitPadFields; i < 4 && i < pads; ++i)
            display_input.pad[i] = static_cast<i8>(platform::drum_send_shown(static_cast<u32>(controls.pads[i]), Machine::percent_sends));
        display_input.shift = controls.choosing ? 1 : controls.shift;
        for (u32 i = 0; i < 4; ++i) display_input.choices[i] = controls.choices[i];
        display_input.elapsed = o->delta;
        display_input.track = controls.track;
        display_input.note_serial = display_note_serial(controls);
        display_input.trigger_key = display_input.trigger_model = 0;
        for (u32 i = 0; i < parameter_slots; ++i) display_input.trigger_controls[i] = 0;
        if (Machine::drum) sdk_drum_trigger(controls.track, &display_input);
        sdk_display_note_activity(controls.track, &display_input);
        // A sample engine's own screen reads its sample (opxy_sample_overview).
        if (Machine::sample) sdk_sample_overview_update(controls.track);
        platform::amp_envelope(controls.track, display_input.amp);
        display_input.reduced_motion = 0;
        binding::display_render(&display_state, &display_input, &display_frame);
        if (!binding::display_valid(&display_frame)) return;
        Renderer::apply(o,display_frame);
        // Keep the drum Shift page blank; other screens share header overrides.
        if (!controls.choosing) apply_header(&o->base, display_frame);
    }

    // Incoming elapsed time is the dispatcher's, in Q10 seconds.
    static void frame(Object* o, i32 elapsed) {
        o->delta = elapsed;
        Controls controls;
        if (!refresh_controls(&o->base, controls)) return;
        animate(o, controls);
        draw(&o->base);
        // A drum kit's held pad: Drumline's slide-up strip, over the page.
        draw_hold_strip(controls.track, elapsed);
    }
};
}}} // namespace opxy::screen::<unnamed>
