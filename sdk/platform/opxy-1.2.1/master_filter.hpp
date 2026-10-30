#pragma once
#include <opxy/master_filter.hpp>
namespace opxy { namespace platform { namespace master_filter {
const u32 block_frames = 64, ramp_frames = 882, message_id = 128, initial_packet = 512;
struct Controls {
    u32 sweep, type;
    static Controls unpack(u32 p) { return Controls{p & 1023u, (p >> 30) & 1u}; }
    u32 pack() const { return (sweep & 1023u) | ((type & 1u) << 30); }
};
struct Menu {
    i32 values[4];
    u32 type, candidate;
    bool open;
    void reset();
    void show();
    void confirm();
    void cancel();
    void click(u32);
    u32 packet() const;
};
extern Menu menu;
const master::Filter *descriptor(); // Generated, immutable; no registration/sync.
const char *asset();
const char *owner(); // Empty when no owner is selected.
u32 snapshot();
void loaded(u32 project, u32 packet);
u32 restore(u32 project);
void clear_pending();
void frame(u32 sweep, i32 elapsed);
void reset_visual();
void follow_visual(u32);
void reset_project();
}}} // namespace opxy::platform::master_filter
