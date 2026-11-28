// A note player's own drawn page (SDK 1.26): the player declares a display
// (resources.display) and draws its page as a synth draws its screen, a
// draw::Frame (from SDK 1.28 an extended one, which an extended renderer applies,
// extended_retained.hpp) each GUI frame, here from its State and Controls, in place of the
// SDK's dot page (screen_player.hpp). It paints in place of the stock Maestro
// page while the selected track plays the player (players.cpp). Include once,
// from the player's generated unit (player_drawing.cpp.in).
#pragma once
#include <opxy/page_header.hpp>
#include "retained_drawing.hpp"
#include "note_activity.hpp"
#include "amp_envelope.hpp"
#include "players.hpp"

extern "C" opxy::u32 sdk_screen_activation(opxy::u32, opxy::u32) __asm__("sdk_screen_activation");

namespace opxy { namespace screen { namespace {
// Machine: Frame, DisplayState and Input, the display's types; Retained the
// renderer of its frame; ids[] names its nodes, text slots and palette entries
// (native.display_ids); titles[8] its lowercase labels; paged whether a press
// switches between its two pages; full_canvas whether it owns the whole screen,
// without the stock header; vtable() its player method table; bind(object), which
// binds its artwork to the renderer; reset(state, frame),
// snapshot(input), which fills what an extended input appends, and render(object,
// state, controls, input, frame), which call the player's display_reset and display.
template <class Machine> struct PlayerDrawing {
    typedef typename Machine::Frame Frame;
    typedef typename Machine::DisplayState DisplayState;
    typedef typename Machine::Retained Renderer;
    typedef typename Renderer::Object Object;
    static_assert(sizeof(Machine::ids) / sizeof(Machine::ids[0]) ==
                  Frame::node_count + Frame::text_count + Frame::palette_count, "Drawing artwork IDs");

    // One typed GUI state, input and frame, as screen_drawing.hpp keeps; never
    // touched by note callbacks, which own State.
    struct Shared {
        DisplayState state;
        typename Machine::Input input;
        Frame frame;
        u32 activation;
        u32 track;      // the track whose header titles are shown, + 1; 0 before the first
        char values[page_controls][draw::header_capacity];
    };
    static Shared shared;

    static void reset() {
        Machine::reset(shared.state, shared.frame);
        shared.track = 0;
        for (u32 i = 0; i < page_controls; ++i) shared.values[i][0] = 1;   // rewrite every value
    }

    static void init(Object* o) {
        clear(o);
        shared.activation = 0;
        reset();
        if (!o->base.svg) return;
        if (!Machine::full_canvas) bind_controls(&o->base);
        Machine::bind(o);
    }

    // The input a synth's display gets, from the player: its eight controls as the
    // parameter slots, the page shown (on a paged player's second page, slots 4..7),
    // the frame time, the track's note activity and amp envelope.
    static void fill(draw::Input& input, const player::Controls& controls, u32 track, i32 elapsed) {
        const u32 row = Machine::paged ? platform::player_row(track) : 0;
        for (u32 i = 0; i < parameter_slots; ++i) input.slots[i] = controls.value[i];
        for (u32 i = 0; i < page_controls; ++i) {
            input.shown[i] = static_cast<u8>(row + i);
            input.parameters[i] = controls.value[row + i];
        }
        input.elapsed = elapsed;
        input.track = track;
        input.note_serial = track < 8 ? note_serial(track) : 0;
        sdk_display_note_activity(track, &input);
        platform::amp_envelope(track, input.amp);
        input.reduced_motion = 0;
        // Shift as Controls.shift has it; the shift row's values are in Controls.
        input.shift = controls.shift;
        for (u32 i = 0; i < 4; ++i) input.choices[i] = 0;
        input.model = 0;
        input.pan = input.level = 0;
        input.line = static_cast<u8>(row ? 1 : 0);
        input.selected_key = 0;   // a drum kit's only
        for (u32 i = 0; i < 4; ++i) input.pad[i] = 0;
        input.trigger_key = input.trigger_model = 0;
        for (u32 i = 0; i < parameter_slots; ++i) input.trigger_controls[i] = 0;
    }

    // A one-page player keeps the stock header: its labels, and each value the
    // frame's header caption or else the amount 0..100, as a synth's; an absent
    // slot (an empty title) keeps its value empty, as on the dot page. A paged
    // player draws the two-page header in its own frame (opxy/page_header.hpp),
    // and a whole-screen one has none.
    static void header(Object* o, const draw::Input& input) {
        if (Machine::paged || Machine::full_canvas) return;
        if (shared.track != input.track + 1) {
            for (u32 i = 0; i < page_controls; ++i)
                if (Leaf* title = text_node(o->base.titles[i])) set_text(title, Machine::titles[i]);
            shared.track = input.track + 1;
        }
        for (u32 i = 0; i < page_controls; ++i) {
            char caption[draw::header_capacity] = {};
            if (!Machine::titles[i][0]) {
                // An absent slot: its title and value stay empty.
            } else if (shared.frame.header[i][0]) {
                for (u32 k = 0; k + 1 < draw::header_capacity && shared.frame.header[i][k]; ++k)
                    caption[k] = shared.frame.header[i][k];
            } else {
                draw::header_amount(caption, sizeof(caption), input.parameters[i]);
            }
            u32 n = 0;
            while (n < draw::header_capacity && caption[n] == shared.values[i][n] && caption[n]) ++n;
            if (n < draw::header_capacity && caption[n] == shared.values[i][n]) continue;
            for (n = 0; n < draw::header_capacity; ++n) shared.values[i][n] = caption[n];
            if (Leaf* value = text_node(o->base.values[i])) set_text(value, shared.values[i]);
        }
    }

    // Incoming elapsed time is the dispatcher's, in Q10 seconds.
    static void frame(Object* o, i32 elapsed) {
        o->delta = elapsed;
        if (!o->base.svg) return;
        const u32 track = selected_track();
        void* object = platform::player_object(track, Machine::vtable());
        if (object) {
            const u32 activation = sdk_screen_activation(o->base.page, track);
            if (activation && activation != shared.activation) {
                reset();
                // A reset may reuse earlier nonzero geometry keys with new points.
                for (u32 p = 0; p < Frame::path_count; ++p) o->keys[p] = 0;
                shared.activation = activation;
            }
            const player::Controls controls = Machine::controls(object);
            fill(shared.input, controls, track, elapsed);
            Machine::snapshot(shared.input);
            Machine::render(object, shared.state, controls, shared.input, shared.frame);
            if (draw::valid(shared.frame)) {
                Renderer::apply(o, shared.frame);
                header(o, shared.input);
            }
        }
        draw(&o->base);
    }
};
template <class Machine> typename PlayerDrawing<Machine>::Shared PlayerDrawing<Machine>::shared;
}}} // namespace opxy::screen::<unnamed>
