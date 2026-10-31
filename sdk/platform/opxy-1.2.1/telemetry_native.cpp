#define OPXY_AUDIO_TRANSPORT_121 1
#include "audio_transport_native.hpp"
#include "telemetry/xy_stock.hpp"
#include "stock.hpp"

inline void* operator new(__SIZE_TYPE__, void* destination) { return destination; }

namespace opxy { namespace platform { namespace telemetry {
namespace {
// Each processor has its own copy. The receive hook only appends bounded
// packets; endpoint mutation belongs to audio background or GUI frame service.
struct Inbox {
    wire::Packet packets[8];
    u32 read, write;
    bool push(const wire::u8* bytes, u32 size) {
        if (!bytes || size > wire::max_packet_bytes) return false;
        const u32 token = enter();
        if (write - read >= 8) { leave(token); return false; }
        wire::Packet& p = packets[write & 7];
        p.size = size;
        for (u32 i = 0; i < size; ++i) p.bytes[i] = bytes[i];
        ++write;
        leave(token);
        return true;
    }
    bool pop(wire::Packet& packet) {
        const u32 token = enter();
        if (read == write) { leave(token); return false; }
        packet = packets[read & 7];
        ++read;
        leave(token);
        return true;
    }
};
Inbox role_inboxes[2];
alignas(wire::AudioEndpoint) unsigned char audio_storage[sizeof(wire::AudioEndpoint)];
alignas(wire::GuiEndpoint) unsigned char gui_storage[sizeof(wire::GuiEndpoint)];
wire::AudioEndpoint* audio;
wire::GuiEndpoint* gui;
u32 active_audio_engine, active_audio_track;
u32 selected_engine, selected_track, generation, requested_command;
bool reset_selection;
bool gui_role() { return byte(0x86c05ca4u) != 0; }
// Separate destinations even if role memory aliases. This does not
// establish cross-processor coherence or a shared-memory contract.
Inbox& current_inbox() { return role_inboxes[gui_role() ? 1 : 0]; }
u32 advance(u32 value) { ++value; return value ? value : 1; }
wire::GuiEndpoint& gui_endpoint() {
    if (!gui) gui = new (gui_storage) wire::GuiEndpoint();
    return *gui;
}
wire::AudioEndpoint& audio_endpoint() {
    if (!audio) audio = new (audio_storage) wire::AudioEndpoint(wire::xy_critical(), resolve_bank, nullptr);
    return *audio;
}
void pump_gui(wire::GuiEndpoint& endpoint) {
    wire::Packet packet;
    for (u32 i = 0; i < 8 && current_inbox().pop(packet); ++i)
        endpoint.receive(packet.bytes, packet.size);
}
}
u32 enter() { return wire::xy_enter(nullptr); }
void leave(u32 token) { wire::xy_leave(nullptr, token); }
wire::Bank* construct_bank(void* storage, volatile u32* words, u32 count, u32 mode, bool reset, u32 prefix) {
    return new (storage) wire::Bank(words, static_cast<wire::u16>(count), static_cast<wire::Mode>(mode), reset, prefix);
}
void reset_screen(u32 engine) {
    if (gui_role() && selected_engine == engine) reset_selection = true;
}
Snapshot capture(u32 engine, u32 track, u32* output, u32 capacity) {
    Snapshot result = {};
    if (!output || capacity > audio_transport_capacity) return result;
    for (u32 i = 0; i < capacity; ++i) output[i] = 0;
    if (!gui_role()) return result;
    u32 words, mode, prefix; bool reset;
    if (track >= 8 || track_engine(track) != engine || !configuration(engine, words, mode, reset, prefix) || capacity < words) {
        if (gui) gui->deselect();
        selected_engine = 0;
        return result;
    }
    wire::GuiEndpoint& endpoint = gui_endpoint();
    if (selected_engine != engine || selected_track != track || reset_selection) {
        generation = advance(generation);
        if (!endpoint.select(wire::Key{static_cast<wire::u16>(engine), static_cast<wire::u8>(track), generation},
                             static_cast<wire::u16>(words), static_cast<wire::Mode>(mode), reset, prefix)) return result;
        selected_engine = engine; selected_track = track; requested_command = 0; reset_selection = false;
    }
    pump_gui(endpoint);
    result.count = endpoint.take(output, capacity);
    result.sequence = endpoint.sequence(); result.requested = requested_command;
    result.applied = endpoint.applied_command();
    return result;
}
void frame_end(u32 engine, u32 track, u32 reset) {
    if (!gui_role() || !gui || selected_engine != engine || selected_track != track) return;
    if (reset && (!requested_command || requested_command == gui->applied_command()))
        requested_command = gui->request_reset(reset);
    wire::XYQueue queue;
    gui->service(wire::xy_now_ms(), wire::xy_send, &queue);
}

// This is called only from the authenticated audio service-loop hook.
void audio_service() {
    if (gui_role()) return;
    wire::AudioEndpoint& endpoint = audio_endpoint();
    const u32 now = wire::xy_now_ms();
    wire::Packet packet;
    for (u32 i = 0; i < 8 && current_inbox().pop(packet); ++i) {
        wire::Header h;
        if (wire::decode(packet.bytes, packet.size, h) && endpoint.receive(packet.bytes, packet.size, now) &&
            h.kind == wire::Kind::Request) { active_audio_engine = h.key.engine; active_audio_track = h.key.track; }
    }
    if (active_audio_engine && word(0x11b01d48u + active_audio_track * 4) != active_audio_engine) {
        endpoint.deactivate(); active_audio_engine = 0;
    }
    wire::XYQueue queue;
    endpoint.service(now, wire::xy_send, &queue);
}
// Returns true for every SDK envelope, including malformed versions. Stock
// subcommands are forwarded by the assembly wrapper with original registers.
bool receive(const wire::u8* bytes, u32 length) {
    if (!bytes || !length) return true;
    if (!wire::extension(bytes, length)) return false;
    wire::Header h;
    if (!wire::decode(bytes, length, h)) return true;
    const bool gui_side = gui_role();
    if ((h.kind == wire::Kind::Snapshot) != gui_side) return true;
    u32 words, mode, prefix; bool reset;
    if (!configuration(h.key.engine, words, mode, reset, prefix) || words != h.total || mode != u32(h.mode) ||
        (mode == 2 && bytes[29] != prefix) ||
        (!reset && h.command)) return true;
    current_inbox().push(bytes, length);
    return true;
}
}}}
extern "C" {
void sdk_telemetry_audio_service() __asm__("sdk_telemetry_audio_service");
int sdk_telemetry_receive(const unsigned char*, unsigned) __asm__("sdk_telemetry_receive");
void sdk_telemetry_audio_service() { opxy::platform::telemetry::audio_service(); }
int sdk_telemetry_receive(const unsigned char* bytes, unsigned length) {
    return opxy::platform::telemetry::receive(bytes, length);
}
}
