#include "../sdk/platform/opxy-1.2.1/telemetry/protocol.hpp"
#include "../sdk/platform/opxy-1.2.1/telemetry/queue.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace opxy_transport::telemetry;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
struct CriticalState { unsigned depth = 0, calls = 0; };
u32 enter(void* p) { CriticalState& s = *static_cast<CriticalState*>(p); CHECK(!s.depth); ++s.depth; ++s.calls; return 0x1357; }
void leave(void* p, u32 token) { CriticalState& s = *static_cast<CriticalState*>(p); CHECK(s.depth == 1 && token == 0x1357); --s.depth; }
struct Registry {
    Producer<1> accumulator{Mode::AccumulatePacked16};
    Producer<1> accumulator_track1{Mode::AccumulatePacked16};
    Producer<16> snapshot;
    Producer<256> wide;
    Producer<33> resettable{Mode::Persistent, true};
    static Bank* resolve(void* p, u16 engine, u8 track) {
        Registry& r = *static_cast<Registry*>(p);
        if (track == 1 && engine == 1) return &r.accumulator_track1.bank;
        if (track != 0) return 0;
        switch (engine) { case 1: return &r.accumulator.bank; case 2: return &r.snapshot.bank;
                        case 3: return &r.wide.bank; case 4: return &r.resettable.bank; default: return 0; }
    }
};
struct Pipe {
    std::vector<Packet> packets;
    bool full = false, drop = false;
    unsigned attempts = 0;
    static bool send(void* p, const Packet& packet) {
        Pipe& pipe = *static_cast<Pipe*>(p); ++pipe.attempts;
        if (pipe.full) return false;
        if (!pipe.drop) pipe.packets.push_back(packet);
        return true;
    }
    Packet pop() { CHECK(!packets.empty()); Packet p = packets.front(); packets.erase(packets.begin()); return p; }
};
struct Fixture {
    Registry registry;
    CriticalState critical;
    AudioEndpoint audio{Critical{&critical, enter, leave}, Registry::resolve, &registry};
    GuiEndpoint gui;
    Pipe up, down;
    void select(u16 id, u16 words, Mode mode, u32 gen = 1, bool reset = false, u32 now = 0, u8 track = 0) {
        CHECK(gui.select(Key{id, track, gen}, words, mode, reset));
        CHECK(gui.service(now, Pipe::send, &up));
        Packet p = up.pop(); CHECK(audio.receive(p.bytes, p.size, now));
    }
    Packet publish(u32 now) { CHECK(audio.service(now, Pipe::send, &down)); return down.pop(); }
    void receive(const Packet& p) { CHECK(gui.receive(p.bytes, p.size)); }
    void ack(u32 now) {
        CHECK(gui.service(now, Pipe::send, &up)); Packet p = up.pop(); Header h;
        CHECK(decode(p.bytes, p.size, h) && h.kind == Kind::Ack);
        CHECK(audio.receive(p.bytes, p.size, now));
    }
};
void wire_validation() {
    u32 values[54] = {}; Packet p;
    Header h = {Kind::Snapshot, Mode::Persistent, {3, 0, 1}, 7, 0, 256, 0, 54};
    CHECK(encode(h, values, p)); CHECK(p.size == 246);
    Header out;
    for (unsigned n = 0; n < p.size; ++n) CHECK(!decode(p.bytes, n, out));
    CHECK(!decode(0, p.size, out)); CHECK(!decode(p.bytes, 250, out));
    for (unsigned field : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 10u, 11u, 29u}) {
        Packet bad = p; bad.bytes[field] = 255; CHECK(!decode(bad.bytes, bad.size, out));
    }
    Packet bad = p; bad.bytes[26] = 1; CHECK(!decode(bad.bytes, bad.size, out));
    bad = p; bad.bytes[5] = 1; CHECK(!decode(bad.bytes, bad.size, out));
    bad = p; bad.bytes[24] = 1; bad.bytes[25] = 1; CHECK(!decode(bad.bytes, bad.size, out));
    h.offset = 216; h.count = 40; CHECK(encode(h, values, p)); CHECK(p.size == 190);
    h.offset = 255; h.count = 1; CHECK(!encode(h, values, p));
    h.offset = 0; h.count = 54; h.total = 257; CHECK(!encode(h, values, p));
    h.kind = Kind::Request; h.total = 256; h.count = 0; h.sequence = 0; CHECK(encode(h, 0, p));
    CHECK(p.size == header_bytes); h.count = 1; CHECK(!encode(h, values, p));
    CHECK(newer(1, 0xffffffffu)); CHECK(!newer(0xffffffffu, 1));
    CHECK(packed_add(0x7fff7fff, 0x00010001) == 0x7fff7fff);
    CHECK(packed_add(0x80008000, 0xffffFFFF) == 0x80008000);
}
void accumulator_loss_and_duplicate() {
    Fixture f; f.select(1, 1, Mode::AccumulatePacked16);
    f.registry.accumulator.bank.add(0, 0x00020064);
    Packet first = f.publish(0); CHECK(f.registry.accumulator.storage[0] == 0);
    f.registry.accumulator.bank.add(0, 0x00030014);
    f.receive(first); u32 out[256]; CHECK(f.gui.take(out, 256) == 1 && out[0] == 0x00020064);
    f.receive(first); CHECK(f.gui.take(out, 256) == 0);
    f.up.drop = true; CHECK(f.gui.service(1, Pipe::send, &f.up)); CHECK(f.audio.in_flight());
    f.up.drop = false; Packet retry = f.publish(64); f.receive(retry);
    CHECK(f.gui.take(out, 256) == 0); f.ack(64); CHECK(!f.audio.in_flight());
    Packet second = f.publish(80); f.receive(second);
    CHECK(f.gui.take(out, 256) == 1 && out[0] == 0x00030014); f.ack(80);
    CHECK(!f.gui.receive(first.bytes, first.size));
    CHECK(!f.critical.depth && f.critical.calls >= 2);
}
void accumulator_backpressure_and_cancel() {
    Fixture f; f.select(1, 1, Mode::AccumulatePacked16);
    f.registry.accumulator.bank.add(0, 100); f.down.full = true;
    CHECK(!f.audio.service(0, Pipe::send, &f.down)); CHECK(f.audio.in_flight());
    f.registry.accumulator.bank.add(0, 20); CHECK(f.registry.accumulator.storage[0] == 20);
    CHECK(!f.audio.service(0, Pipe::send, &f.down)); CHECK(f.down.attempts == 1);
    f.down.full = false; Packet p = f.publish(1); f.receive(p);
    u32 out[1]; CHECK(f.gui.take(out, 1) == 1 && out[0] == 100);
    // Expiration parks the flight without merging possibly consumed data back.
    CHECK(!f.audio.service(1000, Pipe::send, &f.down)); CHECK(f.audio.in_flight());
    CHECK(f.registry.accumulator.storage[0] == 20);
    f.select(2, 16, Mode::Persistent, 2, false, 1001);
    CHECK(!f.audio.in_flight() && f.registry.accumulator.storage[0] == 20);
}
void fragmented_coherence() {
    Fixture f; f.select(3, 256, Mode::Persistent);
    for (unsigned i = 0; i < 256; ++i) f.registry.wide.bank.publish(i, 0x1000 + i);
    std::vector<Packet> fragments; fragments.push_back(f.publish(0));
    for (unsigned i = 0; i < 256; ++i) f.registry.wide.bank.publish(i, 0x2000 + i);
    for (u32 now = 1; now < 5; ++now) fragments.push_back(f.publish(now));
    u32 out[256] = {};
    for (int i = 4; i >= 1; --i) { f.receive(fragments[unsigned(i)]); CHECK(f.gui.take(out, 256) == 0); }
    f.receive(fragments[4]); CHECK(f.gui.take(out, 256) == 0);
    Packet conflict = fragments[4]; conflict.bytes[header_bytes] ^= 1;
    CHECK(!f.gui.receive(conflict.bytes, conflict.size));
    f.receive(fragments[0]); CHECK(f.gui.take(out, 255) == 0);
    CHECK(f.gui.take(out, 256) == 256);
    for (unsigned i = 0; i < 256; ++i) CHECK(out[i] == 0x1000 + i);
    CHECK(f.gui.take(out, 256) == 256); // Persistent frames never clear.
    f.up.full = true; CHECK(!f.gui.service(5, Pipe::send, &f.up)); CHECK(f.audio.in_flight());
    f.up.full = false; f.ack(6);
    for (u32 now = 22; now < 27; ++now) {
        Packet p = f.publish(now); f.receive(p);
        CHECK(f.gui.take(out, 256) == 256);
        CHECK(out[0] == (now == 26 ? 0x2000u : 0x1000u));
    }
    for (unsigned i = 0; i < 256; ++i) CHECK(out[i] == 0x2000 + i);
    CHECK(!f.gui.receive(fragments[0].bytes, fragments[0].size));
}
void fragment_loss_retry_and_generation() {
    Fixture f; f.select(3, 256, Mode::Persistent);
    Packet oldrequest; Header request = {Kind::Request, Mode::Persistent, {3,0,1}, 0, 0, 256, 0, 0};
    CHECK(encode(request, 0, oldrequest));
    for (unsigned i = 0; i < 256; ++i) f.registry.wide.bank.publish(i, i);
    for (u32 t = 0; t < 5; ++t) { Packet p = f.publish(t); if (t != 2) f.receive(p); }
    u32 out[256]; CHECK(!f.gui.take(out, 256));
    for (u32 t = 68; t < 71; ++t) { Packet p = f.publish(t); f.receive(p); }
    CHECK(f.gui.take(out, 256) == 256 && out[255] == 255);
    Packet stale = f.publish(71);
    f.select(2, 16, Mode::Persistent, 2, false, 72);
    CHECK(!f.audio.receive(oldrequest.bytes, oldrequest.size, 73));
    CHECK(!f.gui.receive(stale.bytes, stale.size));
    request.key.generation = 2; request.key.track = 1; CHECK(encode(request, 0, oldrequest));
    CHECK(!f.audio.receive(oldrequest.bytes, oldrequest.size, 73));
    CHECK(!f.gui.select(Key{2,0,1}, 16, Mode::Persistent));
}
void reset_acknowledgment() {
    Fixture f; f.select(4, 33, Mode::Persistent, 1, true);
    u32 command = f.gui.request_reset(); CHECK(command == 1);
    CHECK(f.gui.service(1, Pipe::send, &f.up)); Packet request = f.up.pop();
    CHECK(f.audio.receive(request.bytes, request.size, 1)); CHECK(f.registry.resettable.bank.pending_command() == command);
    Packet before = f.publish(1); f.receive(before); CHECK(f.gui.applied_command() == 0); f.ack(2);
    // Only the audio owner may acknowledge the actual engine cursor mutation.
    f.registry.resettable.bank.command_applied(command);
    CHECK(!f.registry.resettable.bank.pending_command());
    CHECK(f.audio.receive(request.bytes, request.size, 3)); CHECK(!f.registry.resettable.bank.pending_command());
    Packet after = f.publish(18); f.receive(after); CHECK(f.gui.applied_command() == command); f.ack(19);
    f.select(2, 16, Mode::Persistent, 2, false, 20); CHECK(!f.gui.request_reset());
    f.select(4, 33, Mode::Persistent, 3, true, 21); CHECK(!f.registry.resettable.bank.pending_command());
    CHECK(f.gui.request_reset() == 2); CHECK(f.gui.service(22, Pipe::send, &f.up));
    Packet next = f.up.pop(); CHECK(f.audio.receive(next.bytes, next.size, 22));
    CHECK(f.registry.resettable.bank.pending_command() == 2);
    CHECK(!f.audio.receive(request.bytes, request.size, 23));
}
void clock_wrap() {
    Fixture f; f.select(2, 16, Mode::Persistent, 0xffffffffu, false, 0xfffffff0u);
    Packet p = f.publish(0xfffffff0u); f.receive(p); f.ack(0xfffffff1u);
    CHECK(!f.audio.service(0, Pipe::send, &f.down));
    p = f.publish(1); f.receive(p); f.ack(1);
    f.select(2, 16, Mode::Persistent, 1, false, 2);
}
void producer_transaction() {
    Fixture f; f.select(2, 16, Mode::Persistent);
    Bank& bank = f.registry.snapshot.bank;
    bank.begin_update(); bank.publish(0, 10);
    CHECK(!f.audio.service(0, Pipe::send, &f.down)); CHECK(f.down.attempts == 0);
    for (unsigned i = 1; i < 16; ++i) bank.publish(i, 10 + i);
    bank.end_update(); Packet p = f.publish(1); f.receive(p);
    u32 out[16]; CHECK(f.gui.take(out, 16) == 16);
    for (unsigned i = 0; i < 16; ++i) CHECK(out[i] == 10 + i);
    Fixture accumulator; accumulator.select(1, 1, Mode::AccumulatePacked16);
    accumulator.registry.accumulator.bank.add(0, 100); accumulator.publish(0);
    accumulator.registry.accumulator.bank.begin_update(); accumulator.registry.accumulator.bank.add(0, 20);
    CHECK(!accumulator.audio.service(1000, Pipe::send, &accumulator.down));
    CHECK(accumulator.audio.in_flight() && accumulator.registry.accumulator.storage[0] == 20);
    Header request = {Kind::Request, Mode::Persistent, {2,0,2}, 0, 0, 16, 0, 0}; Packet switch_request;
    CHECK(encode(request, 0, switch_request));
    CHECK(!accumulator.audio.receive(switch_request.bytes, switch_request.size, 1001));
    CHECK(accumulator.audio.in_flight() && accumulator.registry.accumulator.storage[0] == 20);
    accumulator.registry.accumulator.bank.end_update();
    CHECK(accumulator.audio.receive(switch_request.bytes, switch_request.size, 1002));
    CHECK(!accumulator.audio.in_flight() && accumulator.registry.accumulator.storage[0] == 120);
}
void lost_ack_selection_switch() {
    Fixture f; f.select(1, 1, Mode::AccumulatePacked16);
    f.registry.accumulator.bank.add(0, 100); Packet first = f.publish(0); f.receive(first);
    u32 out[1]; CHECK(f.gui.take(out, 1) == 1 && out[0] == 100);
    f.up.drop = true; CHECK(f.gui.service(1, Pipe::send, &f.up)); f.up.drop = false;
    CHECK(f.audio.in_flight()); f.registry.accumulator.bank.add(0, 20);
    // Selection carries receipt1 even though its explicit ACK never arrived.
    f.select(1, 1, Mode::AccumulatePacked16, 2, false, 2, 1);
    CHECK(!f.audio.in_flight() && f.registry.accumulator.storage[0] == 20);
    f.registry.accumulator_track1.bank.add(0, 7); Packet second = f.publish(2); f.receive(second);
    CHECK(f.gui.take(out, 1) == 1 && out[0] == 7);
    f.up.drop = true; CHECK(f.gui.service(3, Pipe::send, &f.up)); f.up.drop = false;
    f.gui.deselect(); // Receipt must survive deselect as well as select.
    f.select(1, 1, Mode::AccumulatePacked16, 3, false, 4);
    Packet returned = f.publish(4); f.receive(returned);
    CHECK(f.gui.take(out, 1) == 1 && out[0] == 20); // Never replay consumed100.
    CHECK(f.registry.accumulator_track1.storage[0] == 0);
    f.ack(5); CHECK(!f.gui.receive(first.bytes, first.size));
}
void lost_ack_lease_resume() {
    Fixture f; f.select(1, 1, Mode::AccumulatePacked16);
    f.registry.accumulator.bank.add(0, 100); Packet first = f.publish(0); f.receive(first);
    u32 out[1]; CHECK(f.gui.take(out, 1) == 1 && out[0] == 100);
    f.up.drop = true; CHECK(f.gui.service(1, Pipe::send, &f.up)); f.up.drop = false;
    f.registry.accumulator.bank.add(0, 20);
    CHECK(!f.audio.service(1000, Pipe::send, &f.down)); CHECK(f.audio.in_flight());
    CHECK(f.registry.accumulator.storage[0] == 20);
    // A delayed old heartbeat cannot restore the flight. It merely resumes the
    // same sequence, which GUI still deduplicates after the quiet interval.
    Header stale = {Kind::Request, Mode::AccumulatePacked16, {1,0,1}, 0, 0, 1, 0, 0}; Packet stale_request;
    CHECK(encode(stale, 0, stale_request)); CHECK(f.audio.receive(stale_request.bytes, stale_request.size, 1001));
    Packet replay = f.publish(1001); f.receive(replay); CHECK(f.gui.take(out, 1) == 0);
    f.up.drop = true; CHECK(f.gui.service(1002, Pipe::send, &f.up)); f.up.drop = false;
    CHECK(f.gui.service(1003, Pipe::send, &f.up)); Packet heartbeat = f.up.pop(); Header h;
    CHECK(decode(heartbeat.bytes, heartbeat.size, h) && h.kind == Kind::Request && h.sequence == 1);
    CHECK(f.audio.receive(heartbeat.bytes, heartbeat.size, 1003)); CHECK(!f.audio.in_flight());
    Packet next = f.publish(1065); f.receive(next);
    CHECK(f.gui.take(out, 1) == 1 && out[0] == 20); f.ack(1066);
    // An explicit ACK is also accepted while parked, without another Request.
    f.registry.accumulator.bank.add(0, 9); Packet parked = f.publish(1082); f.receive(parked);
    CHECK(f.gui.take(out, 1) == 1 && out[0] == 9);
    f.audio.deactivate(); CHECK(f.audio.in_flight()); f.ack(1083); CHECK(!f.audio.in_flight());
}
void undelivered_lease_resume() {
    Fixture f; f.select(1, 1, Mode::AccumulatePacked16);
    f.registry.accumulator.bank.add(0, 100); f.down.drop = true;
    CHECK(f.audio.service(0, Pipe::send, &f.down)); f.down.drop = false;
    f.registry.accumulator.bank.add(0, 20); CHECK(!f.audio.service(1000, Pipe::send, &f.down));
    CHECK(f.audio.in_flight() && f.registry.accumulator.storage[0] == 20);
    CHECK(f.gui.service(1001, Pipe::send, &f.up)); Packet request = f.up.pop();
    CHECK(f.audio.receive(request.bytes, request.size, 1001));
    Packet parked = f.publish(1001); f.receive(parked); u32 out[1];
    CHECK(f.gui.take(out, 1) == 1 && out[0] == 100); f.ack(1002);
    Packet pending = f.publish(1018); f.receive(pending);
    CHECK(f.gui.take(out, 1) == 1 && out[0] == 20);
}
struct FakeQueue {
    QueueState state{0x0803f134, 0, 0, 0x8000, 0, true, true, true};
    bool locked = false, drop = false;
    unsigned sends = 0, flushes = 0;
    u32 enter() { CHECK(!locked); locked = true; return 9; }
    void leave(u32 t) { CHECK(locked && t == 9); locked = false; }
    QueueState snapshot() { CHECK(locked); return state; }
    void batch(u8 value) { CHECK(locked); state.batch = value; }
    void send(const Packet& p) {
        CHECK(locked && state.batch == 1); ++sends;
        if (!drop) state.write = (state.write + p.size + 6) & (state.capacity - 1);
    }
    u32 write_position() { CHECK(locked); return state.write; }
    void flush() { CHECK(!locked && !state.batch); ++flushes; }
};
void atomic_queue_preflight() {
    Packet p; Header h = {Kind::Request, Mode::Persistent, {3,0,1}, 0, 0, 256, 0, 0}; CHECK(encode(h, 0, p));
    FakeQueue q; CHECK(try_send(q, p)); CHECK(q.sends == 1 && q.flushes == 1 && !q.locked && !q.state.batch);
    q.state.write = q.state.capacity - 256; CHECK(!try_send(q, p)); CHECK(q.sends == 1);
    q.state.write = q.state.capacity - 257; CHECK(try_send(q, p)); CHECK(q.sends == 2);
    q.state.read = 1000; q.state.write = 100; CHECK(try_send(q, p)); // wrapped, 900 bytes free
    q.state.write = q.state.read; q.state.batch = 1; unsigned flushes = q.flushes;
    CHECK(try_send(q, p)); CHECK(q.state.batch == 1 && q.flushes == flushes);
    q.state.batch = 0; q.drop = true; CHECK(!try_send(q, p)); CHECK(q.flushes == flushes);
    q.drop = false; q.state.normal_opcode = false; unsigned sends = q.sends;
    CHECK(!try_send(q, p)); CHECK(q.sends == sends && !q.locked);
    q.state.normal_opcode = true; q.state.routes_valid = false; CHECK(!try_send(q, p));
    q.state.routes_valid = true; q.state.capacity = 0; CHECK(!try_send(q, p));
    q.state.capacity = 0x8000; q.state.read = 0x8000; CHECK(!try_send(q, p));
    q.state.read = 0; q.state.vtable = 0; CHECK(!try_send(q, p));
    CHECK(q.sends == sends && !q.locked);
}
int main() {
    wire_validation(); accumulator_loss_and_duplicate(); accumulator_backpressure_and_cancel();
    fragmented_coherence(); fragment_loss_retry_and_generation(); reset_acknowledgment();
    clock_wrap(); producer_transaction(); lost_ack_selection_switch();
    lost_ack_lease_resume(); undelivered_lease_resume(); atomic_queue_preflight();
    std::puts("telemetry protocol: 12 scenarios passed");
    return 0;
}
