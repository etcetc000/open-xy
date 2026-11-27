// A drum kit's screen: what its renderer adds to a synth's, shared by the plain renderer
// (screen_drawing.hpp) and the extended one (extended_drawing.hpp): the played-pad snapshot
// (SDK 1.21) and the Shift cells of the drum-kit controls (SDK 1.23). Include after screen.hpp
// and note_activity.hpp, inside nothing.
#pragma once

// A drum-kit control's Shift cell (drum_kit_controls.hpp; reached only by a kit declaring one).
extern "C" bool sdk_drum_kit_readout(opxy::u32 track, opxy::u32 position, opxy::u8* choice, const char** title,
                                     char* value) __asm__("sdk_drum_kit_readout");

namespace opxy { namespace screen { namespace {
extern "C" void sdk_drum_trigger(u32, draw::Input*) __asm__("sdk_drum_trigger");
// A 1.21 kit's played-pad snapshot names no pad (all zero) until the track's
// serial moves after the screen's activation or a track change, so the screen
// never shows a pad from stock's last-played byte before its first hit there
// (that byte keeps an earlier track's or kit's key, or whatever it holds at
// boot). Like the serial's own baseline, it fabricates no note. serial is the
// value the frame gives the machine as note_serial, read once: a second read
// could see a note start the machine's serial does not, and the machine would
// then see its serial move with the snapshot still zero.
struct PlayedPad { u32 track, serial, valid, moved; };
PlayedPad played;
void played_pad_snapshot(const Controls& controls, u32 serial, draw::Input& input) {
    PlayedPad& s = played;
    if (!s.valid || s.track != controls.track) s = {controls.track, serial, 1, 0};
    else if (serial != s.serial) s.moved = 1;
    if (s.moved) { sdk_drum_trigger(controls.track, &input); return; }
    input.trigger_key = input.trigger_model = 0;
    for (u32 i = 0; i < 8; ++i) input.trigger_controls[i] = 0;
}

// A kit's Shift cells of the drum-kit controls (padVolume, padRandom; SDK 1.23,
// drum_kit_controls.cpp): each position the kit sets something on holds its choice in
// controls.choices at all times (the key's volume as dB + 30, its random's percent)
// and, while Shift is held, names and shows it, over what refresh_controls put there.
void kit_cells(Base* o, Controls& controls) {
    if (!controls.valid || !controls.pad) return;
    for (u32 i = 0; i < 4; ++i) {
        u8 choice = 0;
        const char* title = "";
        char value[16];
        value[0] = 0;
        if (!sdk_drum_kit_readout(controls.track, i, &choice, &title, value)) continue;
        controls.choices[i] = choice;
        if (!controls.choosing) continue;
        value[15] = 0;
        set_text(o->titles[i], title);
        set_text(o->values[i], value);
    }
}
}}} // namespace opxy::screen::<unnamed>
