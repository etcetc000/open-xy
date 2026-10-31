#include "master_filter.hpp"
using namespace opxy;
namespace opxy {
namespace platform {
namespace master_filter {
Menu menu = {};
namespace {
u32 pending_project;
State pending_state;
} // namespace
void clear_pending() { pending_project = 0; }
State current() {
    if (!menu.initialized)
        menu.reset();
    State state = {};
    state.type = menu.type;
    for (u32 i = 0; i < parameter_slots; ++i)
        state.values[i] = menu.values[i];
    return state;
}
State defaults() {
    State state = {};
    u32 count = 0;
    const Parameter *p = parameters(count);
    for (u32 i = 0; i < count; ++i)
        state.values[i] = p[i].initial;
    return state;
}
void loaded_state(u32 project, const State &state) {
    pending_project = project;
    pending_state = state;
}
State restore_state(u32 project) {
    const State state = project && project == pending_project ? pending_state : defaults();
    clear_pending();
    return state;
}
u32 Menu::signature() const {
    u32 hash = type;
    for (u32 i = 0; i < parameter_slots; ++i)
        hash = (hash ^ static_cast<u32>(values[i])) * 16777619u;
    return hash;
}
void Menu::reset() {
    initialized = true;
    const State state = defaults();
    for (u32 i = 0; i < parameter_slots; ++i)
        values[i] = state.values[i];
    for (u32 i = 0; i < page_controls; ++i)
        shown[i] = i;
    type = candidate = 0;
    open = false;
}
void Menu::show() {
    if (!initialized)
        reset();
    candidate = type;
    open = true;
}
void Menu::confirm() {
    if (open)
        type = candidate;
    open = false;
}
void Menu::cancel() { open = false; }
void Menu::click(u32 encoder) {
    if (encoder >= page_controls)
        return;
    u32 count = 0;
    const Parameter *p = parameters(count);
    const u32 slot = shown[encoder];
    if (slot >= count)
        return;
    u32 next = slot;
    for (u32 n = 1; n < count; ++n)
        if (p[(slot + n) % count].position == encoder) {
            next = (slot + n) % count;
            break;
        }
    if (next != slot)
        shown[encoder] = next;
    else
        values[slot] = p[slot].initial;
}
} // namespace master_filter
} // namespace platform
} // namespace opxy
