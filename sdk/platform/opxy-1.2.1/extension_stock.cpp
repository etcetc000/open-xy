// Exact 1.2.1 call replacements. Each forwards the original C ABI unchanged.
// Recovery calls the pattern editor, never the live/audio note dispatcher.
#include "../extension_runtime.hpp"
#include "note_container.hpp"
#include "stock.hpp"
#include "param_edit.hpp"

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
void sdk_extensions_midi_menu(opxy::u32, const void*, opxy::u32) __asm__("sdk_extensions_midi_menu");
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
// Stock's MIDI settings menu (built once at boot, 8c098d48) takes rows of
// {label, setting, display, setter}. A setting (8c089110, 0x18 bytes from stock's
// new, 8d4d2900) registers its key, so stock saves and loads its value; the menu
// calls setter(value) when it loads it and after an edit. 8c098cae shows on/off.
struct MenuRow { const char* label; void* setting; u32 display; void (*setter)(i32); };
const u32 kMenuRows = 4;
u32 menu_entries[kMenuRows], menu_rows;
void* menu_settings[kMenuRows];
template <u32 row> void menu_set(i32 value) {
    if (row < menu_rows) extensions_menu_set(menu_entries[row], value != 0);
}
void (*const menu_setters[kMenuRows])(i32) = {menu_set<0>, menu_set<1>, menu_set<2>, menu_set<3>};

bool unmodified(u32 key) {
    if (shift_held()) return false;
    for (u32 other = 0; other < 56; ++other)
        if (other != key && stock<u32(u32)>(0x8d5b7d7c)(other)) return false;
    return true;
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
// Stock's CC out (8d57a200): channel, cc, value and a time (null: now). It sends
// only while the MIDI "other" output setting is on, and already runs on this
// path for MIDI engine tracks (8d58a896).
void extension_send_cc(u32 channel, u32 cc, u32 value) {
    stock<void(u32, u32, u32, const void*)>(0x8d57a200)(channel, cc, value, nullptr);
}
// Stock's publish (8d5acd9c) sends MIDI itself for the MIDI engine (29) and the
// MIDI aux track (10).
bool extension_stock_sends_midi(u32 track) {
    return stock<u32(u32)>(0x8d58781c)(track) == 29 || track == 10;
}
// Once, on the controller path, when the runtime starts: stock's load (8c088da0)
// and value (8c088caa) of each row's setting, as the menu applies them when built.
// Not at boot, where the menu is built before stock's settings can be read.
void extension_menu_restore() {
    for (u32 row = 0; row < menu_rows; ++row) {
        stock<void(void*)>(0x8c088da0)(menu_settings[row]);
        extensions_menu_set(menu_entries[row], stock<i32(void*)>(0x8c088caa)(menu_settings[row]) != 0);
    }
}
void extension_notify(const char* text) {
    stock<void(const char*, u32, u32, u32)>(0x8d549168)(text, 1, 0, 0x3f000000u);
}
}}

using namespace opxy;
using namespace opxy::platform;

void sdk_extensions_controller_update(u32 controller, u32 elapsed) {
    // Encoder edits reach extensions through the edit hook (param_edit.cpp).
    sdk_parameter_edits = extensions_parameter_edit;
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
    extensions::Control control = {event[0], event[1] != 0, false, unmodified(event[0]), 0};
    const bool was_held = extension_record_held();
    const bool enabled = byte(kInputEnabled) != 0;
    // The clock writes a full 64-bit microsecond value through its argument.
    stock<void(extensions::Time*)>(0x8d5be3a0)(&control.time);
    stock<void(const u32*)>(0x8c0b5d58)(event);
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
// The MIDI menu's build: stock's rows, then one on/off row per extension that
// declares one, each with its own setting (key ext_<id>, 0..1). The absolute call
// replaces stock's menu load and call (8c099076..8c099082), so R0 is not the menu:
// rows (R1) and count (R2) are stock's, and the menu is stock's constant.
void sdk_extensions_midi_menu(u32, const void* rows, u32 count) {
    const u32 menu = 0x0801eef0;
    MenuRow all[16];
    const MenuRow* stock_rows = static_cast<const MenuRow*>(rows);
    u32 total = 0;
    for (; total < count && total < 16 - kMenuRows; ++total) all[total] = stock_rows[total];
    u32 entry_count = 0;
    const ExtensionEntry* list = extension_collection(entry_count);
    for (u32 i = 0; list && i < entry_count && i < 16 && menu_rows < kMenuRows; ++i) {
        if (!list[i].menu_label) continue;
        void* setting = stock<void*(u32)>(0x8d4d2900)(0x18);
        if (!setting) break;
        stock<void(void*, const char*, i32, i32, i32)>(0x8c089110)(setting, list[i].menu_key,
            list[i].menu_default ? 1 : 0, 0, 1);
        menu_entries[menu_rows] = i;
        menu_settings[menu_rows] = setting;
        all[total++] = {list[i].menu_label, setting, 0x8c098cae, menu_setters[menu_rows]};
        ++menu_rows;
    }
    stock<void(u32, const MenuRow*, u32)>(0x8c097f0c)(menu, all, total);
}

