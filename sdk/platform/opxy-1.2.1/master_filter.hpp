#pragma once
#include <opxy/master_filter.hpp>
namespace opxy {
namespace platform {
namespace master_filter {
const u32 ramp_frames = 882, message_id = 128, commit_id = 137;
struct Parameter {
    const char *id;
    const char *label;
    i32 initial;
    u32 position, steps;
};
const Parameter *parameters(u32 &count);
struct State {
    i32 values[parameter_slots];
    u32 type;
};
State current();
void loaded_state(u32 project, const State &state);
State restore_state(u32 project);

struct Menu {
    i32 values[parameter_slots];
    u8 shown[page_controls];
    u32 type, candidate;
    bool open, initialized;
    void reset();
    void show();
    void confirm();
    void cancel();
    void click(u32);
    u32 signature() const;
};
extern Menu menu;
const master::Filter *descriptor(); // Generated, immutable; no registration/sync.
const char *asset();
const char *owner(); // Empty when no owner is selected.
void clear_pending();
void frame(i32 elapsed);
void parameter_reset(u32 slot);
void reset_project();
} // namespace master_filter
} // namespace platform
} // namespace opxy
