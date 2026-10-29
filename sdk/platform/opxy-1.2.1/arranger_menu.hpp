#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {

// GUI gesture only. The menu's latched visibility belongs to the caller.
class ArrangerMenuGesture {
public:
    bool pending, opened;
    constexpr ArrangerMenuGesture() : pending(false), opened(false), started_(0) {}

    void press(u32 now) {
        if (pending) return;
        pending = true;
        opened = false;
        started_ = now;
    }
    bool poll(u32 now) {
        if (!pending || opened || now - started_ < 450) return false;
        opened = true;
        return true;
    }
    bool release(u32 now) {
        const bool tap = pending && !opened && now - started_ < 450;
        cancel();
        return tap;
    }
    void cancel() { pending = opened = false; }

private:
    u32 started_;
};

}}
