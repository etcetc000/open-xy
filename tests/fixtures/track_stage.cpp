// Host model of the track stage runtime (sdk/platform/opxy-1.2.1/track_stage.hpp):
// the block a track's stage gets after stock's voice loop, with stages that
// record their calls. tests/test_track_stage.py builds and runs it.
#include <cstdio>
#include <cstring>
#include "track_stage.hpp"

using namespace opxy;
namespace stage = opxy::platform::track_stage;

static int failures;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// What the stages saw: resets and processes per track, and the voices passed.
static u32 resets[stage::kTracks], processes[stage::kTracks], seen_voices;
static int registered = 1;
// What a stage writes: `level` in every word (0: silence), or the input doubled.
static i32 level;
static int ready() { return registered; }
static int absent() { return 0; }
static void reset(u32 track) { ++resets[track]; }
static const i32 host_words[58] = {11, 22, 33};
static const i32* seen_words;
static void process(u32 track, i32* sum, const i32* words, u32 voices) {
    ++processes[track];
    seen_words = words;
    seen_voices = voices;
    for (u32 i = 0; i < stage::kWords; ++i) sum[i] = level ? level : sum[i] * 2;
}
// Engines 40 and 41 have stages; 41's synth is not selected (its ready is 0);
// 42's tail is unbounded.
static const stage::Stage stages[] = {
    {reinterpret_cast<const char*>(40), ready, reset, process, 3},
    {reinterpret_cast<const char*>(41), absent, reset, process, 3},
    {reinterpret_cast<const char*>(42), ready, reset, process, stage::kUnbounded},
};

static i32 sum[stage::kWords];
static void fill(i32 value) { for (u32 i = 0; i < stage::kWords; ++i) sum[i] = value; }

int main() {
    stage::Track tracks[stage::kTracks];
    std::memset(tracks, 0, sizeof(tracks));
    // A track whose engine has no stage keeps stock's count and sum, whatever they are.
    fill(1234);
    for (u32 voices = 0; voices < 3; ++voices) CHECK(stage::block(tracks[0], stages, 3, 0, 7, sum, host_words, voices) == voices);
    CHECK(sum[0] == 1234 && sum[127] == 1234 && resets[0] == 0 && processes[0] == 0);
    // Nor does one whose synth this selection leaves out.
    CHECK(stage::block(tracks[1], stages, 3, 1, 41, sum, host_words, 2) == 2);
    CHECK(sum[5] == 1234 && resets[1] == 0 && processes[1] == 0);
    // Engine 40 on track 2: reset once, then processed with its voices; the count stands.
    fill(5);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 4) == 4);
    CHECK(resets[2] == 1 && processes[2] == 1 && seen_voices == 4 && seen_words == host_words && sum[0] == 10 && sum[127] == 10);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 1) == 1);
    CHECK(resets[2] == 1 && processes[2] == 2);
    // No voice and a tail still sounding: the count stock gets is 1.
    fill(0);
    level = 70;
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 1 && processes[2] == 3 && seen_voices == 0);
    // Below the floor (64) but not silent: still 1, and the quiet blocks count: 3 is its hold.
    level = 63;
    for (u32 i = 0; i < 3; ++i) { fill(0); CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 1); }
    CHECK(processes[2] == 6);
    // Quiet for its hold: asleep. It is not called, the sum stays stock's zeros, the count 0.
    fill(0);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 0 && processes[2] == 6 && sum[0] == 0);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 0 && processes[2] == 6);
    // A voice wakes it on the state as it was (no reset).
    level = 0;
    fill(3);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 1) == 1 && processes[2] == 7 && resets[2] == 1 && sum[0] == 6);
    // Output at the floor restarts the quiet count; silence returns 0 without sleeping yet.
    level = 64;
    fill(0);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 1);
    level = 0;
    for (u32 i = 0; i < 3; ++i) { fill(0); CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 0); }
    CHECK(processes[2] == 11);
    fill(0);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 0 && processes[2] == 11);
    // -2^31 counts as loud.
    level = -0x7fffffff - 1;
    fill(0);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 1) == 1);
    fill(0);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 1 && tracks[2].quiet == 0);
    // Another engine, then 40 again: reset again before its next process.
    level = 0;
    CHECK(stage::block(tracks[2], stages, 3, 2, 7, sum, host_words, 0) == 0 && resets[2] == 1);
    CHECK(stage::block(tracks[2], stages, 3, 2, 40, sum, host_words, 0) == 0 && resets[2] == 2 && processes[2] == 14);
    // An unbounded tail never sleeps.
    level = 0;
    for (u32 i = 0; i < 1000; ++i) { fill(0); CHECK(stage::block(tracks[3], stages, 3, 3, 42, sum, host_words, 0) == 0); }
    CHECK(resets[3] == 1 && processes[3] == 1000);
    // A track holding an engine whose synth is not registered (as an unselected one, whose
    // engine number is 0) has no stage.
    registered = 0;
    CHECK(stage::block(tracks[4], stages, 3, 4, 40, sum, host_words, 1) == 1 && resets[4] == 0 && tracks[4].stage == nullptr);
    registered = 1;
    // The first engine a track is seen with is a change, even engine 0.
    CHECK(stage::block(tracks[5], stages, 3, 5, 0, sum, host_words, 2) == 2 && tracks[5].known && tracks[5].stage == nullptr);
    if (failures) return 1;
    std::printf("track stage ok\n");
    return 0;
}
