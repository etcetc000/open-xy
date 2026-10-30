// Host boundary double for the real GUI platform adapter, plus the public
// frame-to-frame event contract. No oscillator or machine behavior is tested.
#include <cstdio>
#include <cstdlib>
#include <note_activity.cpp>

namespace {
unsigned char gates[16 * 128];
unsigned calls;
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
opxy::u32 clear_gate(opxy::u32 track, opxy::u32 note) {
    ++calls;
    if (track < 16 && note < 128) gates[track * 128 + note] = 0;
    return 0x1234;
}
void events(opxy::draw::NoteTracker& tracker, opxy::draw::Input& input, bool start, bool end, bool released) {
    const auto e = tracker.update(input);
    require(e.hold_started == start && e.hold_ended == end && e.note_released == released, "Wrong note events");
}
}

opxy::platform::Address opxy::platform::host_stock_address(opxy::platform::Address address) {
    if (address >= kNoteMap && address < kNoteMap + sizeof(gates))
        return reinterpret_cast<Address>(&gates[address - kNoteMap]);
    if (address == 0x8d5b7d2c) return reinterpret_cast<Address>(&clear_gate);
    std::abort();
}

int main() {
    using namespace opxy::draw;
    Input input = {}; NoteTracker tracker = {};
    events(tracker, input, false, false, false);
    input.held_notes = 2; events(tracker, input, true, false, false);
    input.held_notes = 1; input.note_off_serial = 1; events(tracker, input, false, false, true);
    events(tracker, input, false, false, false);
    input.held_notes = 0; input.note_off_serial = 2; events(tracker, input, false, true, true);
    // A whole tap between frames reports a release but fabricates no hold edge.
    ++input.note_off_serial; ++input.note_serial; events(tracker, input, false, false, true);
    require(input.note_on_serial == input.note_serial, "Start serial alias changed");
    input.track = 1; input.held_notes = 3; input.note_off_serial = 0xffffffffu;
    events(tracker, input, false, false, false);
    input.note_off_serial = 0; events(tracker, input, false, false, true);
    tracker.reset(); events(tracker, input, false, false, false);
    input.held_notes = notes_unavailable; events(tracker, input, false, false, false);
    input.held_notes = 0; events(tracker, input, false, false, false);

    gates[60] = gates[64] = gates[128 + 60] = 1;
    input.note_serial = 47;
    sdk_display_note_activity(0, &input);
    require(input.held_notes == 2 && !input.note_off_serial && input.note_serial == 47, "Gate snapshot changed other input");
    require(sdk_note_off(0,60) == 0x1234 && calls == 1 && !gates[60], "Stock release not preserved");
    sdk_display_note_activity(0, &input);
    require(input.held_notes == 1 && input.note_off_serial == 1, "Partial release lost");
    sdk_note_off(0,60); sdk_display_note_activity(0, &input);
    require(input.note_off_serial == 1 && calls == 2, "Duplicate release counted or swallowed");
    sdk_display_note_activity(1, &input);
    require(input.held_notes == 1 && !input.note_off_serial, "Tracks contaminated each other");
    releases[0] = 0xffffffffu;
    sdk_note_off(0,64); sdk_display_note_activity(0, &input);
    require(!input.held_notes && !input.note_off_serial, "Release counter did not wrap");
    gates[8*128 + 60] = 1;
    sdk_note_off(8,60); sdk_display_note_activity(8, &input);
    require(!input.held_notes && !input.note_off_serial && !gates[8*128+60] && calls == 4, "Non-instrument routing changed");
    sdk_display_note_activity(0xffffffffu, &input);
    require(!input.held_notes && !input.note_off_serial, "Invalid track read stock memory");
    for (unsigned note = 0; note < 128; ++note) gates[7*128+note] = 1;
    sdk_display_note_activity(7, &input); require(input.held_notes == 128, "Full polyphony truncated");
    return 0;
}
