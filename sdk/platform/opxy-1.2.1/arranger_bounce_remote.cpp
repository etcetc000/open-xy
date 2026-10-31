// Recorder state is slave-local; only the service owner sends bridge messages.
#include "arranger_bounce_remote.hpp"
#include "arranger_bounce.hpp"
#include "stock.hpp"
#include "barrier.hpp"

namespace {
using namespace opxy;
using namespace opxy::platform;
namespace wire = opxy::platform::bounce_wire;
const u32 recorder = 0x8429d5b0;
enum Failure { Busy = 1, Invalid = 2, Capture = 3, Timeout = 4, Save = 5 };
volatile u32 owned, frames, captures, capture_error, stopped;
volatile u32 saving, writer_done, writer_ok, writer_frames, writer_hash, save_complete;
u32 last_cookie, rejected_cookie, released_cookie, previous_source, previous_enabled, previous_threshold;
u32 started_ms, saving_ms, reported_frames, reported_error, expected_frames;
bool ready_sent, stop_sent, result_sent, release_pending;
char expected_path[97];

u32 remote_now() { return stock<u32(u32, u32)>(0x8d5be290)(0, 0); }
void set_byte(u32 address, u8 value) { *at<volatile u8>(address) = value; }
void emit(u32 kind, u32 a = 0, u32 b = 0, u32 c = 0) {
    sdk_arranger_bounce_send(kind, owned, a, b, c);
}
void native_command(u8 command) { stock<void(const void*, u32)>(0x8d4d83b8)(&command, 1); }
void stop_capture() {
    if (stopped) return;
    // The native receiver removes the capture pointer before disabling its recorder.
    native_command(1);
    barrier();
    if (!word(0x842e2a18) && !byte(recorder + 0x25)) {
        frames = word(recorder + 0xc) / 2;
        barrier(); stopped = 1;
    }
}
void release_capture() {
    if (!stopped || (saving && !save_complete)) return;
    const u32 cookie = owned;
    // Drain the stock end notification before Released unmasks the master's editor.
    stock<void()>(0x8d4d86c0)();
    stock<void(u32)>(0x8d4db404)(previous_source);
    stock<void(u32)>(0x8d4db47c)(previous_enabled);
    set_byte(recorder + 0x26, static_cast<u8>(previous_threshold));
    released_cookie = cookie;
    barrier(); owned = 0;
    sdk_arranger_bounce_send(wire::Released, cookie, 0, 0, 0);
}
void reject(u32 cookie, u32 reason) {
    rejected_cookie = cookie;
    sdk_arranger_bounce_send(wire::Error, cookie, reason, 0, 0);
    sdk_arranger_bounce_send(wire::Released, cookie, 0, 0, 0);
}
void prepare(const wire::Message& message) {
    if (message.cookie == owned) {
        if (stopped) emit(wire::Stopped, frames, captures);
        else if (ready_sent) emit(wire::Ready, frames, captures);
        return;
    }
    const u32 distance = message.cookie - last_cookie;
    if (last_cookie && (!distance || distance >= 0x80000000u)) {
        if (message.cookie == released_cookie || message.cookie == rejected_cookie)
            sdk_arranger_bounce_send(wire::Released, message.cookie, 0, 0, 0);
        return;
    }
    last_cookie = message.cookie;
    if (message.args[0] <= 6000 || message.args[0] > 877904 || message.args[1] || message.args[2]) {
        reject(message.cookie, Invalid); return;
    }
    if (owned || word(0x842e2a18) || byte(recorder + 0x25) ||
        word(0x84a50b7c) != word(0x84a50b80)) { reject(message.cookie, Busy); return; }
    previous_source = stock<u32()>(0x8d4db3f0)();
    previous_enabled = stock<u32()>(0x8d4db468)();
    previous_threshold = byte(recorder + 0x26);
    frames = captures = capture_error = stopped = saving = writer_done = writer_ok = writer_frames = writer_hash = save_complete = 0;
    reported_frames = reported_error = expected_frames = 0;
    ready_sent = stop_sent = result_sent = release_pending = false;
    expected_path[0] = 0;
    started_ms = remote_now();
    stock<void(u32)>(0x8d4db404)(4);
    stock<void(u32)>(0x8d4db47c)(1);
    barrier(); owned = message.cookie; barrier();
    native_command(0);
}
bool bind_save(const u8* payload, u32 size) {
    if (size < 11 || !stopped || saving || release_pending) return false;
    const u32 length = payload[1];
    if (!length || length > 96 || size != length + 11) return false;
    const u32 begin = wire::read_word(payload + 2 + length);
    const u32 end = wire::read_word(payload + 6 + length);
    if (end > frames || begin >= end || end - begin <= 6000 || payload[10 + length] != 60) return false;
    for (u32 i = 0; i < length; ++i) if (!payload[2 + i]) return false;
    for (u32 i = 0; i < length; ++i) expected_path[i] = static_cast<char>(payload[2 + i]);
    expected_path[length] = 0;
    expected_frames = end - begin;
    saving_ms = remote_now();
    barrier(); saving = 1;
    return true;
}
bool same_path(const char* path) {
    if (!path) return false;
    for (u32 i = 0; i < 97; ++i) {
        if (path[i] != expected_path[i]) return false;
        if (!path[i]) return true;
    }
    return false;
}
u32 path_hash() {
    u32 hash = 2166136261u;
    for (u32 i = 0; expected_path[i]; ++i) hash = (hash ^ static_cast<u8>(expected_path[i])) * 16777619u;
    return hash;
}
}

void sdk_arranger_bounce_send(u32 kind, u32 cookie, u32 a, u32 b, u32 c) {
    const wire::Message message = {static_cast<wire::Kind>(kind), cookie, {a, b, c}};
    u8 payload[wire::kSize];
    if (!wire::encode(message, payload)) return;
    stock<void(u32, u32, const void*, u32, u32, u32, u32)>(0x11a00ec0)
        (0x841460b4, 0xa4, payload, wire::kSize, 0, 255, 255);
}

void sdk_arranger_bounce_message(const void* data, u32 size) {
    if (!data || !size) return;
    const bool master = stock<u32()>(0x8d5c3eac)() != 0;
    wire::Message message;
    if (wire::decode(data, size, master, message)) {
        if (master) { sdk_arranger_bounce_receive(&message); return; }
        if (message.kind == wire::Prepare) { prepare(message); return; }
        if (message.cookie != owned) {
            if (message.cookie == released_cookie || message.cookie == rejected_cookie) {
                const u32 kind = message.kind == wire::Stop ? wire::Stopped : wire::Released;
                sdk_arranger_bounce_send(kind, message.cookie, 0, 0, 0);
            }
            return;
        }
        if (message.args[0] || message.args[1] || message.args[2]) { emit(wire::Error, Invalid, frames, captures); return; }
        stop_capture();
        if (message.kind == wire::Release) {
            release_pending = true; release_capture();
        } else if (stopped) {
            stop_sent = true; emit(wire::Stopped, frames, captures);
        }
        return;
    }
    const u8* payload = static_cast<const u8*>(data);
    if (master && (payload[0] == 4 || payload[0] == 13) && sdk_arranger_bounce_busy()) return;
    if (payload && size && !master && owned && payload[0] == 10 && !bind_save(payload, size)) {
        emit(wire::Error, Save, frames, captures); return;
    }
    stock<void(const void*, u32)>(0x8d4d83b8)(data, size);
}

void sdk_arranger_bounce_capture(u32 object, const void* pcm, u32 words) {
    if (!owned || object != recorder) {
        stock<void(u32, const void*, u32)>(0x8d5072d8)(object, pcm, words); return;
    }
    if (captures != 0xffffffffu) ++captures;
    if (stopped || capture_error) return;
    if (words != 128 || !byte(recorder + 0x25) || !byte(recorder + 0x27)) {
        capture_error = Capture; barrier(); return;
    }
    set_byte(recorder + 0x26, 0);
    *at<volatile u32>(0x840483e4) = 4;
    stock<void(u32, const void*, u32)>(0x8d5072d8)(object, pcm, words);
    frames = word(recorder + 0xc) / 2;
    if (frames >= 882000) capture_error = Capture;
    barrier();
}

void sdk_arranger_bounce_stopped(u32 object) {
    stock<void(u32)>(0x8d50713c)(object);
    if (owned && object == recorder) {
        frames = word(recorder + 0xc) / 2;
        barrier(); stopped = 1;
    }
}

void sdk_arranger_bounce_trim(u32 job) {
    // Worker507e76 normally calls507b84, which changes this queued job's
    // PCM pointer (+1c) and frame count (+20) by removing leading/trailing
    // silence. A timeline bounce must retain its explicitly requested rests.
    // Match the owned queued save; ordinary sample recording keeps auto-trim.
    if (job && owned && stopped && saving && !writer_done &&
        word(job + 0x20) == expected_frames && byte(job + 0x24) == 60 &&
        byte(job + 0x25) == 1) {
        const u32 size = word(job + 0x14), capacity = word(job + 0x18);
        if (size && size <= 96 && capacity >= size) {
            const char* path = at<const char>(capacity >= 16 ? word(job + 4) : job + 4);
            if (same_path(path)) return;
        }
    }
    stock<void(u32)>(0x8d507b84)(job);
}

u32 sdk_arranger_bounce_write(const char* path, u32 channels, u32 rate,
    const void* pcm, u32 samples, u32 format, i32 key) {
    const bool ours = owned && saving && !writer_done && same_path(path);
    const u32 result = stock<u32(const char*, u32, u32, const void*, u32, u32, i32)>(0x8d4fe15c)
        (path, channels, rate, pcm, samples, format, key);
    if (ours) {
        writer_ok = result && channels == 2 && rate == 44100 && samples == expected_frames * 2u &&
            expected_frames > 6000 && format == 0 && key == 60;
        writer_frames = samples / 2;
        writer_hash = path_hash();
        barrier(); writer_done = 1;
    }
    return result;
}

void sdk_arranger_bounce_remote_saved(u32 count) {
    stock<void(u32)>(0x8d4d8910)(count);
    if (!owned || !saving) return;
    if (!writer_done) {
        writer_ok = 0; writer_frames = count; writer_hash = path_hash();
        barrier(); writer_done = 1;
    } else if (count != writer_frames) writer_ok = 0;
    // Native508030 follows all PCM work and the queue-empty check; only string cleanup remains.
    barrier(); save_complete = 1;
}

void sdk_arranger_bounce_remote_poll() {
    stock<void()>(0x8d4d86c0)();
    if (!owned) return;
    barrier();
    if (!stopped && remote_now() - started_ms > 22000u) capture_error = Timeout;
    if (saving && !save_complete && remote_now() - saving_ms > 30000u && !reported_error) {
        reported_error = Timeout; emit(wire::Error, Timeout, frames, captures);
    }
    if (capture_error) {
        stop_capture();
        if (!reported_error) { reported_error = capture_error; emit(wire::Error, capture_error, frames, captures); }
    }
    if (!stopped && frames >= 64) {
        if (!ready_sent) { ready_sent = true; reported_frames = frames; emit(wire::Ready, frames, captures); }
        else if (frames != reported_frames) { reported_frames = frames; emit(wire::Progress, frames, captures); }
    }
    if (stopped && !stop_sent) { stop_sent = true; emit(wire::Stopped, frames, captures); }
    if (save_complete && writer_done && !result_sent) { result_sent = true; emit(wire::WriteResult, writer_ok, writer_frames, writer_hash); }
    if (release_pending) release_capture();
}
