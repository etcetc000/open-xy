#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace platform {

// Routes queued controls at their existing dispatch site. A chord's modifier
// waits until its next key or release; ordinary taps and other combinations
// replay the original events, in order, through stock.
struct ControlRouter {
    struct Host {
        bool (*modifier)(u32);
        bool (*chord)(u32, u32);
        void (*forward)(const u32*);
        void (*claimed)(const u32*, u32, bool);
    };
    u32 deferred[4];
    u32 swallowed[56];  // modifier + 1, so zero-initialized means none
    bool pending, used;

    void route(const u32* event, bool enabled, bool clean, bool normal, const Host& host) {
        const u32 key = event[0];
        const bool pressed = event[1] != 0;
        if (key >= 56) { host.forward(event); return; }
        if (swallowed[key]) {
            if (!pressed) {
                const u32 modifier = swallowed[key] - 1;
                swallowed[key] = 0;
                host.claimed(event, modifier, enabled);
            }
            return;  // key repeats never run an action twice
        }
        if (pending && key == deferred[0]) {
            if (pressed) return;
            pending = false;
            if (!used && enabled) {
                host.forward(deferred);
                host.forward(event);
            }
            used = false;
            return;
        }
        if (pending && pressed) {
            if (!enabled) used = true;
            if (enabled && clean && normal && host.chord(deferred[0], key)) {
                used = true;
                swallowed[key] = deferred[0] + 1;
                host.claimed(event, deferred[0], true);
                return;
            }
            if (!used) {
                pending = false;
                if (enabled) host.forward(deferred);
            }
        }
        if (!pending && pressed && enabled && clean && normal && host.modifier(key)) {
            for (u32 i = 0; i < 4; ++i) deferred[i] = event[i];
            pending = true;
            used = false;
            return;
        }
        host.forward(event);
    }
};
}}
