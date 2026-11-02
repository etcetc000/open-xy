// Master-side bounce coordinator. Recorder and writer acknowledgements arrive
// over the stock inter-CPU message transport; their globals are not shared RAM.
#include "arranger_bounce.hpp"
#include "arranger_bounce_wire.hpp"
#include "arranger_state.hpp"
#include "arranger_project.hpp"
#include "arranger_transport.hpp"
#include "stock.hpp"
#include "barrier.hpp"

extern "C" bool sdk_arranger_bounce_destination_ready(opxy::u32, opxy::u32, bool)
    __asm__("sdk_arranger_bounce_destination_ready");
extern "C" bool sdk_arranger_bounce_destination_begin(opxy::u32, opxy::u32, bool, const char*, opxy::u32)
    __asm__("sdk_arranger_bounce_destination_begin");
extern "C" opxy::u32 sdk_arranger_bounce_destination_status() __asm__("sdk_arranger_bounce_destination_status");
extern "C" bool sdk_arranger_bounce_destination_busy() __asm__("sdk_arranger_bounce_destination_busy");
extern "C" void sdk_arranger_bounce_destination_cancel() __asm__("sdk_arranger_bounce_destination_cancel");
extern "C" void sdk_arranger_bounce_send(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_arranger_bounce_send");

namespace {
using namespace opxy;
using namespace opxy::platform;
namespace wire = opxy::platform::bounce_wire;
struct Request { u32 slot, engine, track, start, end, frames, project, generation; bool replace; } request;
char saved_path[97];
volatile u32 active, cancelled, dispatch_started, dispatch_done, ready, ready_frames, captured_frames;
volatile u32 stop_ack, stop_intent, transport_started, trim_frames;
volatile u32 finish_requested, finish_ack, released;
volatile u32 writer_done, writer_ok, writer_frames, writer_path_hash, save_complete, saved_frames;
u32 state, editor, previous_key, previous_native_state, previous_native_frames, started_ms, cookie, finish_result;
volatile bool prepare_sent, stop_sent, release_sent, saving;
volatile u32 diagnostic_polls, diagnostic_dispatches, diagnostic_captures;
u32 diagnostic_timeout;
volatile u32 failure_reason;
void fail_reason(u32 value) { if (!failure_reason) failure_reason = value; }
void count(volatile u32& value) { if (value < 65535u) ++value; }
u32 diagnostic_flags() {
    return (prepare_sent ? 1u : 0u) | (ready ? 2u : 0u) |
        (dispatch_started ? 4u : 0u) | (diagnostic_captures ? 8u : 0u) |
        (transport_started ? 16u : 0u) | (stop_sent ? 32u : 0u) |
        (stop_ack ? 64u : 0u) | (finish_requested ? 128u : 0u) |
        (finish_ack ? 256u : 0u);
}
u32 now_ms() { return stock<u32(u32, u32)>(0x8d5be290)(0, 0); }
u32 duration_frames(u32 ticks, u32 bpm) {
    if (bpm < 200 || bpm > 3000) return 0;
    const u32 denominator = bpm * 4, whole = ticks / denominator;
    if (whole > 16) return 0;
    const u32 frames = whole * 55125 + ((ticks % denominator) * 55125 + denominator - 1) / denominator;
    return frames > 6000 && frames <= 882000 - 4096 ? frames : 0;
}
bool bound() {
    return request.project == word(0x840a0524) && !word(0x840a0528) &&
        request.generation == stock<u32()>(0x8d584224)();
}
void store_word(u32 address, u32 value) { *at<volatile u32>(address) = value; }
bool copy_path(char (&path)[97]) {
    const u32 size = word(editor + 0x28), capacity = word(editor + 0x2c);
    if (!size || size > 96) return false;
    const char* source = at<const char>(capacity >= 16 ? word(editor + 0x18) : editor + 0x18);
    for (u32 i = 0; i < size; ++i) { if (!source[i]) return false; path[i] = source[i]; }
    path[size] = 0;
    return true;
}
u32 path_hash(const char* path) {
    u32 hash = 2166136261u;
    for (u32 i = 0; path[i]; ++i) hash = (hash ^ static_cast<u8>(path[i])) * 16777619u;
    return hash;
}
void finish(u32 result) {
    if (result == 6) fail_reason(1);
    finish_result = result;
    if (prepare_sent) {
        if (!release_sent) {
            release_sent = true; barrier();
            sdk_arranger_bounce_send(wire::Release, cookie, 0, 0, 0);
        }
        if (!released) return;
    }
    if (dispatch_started && !finish_ack) { barrier(); finish_requested = 1; return; }
    // Native save 8c0a4bcc opens this indicator (8d54a28c). We suppress
    // native completion 8c0a44f0 to avoid assigning its sample to the source,
    // so balance its indicator separately, only after all owned work drains.
    if (saving) stock<void()>(0x8d54a26c)();
    store_word(0x84039384, previous_native_state);
    store_word(0x84039394, previous_native_frames);
    if (editor) { store_word(editor + 0x38, previous_key); stock<void(u32, u32)>(0x8c0a46b4)(editor, 0); }
    barrier(); active = 0; state = result;
}
}

u32 sdk_arranger_bounce_request(u32 slot, u32 engine, bool replace) {
    if (active || byte(0x860c6adc + 0x305f) || byte(0x860c6adc + 0x3060)) return 1;
    if (!sdk_arranger_project_edit_ready() || !sdk_arranger_project_linear()) return 4;
    const opxy::arranger::Clip* clip = sdk_arranger_model().clip(slot);
    if (clip && clip->muted) return 5; // Never render a silent instance or change its mute state.
    if (!clip || clip->track >= 7 || (engine != 2 && engine != 3) ||
        !sdk_arranger_bounce_destination_ready(slot, engine, replace)) return 2;
    const u32 duration = duration_frames(clip->length, project_bpm_x10());
    if (!duration) return 3;
    editor = stock<u32()>(0x8c0a424c)();
    if (!editor || word(editor + 0x34)) return 1;
    request = Request{slot, engine, clip->track, clip->start, clip->start + clip->length,
        duration, word(0x840a0524), stock<u32()>(0x8d584224)(), replace};
    if (!++cookie) ++cookie;
    diagnostic_polls = diagnostic_dispatches = diagnostic_captures = diagnostic_timeout = 0;
    previous_key = word(editor + 0x38);
    previous_native_state = word(0x84039384); previous_native_frames = word(0x84039394);
    cancelled = dispatch_started = dispatch_done = ready = ready_frames = captured_frames = 0;
    stop_ack = stop_intent = transport_started = trim_frames = 0;
    finish_requested = finish_ack = released = 0;
    writer_done = writer_ok = writer_frames = writer_path_hash = save_complete = saved_frames = 0;
    prepare_sent = stop_sent = release_sent = saving = false;
    failure_reason = 0; finish_result = 0; state = 1; started_ms = now_ms();
    store_word(editor + 0x38, 60);
    stock<void(u32, u32)>(0x8c0a46b4)(editor, 1);
    barrier(); active = 1;
    return 0;
}

void sdk_arranger_bounce_receive(const wire::Message* message) {
    if (!message || !active || message->cookie != cookie) return;
    switch (message->kind) {
    case wire::Ready:
        if (!prepare_sent || ready || stop_ack) return;
        if (message->args[0] < 64u || message->args[0] > 882000u || message->args[0] < captured_frames) { cancelled = 2; return; }
        ready_frames = message->args[0];
        captured_frames = message->args[0]; diagnostic_captures = message->args[1];
        barrier(); ready = 1; break;
    case wire::Progress:
        if (!prepare_sent || stop_ack) return;
        if (message->args[0] > 882000u || message->args[0] < captured_frames) { cancelled = 2; return; }
        captured_frames = message->args[0]; diagnostic_captures = message->args[1]; break;
    case wire::Stopped:
        if (!stop_sent || stop_ack) return;
        if (message->args[0] > 882000u || message->args[0] < captured_frames) cancelled = 2;
        captured_frames = message->args[0]; diagnostic_captures = message->args[1];
        barrier(); stop_ack = 1; break;
    case wire::WriteResult:
        if (!saving || writer_done) return;
        writer_ok = message->args[0]; writer_frames = message->args[1]; writer_path_hash = message->args[2];
        barrier(); writer_done = 1; break;
    case wire::Released:
        if (!prepare_sent) return;
        barrier(); released = 1; break;
    case wire::Error:
        if (!prepare_sent) return;
        fail_reason(saving ? 3 : 1); barrier(); cancelled = 2; break;
    default: break;
    }
}

void sdk_arranger_bounce_diagnostic(ArrangerBounceDiagnostic* out) {
    if (!out) return;
    barrier(); out->polls = diagnostic_polls; out->dispatches = diagnostic_dispatches;
    out->captures = diagnostic_captures; out->flags = diagnostic_flags(); out->timeout_flags = diagnostic_timeout;
}
bool sdk_arranger_bounce_busy() { return active != 0; }
void sdk_arranger_bounce_cancel() { if (active) { cancelled = 1; barrier(); } }
u32 sdk_arranger_bounce_status() { return state; }
u32 sdk_arranger_bounce_failure() { return failure_reason; }
u32 sdk_arranger_bounce_progress() {
    barrier(); const u32 total = captured_frames;
    const u32 frames = total > trim_frames ? total - trim_frames : 0;
    return request.frames ? (frames >= request.frames ? 1000 : frames * 1000u / request.frames) : 0;
}

void sdk_arranger_bounce_dispatch(u32) {
    if (!active) return;
    count(diagnostic_dispatches); barrier();
    if (finish_requested) {
        if (!finish_ack) { if (dispatch_started) sdk_arranger_bounce_end(); barrier(); finish_ack = 1; }
        return;
    }
    if (dispatch_done) return;
    if (cancelled || !bound()) {
        if (dispatch_started) sdk_arranger_bounce_stop();
        barrier(); dispatch_done = 1; return;
    }
    if (!dispatch_started) {
        if (!sdk_arranger_bounce_begin(request.track, request.start, request.end)) {
            cancelled = 2; barrier(); dispatch_done = 1; return;
        }
        barrier(); dispatch_started = 1;
    }
    if (ready && !transport_started) {
        // Ready is a lower bound on stopped preroll: bridge latency may leave
        // additional leading silence. Do not claim sample-exact alignment.
        trim_frames = ready_frames;
        if (!sdk_arranger_bounce_start()) { cancelled = 2; return; }
        barrier(); transport_started = 1;
    }
    if (transport_started && !sdk_arranger_bounce_transport_running()) {
        sdk_arranger_bounce_stop();
        stop_intent = 1; barrier(); dispatch_done = 1;
    }
}

void sdk_arranger_bounce_poll() {
    if (!active) return;
    count(diagnostic_polls); barrier();
    if (finish_requested) { if (finish_ack) finish(finish_result); return; }
    if (release_sent) { finish(finish_result); return; }
    if (!bound()) cancelled = 1;
    if (state == 1 && !transport_started && now_ms() - started_ms > 3000u) {
        if (!diagnostic_timeout) diagnostic_timeout = diagnostic_flags() | 0x80000000u;
        fail_reason(1); cancelled = 2;
    }
    if (state == 7) {
        if (cancelled) sdk_arranger_bounce_destination_cancel();
        const u32 result = sdk_arranger_bounce_destination_status();
        if (!sdk_arranger_bounce_destination_busy()) {
            if (!cancelled && result != 1) fail_reason(5);
            finish(cancelled ? 5 : result == 1 ? 4 : 6);
        }
        return;
    }
    if (!prepare_sent && dispatch_started && !cancelled) {
        prepare_sent = true; barrier();
        sdk_arranger_bounce_send(wire::Prepare, cookie, request.frames, 0, 0);
    }
    if (transport_started && state == 1) state = 2;
    if (!prepare_sent) {
        if (cancelled && dispatch_done) finish(cancelled == 1 ? 5 : 6);
        return;
    }
    if ((cancelled || stop_intent) && !stop_sent) {
        stop_sent = true; barrier(); sdk_arranger_bounce_send(wire::Stop, cookie, 0, 0, 0);
    }
    if (!stop_ack || !dispatch_done) return;
    if (!saving) {
        if (cancelled || !transport_started || captured_frames < trim_frames + request.frames ||
            captured_frames > 882000u) {
            fail_reason(!cancelled && transport_started && captured_frames < trim_frames + request.frames ? 2 : 1);
            finish(cancelled == 1 ? 5 : 6); return;
        }
        saving = true; state = 3; barrier();
        stock<void(u32, u32)>(0x8c0a4bcc)(editor, captured_frames);
        return;
    }
    // Remote Release drains any queued writer before acknowledging.
    if (cancelled) { finish(cancelled == 1 ? 5 : 6); return; }
    if (!save_complete || !writer_done) return;
    barrier();
    if (cancelled || !bound()) { finish(cancelled == 1 ? 5 : 6); return; }
    const u32 frames = captured_frames - trim_frames;
    if (!writer_ok) { fail_reason(3); finish(6); return; }
    if (writer_frames != frames || saved_frames != frames || !copy_path(saved_path) ||
        writer_path_hash != path_hash(saved_path)) { fail_reason(4); finish(6); return; }
    if (!sdk_arranger_bounce_destination_begin(request.slot, request.engine, request.replace, saved_path, frames)) {
        fail_reason(5); finish(6); return;
    }
    state = 7;
}

void sdk_arranger_bounce_saved(u32 object, u32 frames) {
    if (!active || object != editor) { stock<void(u32, u32)>(0x8c0a44f0)(object, frames); return; }
    if (!saving || save_complete) return;
    saved_frames = frames; barrier(); save_complete = 1;
}
void sdk_arranger_bounce_save_range(const char* path, u32 begin, u32 end, i32 key) {
    if (active && saving && !begin && end == captured_frames && key == 60) begin = trim_frames;
    stock<void(const char*, u32, u32, i32)>(0x8d4d8874)(path, begin, end, key);
}
