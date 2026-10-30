#include "master_filter.hpp"
using namespace opxy;
namespace opxy { namespace platform { namespace master_filter {
Menu menu = {{0x40000000, 0, 0, 0}, 0, 0, false};
u32 snapshot() {
    return descriptor() ? menu.packet() : initial_packet;
}
namespace {
u32 pending_project, pending_packet = initial_packet;
}
void clear_pending() {
    pending_project = 0;
    pending_packet = initial_packet;
}
void loaded(u32 project, u32 packet) {
    pending_project = project;
    pending_packet = packet;
}
u32 restore(u32 project) {
    const u32 packet = project && project == pending_project ? pending_packet : initial_packet;
    clear_pending();
    return packet;
}
void Menu::reset() {
    values[0] = 0x40000000;
    values[1] = values[2] = values[3] = 0;
    type = candidate = 0;
    open = false;
}
void Menu::show() {
    candidate = type;
    open = true;
}
void Menu::confirm() {
    if (open)
        type = candidate;
    open = false;
}
void Menu::cancel() {
    open = false;
}
void Menu::click(u32 encoder) {
    if (!encoder)
        values[0] = 0x40000000;
}
u32 Menu::packet() const {
    return Controls{static_cast<u32>(values[0]) >> 21, type}.pack();
}
}}} // namespace opxy::platform::master_filter
