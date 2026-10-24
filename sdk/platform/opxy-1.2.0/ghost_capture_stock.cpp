// Exact 1.2.0 call replacements. Each forwards the original C ABI unchanged.
// Recovery calls the pattern editor, never the live/audio note dispatcher.
#include "../extension_runtime.hpp"
#include "stock.hpp"

extern "C" {
void sdk_capture_loop(opxy::u32, opxy::u32) __asm__("sdk_capture_loop");
void sdk_capture_event(const opxy::u32*) __asm__("sdk_capture_event");
opxy::u32 sdk_capture_queued_event(const opxy::u32*) __asm__("sdk_capture_queued_event");
void sdk_capture_tick(opxy::u32, opxy::u32) __asm__("sdk_capture_tick");
void sdk_capture_stop(opxy::u32) __asm__("sdk_capture_stop");
void sdk_capture_accepted(opxy::u32, opxy::u32) __asm__("sdk_capture_accepted");
void sdk_capture_note_on(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32,
    opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, const void*)
    __asm__("sdk_capture_note_on");
void sdk_capture_note_off(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32,
    const void*) __asm__("sdk_capture_note_off");
}

namespace opxy { namespace platform {
namespace {
const u32 kController = 0x860cf9c8;
const u32 kRecorder = 0x860c9628;
const u32 kProject = 0x8409d2c8;
const u32 kInputEnabled = 0x08024000;
using Wide = __UINT64_TYPE__;
// Only the normal live-input call establishes this scope. Sequencer playback
// calls the broad dispatcher directly and cannot become a history event.
struct LiveInput { u32 track, pitch, velocity, flags; bool active; };
LiveInput live_input;

struct PatternNote {
    i32 position;
    u32 length;
    u8 pitch, velocity, flags, padding;
};
static_assert(sizeof(PatternNote) == 12, "Stock copies the first eleven payload bytes");

bool unmodified_record() {
    if (shift_held()) return false;
    // Press dispatch precedes insertion into stock's held-key list. A Record
    // chord must keep stock behavior and must not become a capture gesture.
    for (u32 key = 0; key < 56; ++key)
        if (key != 50 && stock<u32(u32)>(0x8d5b5ef4)(key)) return false;
    return true;
}
}

RecordingContext extension_context(bool& playing) {
    RecordingContext c = {};
    c.project = word(kProject);
    c.track = byte(kSelectedTrack);
    playing = byte(kController + 0x3e2d) || byte(kController + 0x3e2e);
    // The other recorder modes are step/sample workflows, not live notes.
    if (!c.project || c.track >= 8 || byte(kRecorder) || byte(kRecorder + 1)) return c;
    c.pattern = stock<u32(u32)>(0x8d5846ac)(c.track);
    if (!c.pattern) return c;
    c.length = 480u * stock<u32(u32)>(0x8d584e98)(c.track);
    c.scale = stock<u32(u32)>(0x8d5851e8)(c.track);
    c.quantization = stock<u32(u32)>(0x8d585324)(c.track);
    c.eligible = c.length != 0 && c.scale < 16;
    return c;
}

i32 extension_position(u32 track) {
    i32 position = stock<i32(u32, u32)>(0x8d590eb4)(kController, track);
    const i32 length = 480 * stock<i32(u32)>(0x8d584e98)(track);
    // Match the ordinary recorder's pickup around pattern zero.
    if (position > length - 240) position -= length;
    return position;
}
u32 extension_scaled_delta(u32 track, u32 delta) {
    return stock<u32(u32, u32)>(0x8d593408)(track, delta << 8);
}
bool extension_record_held() { return byte(kController + 0x3e30) != 0; }

i32 extension_collision_radius(u32 track) {
    const u32 scale = stock<u32(u32)>(0x8d5851e8)(track);
    if (scale >= 16) return -1;
    const u32 numerator = word(0x840a040c + scale * 8);
    const u32 denominator = word(0x840a0410 + scale * 8);
    if (!denominator) return -1;
    return 180u * numerator / denominator;
}

u32 extension_write(u32 track, const RecordedNote* notes, u32 count) {
    if (!notes || !count || count > 120 || track >= 8) return 0;
    auto* container = stock<unsigned char*(u32, u32)>(0x8d585c60)(track, 0);
    if (!container || *reinterpret_cast<const i16*>(container + 0x786) >= 120) return 0;
    const i32 radius = extension_collision_radius(track);
    if (radius < 0) return 0;
    // Start one stock undo transaction before changing any notes. This reset
    // only clears the previous note-undo identity; it does not move transport.
    stock<void(u32)>(0x8d58fd38)(kController);
    stock<void(u32, u32)>(0x8d591f40)(kController, track);
    u32 written = 0;
    for (u32 i = 0; i < count; ++i) {
        const RecordedNote& note = notes[i];
        const PatternNote payload = {note.position, note.length, note.pitch, note.velocity, 0, 0};
        const Wide result = stock<Wide(void*, const PatternNote*, i32)>(0x8d58d654)(container, &payload, radius);
        if (static_cast<i16>(result >> 32) < 0) break;
        ++written;
    }
    if (written) stock<void(u32)>(0x8d585d00)(track);
    return written;
}
void extension_notify(const char* text) {
    stock<void(const char*, u32, u32, u32)>(0x8d548580)(text, 1, 0, 0x3f000000u);
}
}}

using namespace opxy;
using namespace opxy::platform;

void sdk_capture_loop(u32 controller, u32 elapsed) {
    extensions_sync();
    stock<void(u32, u32)>(0x8d5969f0)(controller, elapsed);
    extensions_sync();
}
void sdk_capture_tick(u32 state, u32 delta) {
    extensions_tick(delta);
    stock<void(u32, u32)>(0x8d58ef12)(state, delta);
}
void sdk_capture_stop(u32 controller) {
    stock<void(u32)>(0x8d58fd6c)(controller);
    // Observe Stop even when an external Stop/Start pair arrives in one update.
    // Both stock playing flags are already clear at this call site.
    extensions_sync();
}
void sdk_capture_note_on(u32 controller, u32 track, u32 note, u32 a3, u32 a4,
                        u32 velocity, u32 a6, u32 a7, u32 low, u32 high, u32 flags,
                        const void* event) {
    extensions_activity();
    const LiveInput previous = live_input;
    live_input = {track, note, velocity, flags, true};
    stock<void(u32,u32,u32,u32,u32,u32,u32,u32,u32,u32,u32,const void*)>(0x8d593a70)
        (controller,track,note,a3,a4,velocity,a6,a7,low,high,flags,event);
    live_input = previous;
}
void sdk_capture_accepted(u32 track, u32 note) {
    stock<void(u32,u32)>(0x8d5b5ecc)(track, note);
    // This call follows stock's acceptance/capacity checks, immediately before
    // its record-arm gate. Apply its ordinary-recording gates without arming it.
    if (live_input.active && live_input.track == track && live_input.pitch == note &&
        (live_input.flags & 1) && !stock<u32()>(0x8d5b5fac)())
        extensions_note_on(track, note, live_input.velocity);
}
void sdk_capture_note_off(u32 controller, u32 track, u32 note, u32 a3, u32 flags,
                         const void* event) {
    extensions_note_off(track, note);
    stock<void(u32,u32,u32,u32,u32,const void*)>(0x8d593758)(controller,track,note,a3,flags,event);
}
void sdk_capture_event(const u32* event) {
    extensions_sync();
    const u32 key = event[0];
    const bool pressed = event[1] != 0;
    const bool was_held = extension_record_held();
    const bool enabled = byte(kInputEnabled) != 0;
    const bool clean = key == 50 && pressed && enabled && unmodified_record();
    // The clock writes a full 64-bit microsecond value through its argument.
    extensions::Time time = 0;
    stock<void(extensions::Time*)>(0x8d5bc500)(&time);
    stock<void(const u32*)>(0x8c0b68b8)(event);
    const bool accepted = enabled && (pressed ? extension_record_held() : was_held && !extension_record_held());
    extensions_control(key, pressed, accepted, clean, time);
}
u32 sdk_capture_queued_event(const u32* event) {
    sdk_capture_event(event);
    // The authenticated span includes stock's following R0 = queue address.
    // A 12-byte span admits CALL.A across the module's relative-call range.
    return 0x08024030;
}
