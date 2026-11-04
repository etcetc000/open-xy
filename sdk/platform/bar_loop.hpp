// Bar loops in a track's own time: one bar is 16 steps of 480 pattern ticks,
// and stock keeps track time at 1680 units per pattern tick. A pattern's last
// bar may be shorter. Pure arithmetic, shared by the adapter and host tests.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace bar_loop {
const u32 step_time = 480u * 1680u;
const u32 bar_steps = 16;
const u32 bar_time = bar_steps * step_time;
const u32 max_bars = 4;

// A bar's span of track time, [start, end).
struct Range { u32 start, end; };

inline u32 bars(u32 steps) { return steps ? (steps + bar_steps - 1) / bar_steps : 0; }

// The span of one bar of a pattern with this many steps; false if it has none.
inline bool range(u32 bar, u32 steps, Range& r) {
    if (bar >= bars(steps) || bar >= max_bars) return false;
    const u32 first = bar * bar_steps;
    const u32 last = first + bar_steps < steps ? first + bar_steps : steps;
    r.start = first * step_time;
    r.end = last * step_time;
    return true;
}
inline bool inside(const Range& r, u32 time) { return time >= r.start && time < r.end; }

// A jump to anywhere in the pattern lands at the same place within the bar:
// step 5 of any bar is step 5 of the looped one, play start is its first step.
inline u32 fold(const Range& r, u32 time) {
    return inside(r, time) ? time : r.start + time % (r.end - r.start);
}
// The same for a step index (a step component's go-to target).
inline i32 fold_step(const Range& r, i32 step) {
    const i32 first = static_cast<i32>(r.start / step_time);
    const i32 count = static_cast<i32>((r.end - r.start) / step_time);
    if (step >= first && step < first + count) return step;
    const i32 within = step % count;
    return first + (within < 0 ? within + count : within);
}
// Where a move past the bar's end lands after wrapping to its start.
inline u32 landing(const Range& r, u32 time) {
    return r.start + (time - r.end) % (r.end - r.start);
}

// The bar a track with `bars` bars of `ticks` song time plays at song time
// `at` since its pattern started together with the others: how bar N of one
// track lines up with the rest.
inline u32 aligned(u32 at, u32 bars, u32 ticks) {
    if (!bars || !ticks) return 0;
    return (at % (bars * ticks)) / ticks;
}
}}}
