#pragma once
#include <opxy/arranger.hpp>

namespace opxy { namespace platform {
// Stock owns instrument selection outside Linear. Pinned edits/transactions
// keep their captured lane; AUX tracks have no corresponding timeline lane.
inline bool arranger_sync_lane(u32 selected, bool pinned, u32& lane) {
    if (pinned || selected >= 8 || selected == lane) return false;
    lane = selected;
    return true;
}
inline u32 arranger_shift_time(u32 value, i32 delta, u32 step) {
    const u32 magnitude = delta < 0 ? static_cast<u32>(-(delta + 1)) + 1 : static_cast<u32>(delta);
    if (step && magnitude > 0xffffffffu / step) return delta < 0 ? 0 : 0xffffffffu;
    const u32 amount = magnitude * step;
    return delta < 0 ? (value > amount ? value - amount : 0) :
        (amount > 0xffffffffu - value ? 0xffffffffu : value + amount);
}
inline u32 arranger_zoom(u32 ticks, i32 delta, u32 minimum = 7680) {
    // Eight equal-ratio steps per doubling; piano can zoom below one bar.
    static const u32 widths[] = {
        480, 523, 570, 622, 678, 740, 807, 880,
        960, 1046, 1141, 1244, 1357, 1480, 1614, 1760,
        1920, 2093, 2283, 2489, 2715, 2961, 3229, 3521,
        3840, 4187, 4566, 4979, 5430, 5922, 6458, 7042,
        7680, 8375, 9133, 9960, 10861, 11844, 12916, 14085,
        15360, 16750, 18266, 19919, 21722, 23688, 25832, 28170,
        30720, 33500, 36532, 39839, 43445, 47377, 51665, 56341,
        61440, 67001, 73065, 79678, 86889, 94753, 103329, 112681,
        122880, 134002, 146130, 159356, 173779, 189507, 206659, 225363,
        245760, 268003, 292260, 318711, 347557, 379014, 413317, 450726, 491520
    };
    i32 first = 0;
    while (first < 80 && widths[first] < minimum) ++first;
    i32 index = first;
    while (index < 80 && widths[index] < ticks) ++index;
    if (delta > 80) delta = 80;
    if (delta < -80) delta = -80;
    index -= delta;
    return widths[index < first ? first : index > 80 ? 80 : index];
}
inline u32 arranger_zoom_anchor(u32 relative, u32 before, u32 after) {
    if (before == after) return relative;
    // Q12 keeps the cursor within a fraction of a pixel without 64-bit helpers.
    return ((relative * 4096u / before) * after) / 4096u;
}
inline u32 arranger_cursor_time(u32 value, i32 delta, u32 step) {
    if (step <= 1 || !delta) return arranger_shift_time(value, delta, step);
    const u32 base = value / step * step;
    if (delta < 0 && value % step) ++delta;
    return arranger_shift_time(base, delta, step);
}
inline u32 arranger_boundary(const arranger::Model& model, u32 track, u32 cursor, i32 delta) {
    const bool forward = delta > 0;
    u32 count = delta < 0 ? static_cast<u32>(-(delta + 1)) + 1 : static_cast<u32>(delta);
    if (count > 128) count = 128;
    while (count--) {
        u32 next = cursor;
        bool found = false;
        for (u32 slot = 0; slot < arranger::Model::capacity; ++slot) {
            const arranger::Clip* clip = model.clip(slot);
            if (!clip || clip->track != track) continue;
            const u32 boundaries[2] = {clip->start, clip->start + clip->length};
            for (u32 i = 0; i < 2; ++i) {
                const u32 point = boundaries[i];
                if ((forward ? point > cursor : point < cursor) &&
                    (!found || (forward ? point < next : point > next))) {
                    next = point; found = true;
                }
            }
        }
        if (!found) break;
        cursor = next;
    }
    return cursor;
}
inline u32 arranger_bar_spacing(u32 window) {
    u32 spacing = 7680;
    while (window / spacing > 16) spacing *= 2;
    return spacing;
}
inline u32 arranger_resize_length(u32 length, i32 delta, u32 step) {
    const u32 adjusted = arranger_shift_time(length, delta, step);
    const u32 minimum = step > 1 ? step : 1;
    // Switching back to a coarse grid must not enlarge an already shorter clip.
    const u32 floor = length && length < minimum ? length : minimum;
    return adjusted < floor ? floor : adjusted;
}
struct ArrangerStopSequence {
    bool stopped;
    u32 target(u32 cursor) { const u32 result = stopped ? 0 : cursor; stopped = true; return result; }
    void interrupt() { stopped = false; }
};
struct ArrangerClipboard {
    arranger::Clip value;
    bool valid;
    bool cut(arranger::Model& model, u32 slot) {
        const arranger::Clip* clip = model.clip(slot);
        if (!clip) return false;
        const arranger::Clip copy = *clip;
        if (!model.remove(slot)) return false;
        value = copy; valid = true;
        return true;
    }
    i32 paste(arranger::Model& model, u32 track, u32 start) const {
        if (!valid || value.track != track) return -1;
        arranger::Clip copy = value;
        copy.start = start;
        return model.insert(copy);
    }
};
struct ArrangerStopGesture {
    bool pending, used;
    bool begin() {
        if (pending) return false;
        pending = true; used = false; return true;
    }
    void modify() { if (pending) used = true; }
    void cancel() { modify(); }
    bool release() {
        if (!pending) return false;
        pending = false;
        return !used;
    }
};
// Destructive timeline replacement always needs a second distinct E4 press.
struct ArrangerBounceChoice {
    bool open, replace, confirmed;
    u32 engine;
    void begin() { open = true; replace = confirmed = false; engine = 2; }
    void cancel() { open = confirmed = false; }
    void turn(u32 encoder, i32 delta) {
        if (!open || !delta) return;
        if (encoder % 4 == 0) { engine = delta > 0 ? 3 : 2; confirmed = false; }
        if (encoder % 4 == 1) { replace = delta > 0; confirmed = false; }
    }
    bool accept() {
        if (!open) return false;
        if (replace && !confirmed) { confirmed = true; return false; }
        return true;
    }
};
}}
