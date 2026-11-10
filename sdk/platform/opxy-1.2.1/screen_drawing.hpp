// Retained drawing renderer: applies the machine's validated draw::Frame to its
// artwork's path outlines, node transforms and paint, text slots and header
// values. Include after the generated display_binding.hpp, which names the
// frame shape.
#pragma once
#include "retained_drawing.hpp"
#include "screen_header.hpp"
#include "note_activity.hpp"
#include "amp_envelope.hpp"

extern "C" opxy::u32 sdk_screen_activation(opxy::u32, opxy::u32) __asm__("sdk_screen_activation");
// Refreshes opxy_sample_overview for a sample engine's own screen (sample_overview.cpp).
extern "C" void sdk_sample_overview_update(opxy::u32) __asm__("sdk_sample_overview_update");

namespace opxy { namespace screen { namespace {
// One typed GUI state, input and frame; never accessed by audio callbacks.
binding::DisplayState display_state;
u32 display_activation;
draw::Input display_input;
binding::Frame display_frame;

// A drum kit's display is struck only by its selected pad's notes. Audio and
// GUI are separate roles, so this reads only GUI state: when stock's note
// counter moves, the pitches that started since the last frame (newly held,
// or released since) are mapped to their pads. A start no pitch accounts for
// strikes as before, so the screen never misses a hit it cannot place.
struct PadNotes { u32 track, valid, notes, serial; u8 held[128], released[128], now[128], now_released[128]; };
PadNotes pad_notes;
u32 display_note_serial(const Controls& controls) {
    if (controls.track >= 8) return 0;
    const u32 notes = note_serial(controls.track);
    if (!controls.pad) {
        pad_notes.valid = 0;
        return notes;
    }
    PadNotes& s = pad_notes;
    sdk_note_pitches(controls.track, s.now, s.now_released);
    if (s.valid && s.track == controls.track && notes != s.notes) {
        bool placed = false, struck = false;
        for (u32 p = 0; p < 128 && !struck; ++p) {
            const bool released = s.now_released[p] != s.released[p];
            if (!(s.now[p] && !s.held[p]) && !(released && (s.now[p] || !s.held[p]))) continue;
            placed = true;
            struck = sdk_drum_pad(controls.track, p) == controls.pad;
        }
        if (struck || !placed) ++s.serial;
    }
    for (u32 p = 0; p < 128; ++p) {
        s.held[p] = s.now[p];
        s.released[p] = s.now_released[p];
    }
    s.track = controls.track;
    s.notes = notes;
    s.valid = 1;
    return s.serial;
}

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
        display_input.model = controls.model;
        display_input.pan = static_cast<i8>(controls.pan);
        display_input.level = static_cast<i8>(controls.level);
        display_input.line = static_cast<u8>(controls.line);
        display_input.reserved = 0;
        const u32 pads = sizeof(controls.pads) / sizeof(controls.pads[0]);
        for (u32 i = 0; i < 4; ++i) display_input.pad[i] = static_cast<i8>(i < pads ? controls.pads[i] : 0);
        display_input.shift = controls.choosing ? 1 : controls.shift;
        for (u32 i = 0; i < 4; ++i) display_input.choices[i] = controls.choices[i];
        display_input.elapsed = o->delta;
        display_input.track = controls.track;
        display_input.note_serial = display_note_serial(controls);
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
    }
};
}}} // namespace opxy::screen::<unnamed>
