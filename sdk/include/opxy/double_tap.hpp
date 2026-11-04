#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace extensions {

// Recognizes a double-tap on one key from its queued presses and releases,
// without delaying or suppressing stock's own actions. It fires only after two
// short, unmodified, completed taps; holds, repeats and chords cancel it.
class DoubleTap {
public:
    using Time = __UINT64_TYPE__;
    constexpr DoubleTap() : pressed_at_(0), released_at_(0), down_(false), clean_(false),
                  pending_(false), second_(false) {}

    // True starts a fresh pair: the caller can anchor its history snapshot now,
    // rather than moving the capture window while the user completes both taps.
    bool press(Time microseconds, bool unmodified) {
        if (down_) { cancel(); return false; } // A repeat is not another tap.
        second_ = pending_ && microseconds - released_at_ <= interval;
        pending_ = false;
        pressed_at_ = microseconds;
        down_ = true;
        clean_ = unmodified;
        return clean_ && !second_;
    }

    bool release(Time microseconds) {
        if (!down_) { cancel(); return false; }
        down_ = false;
        if (!clean_ || microseconds - pressed_at_ > hold_limit) {
            cancel();
            return false;
        }
        const bool capture = second_;
        second_ = false;
        pending_ = !capture;
        released_at_ = microseconds;
        return capture;
    }

    // Called for another control, live playing while holding Record, changes
    // of recording context, or leaving the supported instrument screen.
    // Keep down_ so a cancelled held Record cannot be mistaken for a new tap.
    void cancel() { clean_ = pending_ = second_ = false; }

private:
    static const u32 interval = 350000;
    static const u32 hold_limit = 250000;
    Time pressed_at_, released_at_;
    bool down_, clean_, pending_, second_;
};

}}
