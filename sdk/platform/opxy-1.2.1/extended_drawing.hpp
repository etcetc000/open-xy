// Extended drawing renderer (docs/display.md#extended-drawing): applies
// an extended frame (draw.hpp ExtendedFrame, RgbaFrame) to a synth's, an
// effect's or a drum kit's artwork. Only an extended display's generated
// screen.cpp includes this, after its display_binding.hpp; plain displays keep
// screen_drawing.hpp.
#pragma once
#include "screen.hpp"
#include "screen_header.hpp"
#include "note_activity.hpp"
#include "amp_envelope.hpp"
#include "drum_send.hpp"
#include "extended_retained.hpp"
#include "kit_screen.hpp"

extern "C" opxy::u32 sdk_screen_activation(opxy::u32, opxy::u32) __asm__("sdk_screen_activation");

namespace opxy { namespace screen { namespace {
// One typed GUI state, input and frame; never accessed by audio callbacks.
binding::DisplayState extended_state;
u32 extended_activation;
binding::DisplayInput extended_input;
binding::Frame extended_frame;

// A whole-screen canvas has no stock title or value leaves: the page's
// controls as refresh_controls reads them for a synth or an effect, without
// writing any stock text.
inline bool canvas_controls(Base* o, Controls& controls) {
    if (!o->svg) return false;
    controls.track = selected_track();
    controls.shift = 0;
    controls.choosing = false;
    controls.model = controls.pad = 0;
    controls.pan = controls.level = 0;
    controls.line = 0;
    for (u32 i = 0; i < platform::kDrumPadFields; ++i) controls.pads[i] = 0;
    for (u32 i = 0; i < 4; ++i) controls.choices[i] = 0;
    platform::SendView view;
    if (sdk_send_view(controls.track, &view)) {
        for (u32 i = 0; i < parameter_slots; ++i) controls.slots[i] = view.slots[i];
        for (u32 i = 0; i < page_controls; ++i) {
            controls.shown[i] = view.shown[i];
            controls.values[i] = view.values[i];
        }
        controls.shift = view.shift;
        for (u32 i = 0; i < 4; ++i) controls.choices[i] = view.choices[i];
        controls.valid = true;
        return true;
    }
    sdk_synth_shown(controls.track, controls.shown);
    controls.line = controls.shown[0] >= page_controls ? 1 : 0;
    for (u32 i = 0; i < page_controls; ++i) controls.values[i] = 0;
    for (u32 i = 0; i < parameter_slots; ++i) controls.slots[i] = 0;
    u32 words[parameter_slots];
    controls.valid = read_shown(parameters(controls.track), controls, words);
    return true;
}

// A whole-screen drum kit's header (fullCanvas on a kit): the SDK header's bar,
// values and titles, composed over the kit's drawing (tooling/screen.py kit_header)
// and shown only while Shift is held, where the kit's Shift page names what Shift
// chooses. Otherwise the bar is hidden and the eight texts are blank, written
// after refresh_controls each frame. Other screens compile none of this.
template <bool Kit> struct CanvasKitHeader {
    static void clear() {}
    static void init(Base*) {}
    static void show(Base*, bool) {}
};
struct CanvasKitBar { PathNode* nodes[8]; u8 visibility[8]; };
CanvasKitBar canvas_kit_bar;
template <> struct CanvasKitHeader<true> {
    // No bar until the artwork has loaded: nothing kept from an earlier one.
    static void clear() { for (u32 i = 0; i < 8; ++i) canvas_kit_bar.nodes[i] = 0; }
    // After bind_controls: the bar's eight blocks, hidden until Shift is held.
    static void init(Base* o) {
        static const char* const ids[8] = {"bar_0", "bar_1", "bar_2", "bar_3", "bar_4", "bar_5", "bar_6", "bar_7"};
        CanvasKitBar& bar = canvas_kit_bar;
        for (u32 i = 0; i < 8; ++i) {
            bar.nodes[i] = path_node(find(o, ids[i]));
            bar.visibility[i] = bar.nodes[i] ? reinterpret_cast<u8*>(bar.nodes[i])[visibility_byte] : 0;
        }
        show(o, false);
    }
    static void show(Base* o, bool shifted) {
        CanvasKitBar& bar = canvas_kit_bar;
        for (u32 i = 0; i < 8; ++i)
            if (bar.nodes[i]) reinterpret_cast<u8*>(bar.nodes[i])[visibility_byte] = shifted ? bar.visibility[i] : 0;
        if (shifted) return;
        for (u32 i = 0; i < page_controls; ++i) {
            if (Leaf* title = text_node(o->titles[i])) set_text(title, "");
            if (Leaf* value = text_node(o->values[i])) set_text(value, "");
        }
    }
};

// Machine: ids[] names the artwork's nodes (the first are the mutable paths),
// then its text slots, then its palette entries; kit whether it is a drum kit's
// screen, with played_pad, selected_key and kit_controls as screen_drawing.hpp's.
template <class Machine> struct ExtendedDrawing {
    typedef binding::Frame Frame;
    typedef ExtendedRetained<Frame> Renderer;
    typedef typename Renderer::Object Object;
    static_assert(sizeof(Machine::ids) / sizeof(Machine::ids[0]) ==
                  Frame::node_count + Frame::text_count + Frame::palette_count, "Drawing artwork IDs");
    // A synth's or an effect's whole-screen canvas has no stock header; a kit's has
    // the one it shows while Shift is held.
    static const bool canvas = binding::full_canvas && !Machine::kit;
    typedef CanvasKitHeader<binding::full_canvas && Machine::kit> KitHeader;

    static void init(Object* o) {
        clear(o);
        extended_activation = 0;
        if (Machine::played_pad) played.valid = 0;
        KitHeader::clear();
        binding::display_reset(&extended_state, &extended_frame);
        if (!o->base.svg) return;
        if (!canvas) bind_controls(&o->base);
        Renderer::template bind<Machine>(o);
        KitHeader::init(&o->base);
    }

    static void animate(Object* o, const Controls& controls) {
        if (!controls.valid) return;
        const u32 activation = sdk_screen_activation(o->base.page, controls.track);
        if (activation && activation != extended_activation) {
            binding::display_reset(&extended_state, &extended_frame);
            // A reset may reuse earlier nonzero geometry keys with new points.
            for (u32 p = 0; p < Frame::path_count; ++p) o->keys[p] = 0;
            extended_activation = activation;
            if (Machine::played_pad) played.valid = 0;
        }
        draw::Input& input = extended_input;
        for (u32 i = 0; i < page_controls; ++i) {
            input.parameters[i] = controls.values[i];
            input.shown[i] = controls.shown[i];
        }
        for (u32 i = 0; i < parameter_slots; ++i) input.slots[i] = controls.slots[i];
        input.model = controls.model;
        input.pan = static_cast<i8>(controls.pan);
        input.level = static_cast<i8>(controls.level);
        input.line = static_cast<u8>(controls.line);
        // A kit's selected key, 0..23 (pad 53 + n); 0 on other screens.
        input.selected_key = Machine::selected_key && controls.pad >= 53 && controls.pad < 77
            ? static_cast<u8>(controls.pad - 53) : 0;
        const u32 pads = sizeof(controls.pads) / sizeof(controls.pads[0]);
        for (u32 i = 0; i < 4; ++i) input.pad[i] = static_cast<i8>(i < pads ? controls.pads[i] : 0);
        for (u32 i = platform::kDrumKitPadFields; i < 4 && i < pads; ++i)
            input.pad[i] = static_cast<i8>(platform::drum_send_shown(static_cast<u32>(controls.pads[i]), Machine::percent_sends));
        input.shift = controls.choosing ? 1 : controls.shift;
        for (u32 i = 0; i < 4; ++i) input.choices[i] = controls.choices[i];
        input.elapsed = o->delta;
        input.track = controls.track;
        input.note_serial = controls.track < 8 ? note_serial(controls.track) : 0;
        // A kit's played-pad snapshot (kit_screen.hpp); other screens' stays zero.
        if (Machine::played_pad) played_pad_snapshot(controls, input.note_serial, input);
        sdk_display_note_activity(controls.track, &input);
        platform::amp_envelope(controls.track, input.amp);
        input.reduced_motion = 0;
        binding::snapshot_encoders(extended_input);
        binding::display_render(&extended_state, &extended_input, &extended_frame);
        if (!binding::display_valid(&extended_frame)) return;
        Renderer::apply(o, extended_frame);
        if (!binding::full_canvas && !controls.choosing) apply_header(&o->base, extended_frame);
    }

    // Incoming elapsed time is the dispatcher's, in Q10 seconds.
    static void frame(Object* o, i32 elapsed) {
        o->delta = elapsed;
        Controls controls;
        if (!(canvas ? canvas_controls(&o->base, controls) : refresh_controls(&o->base, controls))) return;
        if (Machine::kit_controls) kit_cells(&o->base, controls);
        KitHeader::show(&o->base, controls.choosing);
        animate(o, controls);
        draw(&o->base);
        // A drum kit's held pad: Drumline's slide-up strip, over the page.
        if (Machine::kit) draw_hold_strip(controls.track, elapsed);
    }
};
}}} // namespace opxy::screen::<unnamed>
