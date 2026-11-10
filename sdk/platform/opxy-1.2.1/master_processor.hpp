#pragma once
#include <opxy/master_processor.hpp>
namespace opxy { namespace platform { namespace master_processor {
const u32 ramp_frames = 882, message_id = 128, commit_id = 137, max_processors = 16;
struct Parameter { const char* id; const char* label; i32 initial; u32 position, steps; };
struct Display { void (*frame)(i32); void (*reset)(); void (*parameter_reset)(u32); };
struct Registration {
    const char* id;
    const master::Processor* processor;
    const Parameter* parameters;
    u32 parameter_count;
    const Display* display;
};
// Immutable, compact registry of included extensions. Index zero is always EQ.
u32 processor_count();
Registration registration(u32 type);
const Parameter* parameters(u32& count, u32 type);
const Parameter* parameters(u32& count);
const char* owner(u32 type);
const char* owner();
struct State { i32 values[parameter_slots]; u32 type; };
State current();
State defaults(u32 type = 0);
void loaded_state(u32 project, const State& state);
State restore_state(u32 project);
struct Menu {
    i32 values[parameter_slots], bank[max_processors + 1][parameter_slots];
    u8 shown[page_controls];
    u32 type, candidate;
    bool open, initialized;
    void reset(); void show(); void confirm(); void cancel(); void click(u32); u32 signature() const;
};
extern Menu menu;
void clear_pending();
void frame(i32 elapsed); void parameter_reset(u32 slot); void reset_project();
}}}

namespace opxy { namespace platform { namespace master_filter = master_processor; }}
