// Exact 1.2.1 call replacements. Each forwards the original C ABI unchanged.
// Recovery calls the pattern editor, never the live/audio note dispatcher.
#include "../extension_runtime.hpp"
#include "note_container.hpp"
#include "stock.hpp"

extern "C" {
void sdk_extensions_controller_update(opxy::u32, opxy::u32) __asm__("sdk_extensions_controller_update");
void sdk_extensions_key(const opxy::u32*) __asm__("sdk_extensions_key");
opxy::u32 sdk_extensions_key_queue(const opxy::u32*) __asm__("sdk_extensions_key_queue");
void sdk_extensions_recorder_tick(opxy::u32, opxy::u32) __asm__("sdk_extensions_recorder_tick");
void sdk_extensions_stop(opxy::u32) __asm__("sdk_extensions_stop");
void sdk_extensions_note_accepted(opxy::u32, opxy::u32) __asm__("sdk_extensions_note_accepted");
void sdk_extensions_live_note_on(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32,
    opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, const void*)
    __asm__("sdk_extensions_live_note_on");
void sdk_extensions_live_note_off(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32,
    const void*) __asm__("sdk_extensions_live_note_off");
}

namespace opxy { namespace platform {
namespace {
const u32 kController = 0x860c6adc;
const u32 kRecorder = 0x860c072c;
const u32 kProject = 0x840a0524;
const u32 kRecordHeld = kPlaying + 3;  // byte: Record held
const u32 kInputEnabled = 0x08023c48;
using Wide = __UINT64_TYPE__;
// Only the normal live-input call establishes this scope. Sequencer playback
// calls the broad dispatcher directly and cannot become a history event.
struct LiveInput { u32 track, pitch, velocity, flags; bool active; };
LiveInput live_input;

using PatternNote = notes::Note;  // Stock copies the first eleven payload bytes.

// No other key and no Shift held. Press dispatch precedes insertion into
// stock's held-key list, so a chord keeps stock behavior.
bool unmodified(u32 key) {
    if (shift_held()) return false;
    for (u32 other = 0; other < 56; ++other)
        if (other != key && stock<u32(u32)>(0x8d5b7d7c)(other)) return false;
    return true;
}
// The one other key held, without Shift (a hold-menu such as the bar menu).
u32 held_with(u32 key) {
    if (shift_held()) return extensions::keys::none;
    u32 found = extensions::keys::none;
    for (u32 other = 0; other < 56; ++other) {
        if (other == key || !stock<u32(u32)>(0x8d5b7d7c)(other)) continue;
        if (found != extensions::keys::none) return extensions::keys::none;
        found = other;
    }
    return found;
}
}

namespace {
const u32 kBarMenuLocked = 0x0801ebec + 0x1d;  // byte: global controller's bar menu lock
const u32 kBar = extensions::keys::bar;
const u32 kBarMenu = extensions::screens::bar_menu;
// A BAR press held back on the locked bar menu: the event, whether Shift was
// held, and whether another key was pressed before BAR's release.
struct HeldBar { bool active, shift, other; u32 event[4]; };
HeldBar held_bar;
void record_held(const u32* event) { stock<void(const u32*)>(0x8d5b8100)(event); }
// The selected track's next bar shown, wrapping, as a BAR tap on the unlocked
// menu shows it (stock's bar menu key handler, 8c0845e0-8c08464c, which skips
// it while locked): 8d585fb4 the shown bar, 8d585fe4 the bar count, 8d587458
// shows one; playing, it clears the byte at kPlaying + 0x17 as stock does, and
// 8d54addc refreshes the view (8d54b438's object, the bar).
void show_next_bar() {
    const u32 track = byte(kSelectedTrack);
    const u32 bars = stock<u32(u32)>(0x8d585fe4)(track);
    if (!bars) return;
    const u32 shown = stock<u32(u32)>(0x8d585fb4)(track) + 1;
    const u32 next = shown < bars ? shown : 0;
    stock<void(u32, u32)>(0x8d587458)(track, next);
    if (byte(kPlaying) || byte(kPlaying + 1)) *at<volatile unsigned char>(kPlaying + 0x17) = 0;
    stock<void(Address, u32)>(0x8d54addc)(stock<Address()>(0x8d54b438)(), next);
}

// Stock's key dispatch (8c0b5d58), with a claim's keys on its screen given to
// its holder, with or without Shift, while its held key (if any) is held
// (bar_menu: Play with BAR held in the bar menu). Stock dispatches a press to
// the screen's controller and its global handler (8c0b5b10; with Shift
// 8c0b5b80), then records the key as held (8d5b8100); a taken press is only
// recorded as held, so the holder gets the key and stock's action never runs.
// Releases, other screens and every other key keep stock's dispatch.
//
// Shift + BAR locks the bar menu open (the lock byte), so Play there without
// BAR stays stock's (playback starts). While a claim needs BAR held there, a
// BAR press on the locked menu (stock: closes it, 8c09291c; with Shift, locks
// it again) is held back until BAR's release, so BAR + Play keeps the menu
// open. Released with no other key pressed meanwhile, BAR alone shows the next
// bar, as a BAR tap does on the unlocked menu, and Shift + BAR gets stock's
// BAR press, which unlocks and closes the menu. Released with the lock set,
// stock's BAR release leaves the menu alone (8c093030).
bool dispatch_key(const u32* event) {
    const bool enabled = byte(kInputEnabled) != 0;
    if (event[1] && held_bar.active && event[0] != kBar) held_bar.other = true;
    if (event[1] && enabled && event[0] == kBar && byte(kBarMenuLocked) &&
        word(kScreen) == kBarMenu && extensions_needs_held(kBar, kBarMenu)) {
        held_bar = {true, shift_held(), false, {event[0], event[1], event[2], event[3]}};
        record_held(event);
        return false;
    }
    if (!event[1] && event[0] == kBar && held_bar.active) {
        held_bar.active = false;
        if (!held_bar.other && enabled && byte(kBarMenuLocked) && word(kScreen) == kBarMenu) {
            if (held_bar.shift) stock<void(const u32*)>(0x8c0b5b10)(held_bar.event);
            else show_next_bar();
        }
    }
    if (event[1] && enabled && extensions_takes(event[0], word(kScreen))) {
        record_held(event);
        return true;
    }
    stock<void(const u32*)>(0x8c0b5d58)(event);
    return false;
}
}

TrackPattern extension_pattern(u32 track) {
    TrackPattern p = {};
    if (!word(kProject) || track >= 8) return p;
    p.pattern = stock<u32(u32)>(0x8d586014)(track);
    if (!p.pattern) return p;
    p.length = 480u * stock<u32(u32)>(0x8d586800)(track);
    p.scale = stock<u32(u32)>(0x8d586b50)(track);
    p.editable = p.length != 0 && p.scale < 16;
    return p;
}

RecordingContext extension_context(bool& playing) {
    RecordingContext c = {};
    c.project = word(kProject);
    c.track = byte(kSelectedTrack);
    playing = byte(kPlaying) || byte(kPlaying + 1);
    const TrackPattern p = extension_pattern(c.track);
    if (!p.pattern) return c;
    c.pattern = p.pattern;
    c.length = p.length;
    c.scale = p.scale;
    c.quantization = stock<u32(u32)>(0x8d586c8c)(c.track);
    c.editable = p.editable;
    // The other recorder modes are step/sample workflows, not live notes.
    c.recordable = c.editable && !byte(kRecorder) && !byte(kRecorder + 1);
    return c;
}

i32 extension_position(u32 track) {
    i32 position = stock<i32(u32, u32)>(0x8d592a94)(kController, track);
    const i32 length = 480 * stock<i32(u32)>(0x8d586800)(track);
    // Match the ordinary recorder's pickup around pattern zero.
    if (position > length - 240) position -= length;
    return position;
}
u32 extension_scaled_delta(u32 track, u32 delta) {
    return stock<u32(u32, u32)>(0x8d5950e0)(track, delta << 8);
}
bool extension_record_held() { return byte(kRecordHeld) != 0; }
bool extension_key_held(u32 key) { return stock<u32(u32)>(0x8d5b7d7c)(key) != 0; }

i32 extension_collision_radius(u32 track) {
    const u32 scale = stock<u32(u32)>(0x8d586b50)(track);
    if (scale >= 16) return -1;
    const u32 numerator = word(0x840a36e0 + scale * 8);
    const u32 denominator = word(0x840a36e4 + scale * 8);
    if (!denominator) return -1;
    return 180u * numerator / denominator;
}

u32 extension_write(u32 track, const RecordedNote* notes, u32 count, bool replace) {
    if (!notes || !count || count > 120 || track >= 8) return 0;
    auto* container = stock<unsigned char*(u32, u32)>(0x8d5875c8)(track, 0);
    if (!container || (!replace && *reinterpret_cast<const i16*>(container + 0x786) >= 120)) return 0;
    auto& list = *reinterpret_cast<notes::Container*>(container);
    if (replace && !notes::clearable(list)) return 0;
    const i32 radius = extension_collision_radius(track);
    if (radius < 0) return 0;
    // Start one stock undo transaction before changing any notes. This reset
    // only clears the previous note-undo identity; it does not move transport.
    stock<void(u32)>(0x8d591930)(kController);
    stock<void(u32, u32)>(0x8d593c18)(kController, track);
    // Replacing clears the track's notes first, inside the same undo transaction.
    if (replace && !notes::clear(list)) return 0;
    u32 written = 0;
    for (u32 i = 0; i < count; ++i) {
        const RecordedNote& note = notes[i];
        const PatternNote payload = {note.position, note.length, note.pitch, note.velocity, 0, 0};
        const Wide result = stock<Wide(void*, const PatternNote*, i32)>(0x8d58f24c)(container, &payload, radius);
        if (static_cast<i16>(result >> 32) < 0) break;
        ++written;
    }
    if (written || replace) stock<void(u32)>(0x8d587668)(track);  // A cleared pattern is a change too.
    return written;
}
// Stock's popup (8d549168: text, 1, 0, seconds as a float); stock's own last
// 2 or 5 seconds, the SDK's half a second unless asked.
void extension_notify(const char* text) {
    stock<void(const char*, u32, u32, u32)>(0x8d549168)(text, 1, 0, 0x3f000000u);
}
// Seconds as an IEEE single, without float arithmetic (the module links none):
// the time in Q18 seconds, normalized. 100-10000 ms fits in 32 bits.
u32 seconds_bits(u32 milliseconds) {
    const u32 q18 = (milliseconds << 18) / 1000;
    if (!q18) return 0;
    u32 top = 31;
    while (!(q18 & (1u << top))) --top;
    const u32 mantissa = (top > 23 ? q18 >> (top - 23) : q18 << (23 - top)) & 0x7fffffu;
    return ((top - 18 + 127) << 23) | mantissa;
}
void extension_notify_for(const char* text, u32 milliseconds) {
    stock<void(const char*, u32, u32, u32)>(0x8d549168)(text, 1, 0, seconds_bits(milliseconds));
}
}}

using namespace opxy;
using namespace opxy::platform;

void sdk_extensions_controller_update(u32 controller, u32 elapsed) {
    extensions_sync();
    stock<void(u32, u32)>(0x8d5986b8)(controller, elapsed);
    extensions_sync();
}
void sdk_extensions_recorder_tick(u32 state, u32 delta) {
    extensions_recording_tick(delta);
    stock<void(u32, u32)>(0x8d590b0a)(state, delta);
}
void sdk_extensions_stop(u32 controller) {
    stock<void(u32)>(0x8d591964)(controller);
    // Observe Stop even when an external Stop/Start pair arrives in one update.
    // Both stock playing flags are already clear at this call site.
    extensions_sync();
}
void sdk_extensions_live_note_on(u32 controller, u32 track, u32 note, u32 a3, u32 a4,
                        u32 velocity, u32 a6, u32 a7, u32 low, u32 high, u32 flags,
                        const void* event) {
    extensions_input_activity();
    const LiveInput previous = live_input;
    live_input = {track, note, velocity, flags, true};
    stock<void(u32,u32,u32,u32,u32,u32,u32,u32,u32,u32,u32,const void*)>(0x8d595738)
        (controller,track,note,a3,a4,velocity,a6,a7,low,high,flags,event);
    live_input = previous;
}
void sdk_extensions_note_accepted(u32 track, u32 note) {
    stock<void(u32,u32)>(0x8d5b7d54)(track, note);
    // This call follows stock's acceptance/capacity checks, immediately before
    // its record-arm gate. Apply its ordinary-recording gates without arming it.
    if (live_input.active && live_input.track == track && live_input.pitch == note &&
        (live_input.flags & 1) && !stock<u32()>(0x8d5b7e34)())
        extensions_recordable_note_on(track, note, live_input.velocity);
}
void sdk_extensions_live_note_off(u32 controller, u32 track, u32 note, u32 a3, u32 flags,
                         const void* event) {
    extensions_selected_note_off(track, note);
    stock<void(u32,u32,u32,u32,u32,const void*)>(0x8d595420)(controller,track,note,a3,flags,event);
}
void sdk_extensions_key(const u32* event) {
    extensions_sync();
    extensions::Control control = {event[0], event[1] != 0, false, unmodified(event[0]), 0, held_with(event[0]),
                                   shift_held(), word(kScreen), false};
    const bool was_held = extension_record_held();
    const bool enabled = byte(kInputEnabled) != 0;
    // The clock writes a full 64-bit microsecond value through its argument.
    stock<void(extensions::Time*)>(0x8d5be3a0)(&control.time);
    control.taken = dispatch_key(event);
    // Stock records Record's held state; other keys are accepted while input is enabled.
    control.accepted = enabled && (control.key != extensions::keys::record ||
        (control.pressed ? extension_record_held() : was_held && !extension_record_held()));
    extensions_queued_control(control);
}
u32 sdk_extensions_key_queue(const u32* event) {
    sdk_extensions_key(event);
    // The authenticated span includes stock's following R0 = queue address.
    // A 12-byte span admits CALL.A across the module's relative-call range.
    return 0x08023c78;
}
