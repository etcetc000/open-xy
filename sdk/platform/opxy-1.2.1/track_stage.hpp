// A synth's track stage (resources.trackStage, SDK 1.24; docs/api.md#track-stage):
// one state per instrument track holding the synth, processed once per block in
// place on the track's stereo voice sum, after stock's voice loop has added every
// voice into it and before the track's gain, meter and buses read it.
//
// Stock's mixer (8d510760) clears each track's sum (11903600, 64 interleaved
// stereo frames) and calls the track's voice loop (8d51c708) every block, for
// silent tracks too, then decides from the voice count the loop returns (and a
// follower of the sum's first word) whether the track still sounds: a track that
// rendered no voice and whose follower has fallen for two blocks skips its gain,
// meter and bus mixes. The shared track call handler (track_call.s) runs the loop
// and hands this runtime its count; the count it returns is what stock decides on.
// A stage still sounding after its voices end returns 1, so stock keeps mixing the
// tail. On every other track the count and the sum are stock's own.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace track_stage {
const u32 kTracks = 8;          // the instrument tracks stock's mixer runs
const u32 kWords = 128;         // a track's sum: 64 interleaved left/right frames
const i32 kFloor = 64;          // the idle floor, as for idle sends: silence on a bus
const u32 kUnbounded = 0xffffffffu;  // the hold of a stage that never sleeps

// A track's words on the audio side, 58 Q31 words at 84eab39c + 0xe8 track: the
// pointer stock's renderer passes a synth voice's render as its parameters
// (11a09652), with parameter locks and LFO modulation as stock applies them.
inline const i32* words(u32 track) {
    return reinterpret_cast<const i32*>(static_cast<__UINTPTR_TYPE__>(0x84eab39cu + track * 0xe8u));
}

// One synth's stage, as its voice adapter defines it (voice.cpp.in TRACK_STAGE)
// and its record holds it for the stage table (machine_tables.cpp).
struct Stage {
    const char* engine;   // the synth's engine number, as a link-time address
    int (*ready)();       // 1 once the synth is registered (0: not yet, or no longer ours)
    void (*reset)(u32 track);                          // zero the track's state, then track_reset
    void (*process)(u32 track, i32* sum, const i32* words, u32 voices);  // track_process
    u32 hold;             // quiet blocks before it sleeps: its tail in blocks plus 64, or kUnbounded
                          // (tooling/track_stage.py hold)
};

// Each instrument track's view of its stage, kept by the runtime (track_stage.cpp).
struct Track {
    const Stage* stage;  // the stage of `engine`, or null
    u32 engine;          // the engine the track held at its last block
    u32 quiet;           // blocks in a row without voices and with the output below the floor
    bool known;          // `stage` is the stage of `engine`
};

// The stage a track holding `engine` runs, or null. `settled` is false while the
// engine's synth has not registered yet, so the track looks again next block. A
// synth this build's selection leaves out has engine number 0 and a ready check
// that answers 0: never a stage, and nothing to wait for.
inline const Stage* find(const Stage* stages, u32 count, u32 engine, bool& settled) {
    settled = true;
    for (u32 i = 0; i < count; ++i) {
        const u32 number = static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(stages[i].engine));
        if (number != engine || !number) continue;
        if (stages[i].ready() == 1) return &stages[i];
        settled = false;
    }
    return nullptr;
}

// The largest magnitude in a sum; -2^31 counts as loud.
inline i32 peak(const i32* sum) {
    i32 most = 0;
    for (u32 i = 0; i < kWords; ++i) {
        const i32 x = sum[i];
        const i32 magnitude = x < 0 ? (x == -0x7fffffff - 1 ? 0x7fffffff : -x) : x;
        if (magnitude > most) most = magnitude;
    }
    return most;
}

// One block of `track`, which holds `engine`, after stock's voice loop rendered
// `voices` voices into `sum`; `words` are the track's words (words(track) on the
// device). Returns the voice count stock's silence check gets.
// A change of engine resets the stage of the new one before it processes. With
// no voice, stock's loop added nothing, so the sum is the zeros stock cleared it
// to: a stage that has been quiet for its hold sleeps then, touching nothing,
// until a block with a voice wakes it on its state as it was.
inline u32 block(Track& t, const Stage* stages, u32 count, u32 track, u32 engine, i32* sum, const i32* words,
                 u32 voices) {
    if (!t.known || engine != t.engine) {
        t.engine = engine;
        t.stage = find(stages, count, engine, t.known);
        t.quiet = 0;
        if (t.stage) t.stage->reset(track);
    }
    const Stage* stage = t.stage;
    if (!stage) return voices;
    if (!voices && stage->hold != kUnbounded && t.quiet >= stage->hold) return 0;
    stage->process(track, sum, words, voices);
    if (voices) {
        t.quiet = 0;
        return voices;
    }
    const i32 loudest = peak(sum);
    if (loudest >= kFloor) t.quiet = 0;
    else if (t.quiet < stage->hold) ++t.quiet;
    return loudest != 0;
}
}}}

extern "C" {
// The selected declaring synths' stages, built at startup from their records (machine_tables.cpp).
extern const opxy::platform::track_stage::Stage* sdk_track_stages __asm__("sdk_track_stages");
extern opxy::u32 sdk_track_stage_count __asm__("sdk_track_stage_count");
}
