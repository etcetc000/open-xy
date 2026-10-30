// Exclusive replacement of the master EQ. SDK owns input, stock selector,
// project persistence and delivery of complete controls to the audio role.
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace master {
const u32 frames = 64, sample_rate = 44100, neutral = 512;
const u32 max_paths = 8, max_points = 32, max_grays = 256;
struct Point {
    i32 x, y;
}; // Q10 screen coordinates.
struct Path {
    const char *id;
    u32 points;
    bool closed;
};
// Borrowed for frame() only. Methods refuse invalid indices, counts and bounds.
// Hidden paths retain nondegenerate geometry; never send a collapsed polygon.
struct Scene {
    void *context;
    bool (*path)(void *, u32 index, const Point *, u32 count, bool visible);
    bool (*gray)(void *, u32 shade);
};
struct Input {
    u32 sweep;   // 0..1023; exact neutral is 512.
    i32 elapsed; // Q10 seconds.
    u32 project_serial, reset_serial, turn_serial;
};
struct Panel {
    const Path *paths;
    u32 path_count;
    const char *gray_target;
    const char *gray_prefix;
    u32 gray_count;
    void (*frame)(const Scene &, const Input &);
};
struct Filter {
    const char *label; // 1..15 characters. Stock selector's second row; first remains original eq.
    const Panel *panel;
    // Synchronous AUDIO-role callbacks; no graphics, I/O, allocation, blocking,
    // controller actions or registration. State belongs exclusively to audio.
    void (*reset)();
    void (*prepare)(u32 sweep);
    void (*process)(i32 *left, i32 *right); // 64 stereo Q31 samples, in place.
};
// Export opxy::extensions::<id>::filter and declare master_filter in
// extension.json. The SDK supports one selected owner; descriptor immutable.
}} // namespace opxy::master
