#pragma once
#include <opxy/draw.hpp>

namespace opxy { namespace extensions {
// Optional exclusive Arrange-page owner. Declare "arrangement": true in
// extension.json, claim arrangement_surface, and attach the surface with
// Hooks().surface(&arrangement).
// The SDK supplies neutral path/text/palette artwork matching this fixed frame.
using ArrangementFrame = draw::Frame<128, 4, 128, 80, 16>;
struct ArrangementContext {
    u32 project;       // opaque generation, equality only
    u32 identity;      // opaque project identity, equality only
    u32 track;         // current selected instrument; no selection is changed
    u32 presented;     // wraps; advances only after a valid frame was drawn
    bool visible;
};
// Physical controls, not firmware addresses. Unnamed values are reserved.
// Encoder clicks are 10..13; instrument tracks 14..21; notes26..49; steps56..71.
enum class ArrangementKey : u32 {
    arrange = 4, encoder1 = 10, encoder2 = 11, encoder3 = 12, encoder4 = 13,
    track1 = 14, track8 = 21, sample = 23, note_first = 26, note_last = 49,
    record = 50, play = 51, stop = 52, minus = 53, plus = 54, shift = 55,
    step_first = 56, step_last = 71
};
struct ArrangementKeyEvent { ArrangementKey key; bool pressed, shifted; };
struct ArrangementTurn { u32 encoder; i32 delta; bool shifted; };
struct Context;
struct Actions;
struct PatternReadResult;
struct ArrangementIntent { ArrangementContext origin; u32 code, values[4]; };
struct ArrangementActions {
    // One pending copied intent; false means busy. Only during key/turn/tick.
    bool (*submit)(u32 code, const u32* values, u32 count);
};
struct ArrangementSurface {
    // Queried before a project exists too. False leaves the stock Song page.
    bool (*enabled)(const ArrangementContext&);
    // Called on visibility/project transitions, including before losing input.
    void (*context)(const ArrangementContext&);
    // Immutable capture masks: keys0..71 (three 32-bit words), encoders0..3.
    // Keyboard26..49 cannot be captured; an invalid mask disables the surface.
    // Unclaimed controls stay stock. Queue-full new presses stay stock too.
    u32 keys[3], encoders;
    // Paired events are delivered later on GUI poll. A captured release stays
    // captured after leaving the page; stale-project events are discarded.
    void (*key)(const ArrangementContext&, const ArrangementKeyEvent&, const ArrangementActions&);
    void (*turn)(const ArrangementContext&, const ArrangementTurn&, const ArrangementActions&);
    // GUI poll, outside key handling/rendering. presented allows transactions
    // to wait for their previous frame without performing writes during draw.
    void (*tick)(const ArrangementContext&, const ArrangementActions&);
    void (*render)(const ArrangementContext&, i32 elapsed_q10, ArrangementFrame&);
    // Controller task, not GUI. Do not access GUI-owned editor/render state.
    // Native note-writing Actions are never granted here. Use declared deferred
    // services and post_arrangement_reply to return copied data to the GUI.
    void (*intent)(const Context&, const ArrangementIntent&, const Actions&);
    // GUI poll; borrowed copied response, at most 2048 bytes. No retained pointer.
    void (*reply)(const ArrangementContext&, const u8*, u32);
    // Controller-side result observer; only surface_reply is granted here.
    void (*pattern_result)(const PatternReadResult&, const Actions&);
};
// All callbacks except intent and pattern_result run on the GUI path. No allocation, blocking, I/O or retained
// pointers to callback arguments. Project edits require separate SDK services.
}}
