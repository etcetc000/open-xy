// Execute the real send hook (send_fx.cpp) against stock boundary doubles: a
// stock-shaped send call that runs a stereo feedback delay, beside a reference
// delay that stock would run every block.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "send_bus.hpp"
#include "stock.hpp"

using namespace opxy;
using namespace opxy::platform;

extern "C" void sdk_send_fx(u32 engine, u32 track, i32* bus) __asm__("sdk_send_fx");

namespace {
const u32 kFrames = 64;
// The idle floor in raw bus units, the same for every effect (send_fx.cpp).
const i32 kFloor = 1 << 6;
i32 floor_now = kFloor;
// The stock hold: a 512 KiB request as a 16-bit mono line (262144 frames), plus 64 blocks.
const u32 kHold = 4096 + 64;
const u32 kEngine = 0x84c696fc;

void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

// A stereo feedback delay: the output is the line, the line takes the input
// plus the output times the feedback (Q31).
struct Delay {
    std::vector<i32> line;
    u32 position;
    i32 feedback;
    i32 rest;   // a constant the output rests at (a residue: a DC offset or limit cycle)
    void reset(u32 frames, i32 q31) { line.assign(2 * frames, 0); position = 0; feedback = q31; rest = 0; }
    void process(i32* left, i32* right) {
        for (u32 i = 0; i < kFrames; ++i) {
            i32* cell = &line[2 * position];
            const i32 out[2] = {cell[0], cell[1]};
            const i32 in[2] = {left[i], right[i]};
            for (u32 c = 0; c < 2; ++c)
                cell[c] = static_cast<i32>(in[c] + ((static_cast<long long>(out[c]) * feedback) >> 31));
            left[i] = out[0] + rest;
            right[i] = out[1] + rest;
            position = (position + 1) % (line.size() / 2);
        }
    }
};

u32 objects[16], requested[16], current[16];
i32 gains[16];
unsigned char filters[16], engine_byte, playing[2];
u32 words[16][0xe8 / 4];
u32 tempo[2];
i32 scratch_left[kFrames], scratch_right[kFrames];
Delay effects[16], references[16];
u32 calls[16];
// Each effect object's first word, its vtable (only read for bus-shifted sends).
const void* vtables[16];
const char shifted_vtable[1] = {0}, other_vtable[1] = {0};
// The last block, counted from the start of the last until_idle, whose
// reference reached floor_now, and the block the send idled on.
u32 last_audible, idled_on;
u32 published[0x20], publications;

void host_process_send(u32 engine, u32 track, i32* bus) {
    require(engine == kEngine, "Wrong engine passed on");
    ++calls[track];
    if (!objects[track]) return;
    for (u32 i = 0; i < kFrames; ++i) { scratch_left[i] = bus[2 * i]; scratch_right[i] = bus[2 * i + 1]; }
    effects[track].process(scratch_left, scratch_right);
    for (u32 i = 0; i < kFrames; ++i) { bus[2 * i] = scratch_left[i]; bus[2 * i + 1] = scratch_right[i]; }
}

u32 host_publish(u32 id, u32 value) {
    require(id >= 0x1a && id <= 0x1d, "Unexpected telemetry id");
    published[id] = value + 1;
    ++publications;
    return 1;
}

// One block on a send: the hook's bus beside the reference's, from the same
// input. Returns whether the hook ran stock's call.
struct Block { bool called; i32 hooked[2 * kFrames], reference[2 * kFrames]; };

Block run(u32 track, i32 left_in, i32 right_in, u32 at = 0) {
    Block b;
    for (u32 i = 0; i < 2 * kFrames; ++i) b.hooked[i] = 0;
    b.hooked[2 * at] = left_in;
    b.hooked[2 * at + 1] = right_in;
    std::memcpy(b.reference, b.hooked, sizeof b.hooked);
    const u32 before = calls[track];
    sdk_send_fx(kEngine, track, b.hooked);
    b.called = calls[track] != before;
    if (objects[track]) {
        i32 l[kFrames], r[kFrames];
        for (u32 i = 0; i < kFrames; ++i) { l[i] = b.reference[2 * i]; r[i] = b.reference[2 * i + 1]; }
        references[track].process(l, r);
        for (u32 i = 0; i < kFrames; ++i) { b.reference[2 * i] = l[i]; b.reference[2 * i + 1] = r[i]; }
    }
    return b;
}

bool audible(const i32* samples, i32 floor = floor_now) {
    for (u32 i = 0; i < 2 * kFrames; ++i) if (samples[i] >= floor || samples[i] <= -floor) return true;
    return false;
}

i32 difference(const Block& b) {
    i32 worst = 0;
    for (u32 i = 0; i < 2 * kFrames; ++i) {
        const long long d = static_cast<long long>(b.hooked[i]) - b.reference[i];
        const i32 a = static_cast<i32>(d < 0 ? -d : d);
        if (a > worst) worst = a;
    }
    return worst;
}

void settle(u32 track, u32 frames, i32 feedback) {
    objects[track] = 0x84c66b9c + 480 * track;
    requested[track] = current[track] = 0;
    gains[track] = 0x7fffffff;
    effects[track].reset(frames, feedback);
    references[track].reset(frames, feedback);
}

// Silence until the send idles; every block matches the reference exactly
// while the reference is audible, and the hook never skips one that is.
// Returns the blocks it took.
u32 until_idle(u32 track, u32 limit) {
    u32 skipped_after = 0;
    last_audible = 0;
    for (u32 block = 0; block < limit; ++block) {
        const Block b = run(track, 0, 0);
        if (audible(b.reference)) require(b.called && difference(b) == 0, "A send cut an audible tail");
        if (audible(b.reference)) last_audible = block;
        require(difference(b) < floor_now, "A send differs from stock by more than the floor");
        if (!b.called) {
            if (!skipped_after) skipped_after = block;
            for (u32 i = 0; i < 2 * kFrames; ++i) require(b.hooked[i] == 0, "An idle send left sound in its bus");
        } else {
            require(!skipped_after, "A send ran again with nothing changed");
        }
        if (skipped_after && block > skipped_after + 4) { idled_on = skipped_after; return skipped_after; }
    }
    require(false, "The send never idled");
    return 0;
}
}

opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d50f594) return reinterpret_cast<Address>(&host_process_send);
    if (address == 0x8d4d666a) return reinterpret_cast<Address>(&host_publish);
    if (address >= 0x11b01d88 && address < 0x11b01d88 + 64) return reinterpret_cast<Address>(objects) + address - 0x11b01d88;
    if (address >= 0x84042700 && address < 0x84042700 + 64) return reinterpret_cast<Address>(requested) + address - 0x84042700;
    if (address >= 0x8404cb38 && address < 0x8404cb38 + 64) return reinterpret_cast<Address>(current) + address - 0x8404cb38;
    if (address >= 0x8404caf8 && address < 0x8404caf8 + 64) return reinterpret_cast<Address>(gains) + address - 0x8404caf8;
    if (address >= 0x11b01dc8 && address < 0x11b01dc8 + 16) return reinterpret_cast<Address>(filters) + address - 0x11b01dc8;
    if (address >= 0x84eab39c && address < 0x84eab39c + sizeof words) return reinterpret_cast<Address>(words) + address - 0x84eab39c;
    if (address == 0x84c69824) return reinterpret_cast<Address>(tempo);
    if (address == 0x860c9b39) return reinterpret_cast<Address>(playing);
    if (address == 0x84ca9f40) return reinterpret_cast<Address>(scratch_left);
    if (address == 0x84caa040) return reinterpret_cast<Address>(scratch_right);
    if (address == kEngine) return reinterpret_cast<Address>(&engine_byte);
    for (u32 t = 0; t < 16; ++t)
        if (address == 0x84c66b9c + 480 * t) return reinterpret_cast<Address>(&vtables[t]);
    std::fprintf(stderr, "Unmapped stock address %08x\n", static_cast<unsigned>(address));
    std::abort();
}

int main() {
    tempo[0] = 0x42f00000;  // 120.0f
    tempo[1] = 0x42f00000;
    words[14][0] = 0x40000000;

    // A stock effect's tail with echoes 262144 frames (4096 blocks) apart, the
    // longest a 512 KiB line holds as 16-bit mono: the gap between echoes never
    // idles the send, and it idles only after the last echo is below the floor
    // and the hold passed.
    settle(14, 4096 * kFrames, 0x40000000);
    {
        const Block first = run(14, 0x40000000, -0x30000000);
        require(first.called && difference(first) == 0, "The impulse was not processed");
    }
    // The echoes halve from 2^30: the 24th is 2^6, the floor, and the 25th the first below it.
    const u32 idled = until_idle(14, 130000);
    require(idled > 24 * 4096 + kHold, "The send idled before its tail fell below the floor");
    require(idled < 25 * 4096 + kHold + 2, "The send did not idle one hold after its tail fell below the floor");
    require(published[0x1a] == 1 && published[0x1c] == 1 && publications == 2,
            "Idling did not publish the send's two meters as zero once");

    // Idle: silence below the floor does not wake it; the floor does, on the
    // block it arrives in, from either sign, on either channel.
    for (u32 i = 0; i < 100; ++i) {
        const Block b = run(14, kFloor - 1, -(kFloor - 1), i % kFrames);
        require(!b.called, "Sub-floor input woke the send");
        for (u32 j = 0; j < 2 * kFrames; ++j) require(b.hooked[j] == 0, "An idle send passed its input on");
    }
    {
        const Block b = run(14, 0, -kFloor, 17);
        require(b.called, "Input at the floor did not wake the send on its block");
        require(difference(b) < kFloor, "The woken send's state was not silence");
    }
    // Woken by input, it runs the tail out and idles again a hold later.
    references[14] = effects[14];
    const u32 again = until_idle(14, 90000);
    require(again > kHold, "The send idled again before the hold");

    // Each thing the call reads besides audio wakes an idle send on its block
    // and restarts the hold.
    struct Wake { const char* name; void (*change)(); void (*restore)(); };
    const Wake wakes[] = {
        {"a track word", [] { words[14][7] ^= 1; }, [] {}},
        {"the last track word", [] { words[14][0xe8 / 4 - 1] += 3; }, [] {}},
        {"the tempo", [] { tempo[0] = 0x43000000; }, [] {}},
        {"the delay's tempo", [] { tempo[1] = 0x43000000; }, [] {}},
        {"playing", [] { playing[0] = 1; }, [] {}},
        {"the second playing byte", [] { playing[1] = 1; }, [] {}},
        {"the post filter flag", [] { filters[14] = 1; }, [] {}},
    };
    for (const Wake& w : wakes) {
        w.change();
        const Block b = run(14, 0, 0);
        if (!b.called) { std::fprintf(stderr, "%s did not wake the send\n", w.name); return 1; }
        references[14] = effects[14];
        const u32 blocks = until_idle(14, 10000);
        if (blocks < kHold - 1 || blocks > kHold + 1) { std::fprintf(stderr, "%s: idled after %u blocks\n", w.name, blocks); return 1; }
    }

    // Unsettled objects never idle: a pending effect change (stock fades and
    // rebuilds it), a fading gain, a pending engine reset, no object.
    struct Hold { const char* name; void (*set)(); void (*clear)(); };
    const Hold holds[] = {
        {"a requested effect", [] { requested[14] = 5; }, [] { requested[14] = 0; }},
        {"a fading gain", [] { gains[14] = 0x7ffffff0; }, [] { gains[14] = 0x7fffffff; }},
        {"an engine reset", [] { engine_byte = 1; }, [] { engine_byte = 0; }},
        {"no effect object", [] { objects[14] = 0; }, [] { objects[14] = 0x84c66b9c + 480 * 14; }},
    };
    for (const Hold& h : holds) {
        h.set();
        for (u32 block = 0; block < kHold + 10; ++block)
            if (!run(14, 0, 0).called) { std::fprintf(stderr, "%s let the send idle\n", h.name); return 1; }
        h.clear();
        references[14] = effects[14];
        const u32 blocks = until_idle(14, 10000);
        if (blocks < kHold - 1 || blocks > kHold + 1) { std::fprintf(stderr, "%s: idled after %u blocks\n", h.name, blocks); return 1; }
    }

    // Without an effect object stock passes the bus on; so does the hook.
    objects[15] = 0;
    for (u32 block = 0; block < kHold + 10; ++block) {
        const Block b = run(15, block & 1 ? 0 : 5, 0);
        require(b.called && b.hooked[0] == (block & 1 ? 0 : 5), "A send without an effect changed");
    }

    // The sends idle on their own: send 15 runs while 14 stays idle.
    settle(15, 4410, 0x60000000);
    require(!run(14, 0, 0).called, "Send 14 woke for send 15");
    for (u32 block = 0; block < 50; ++block) require(run(15, block ? 0 : 0x20000000, 0).called, "Send 15 idled");
    require(!run(14, 0, 0).called, "Send 14 woke for send 15");

    // A send holding a bus-shifted effect (send_bus.hpp) idles at the same
    // floor, in raw bus units, whatever its shift; its record's hold applies.
    {
        settle(15, 4410, 0x60000000);
        vtables[15] = shifted_vtable;
        platform::send_bus::declare(15, shifted_vtable, kHold);
        require(run(15, 0x20000000, 0).called, "The shifted send did not run its impulse");
        until_idle(15, 60000);
        require(last_audible > 0 && idled_on > last_audible + kHold - 2, "The shifted send idled before its hold");
        for (u32 i = 0; i < 100; ++i)
            require(!run(15, kFloor - 1, -(kFloor - 1), i % kFrames).called, "Sub-floor input woke the shifted send");
        require(run(15, 0, -kFloor, 9).called, "Input at the floor did not wake the shifted send");
        references[15] = effects[15];
        until_idle(15, 60000);
        // Another effect in the slot (another vtable), a shift recorded for the
        // other send, and busShift 12 all idle at the same floor; slots other
        // than the sends are ignored.
        vtables[15] = other_vtable;
        require(!run(15, kFloor - 1, 0).called, "Sub-floor input woke another effect's send");
        vtables[15] = shifted_vtable;
        platform::send_bus::declare(14, shifted_vtable, kHold);
        platform::send_bus::declare(15, shifted_vtable, kHold);
        platform::send_bus::declare(3, other_vtable, 1);
        require(sdk_send_bus[0].vtable == shifted_vtable && sdk_send_bus[1].vtable == shifted_vtable &&
                sdk_send_bus[1].hold == kHold, "A non-send slot was recorded");
        require(!run(15, kFloor - 1, -(kFloor - 1)).called, "Sub-floor input woke the busShift 12 send");
        require(run(15, kFloor, 0).called, "Input at the floor did not wake the busShift 12 send");
        platform::send_bus::declare(14, nullptr, 0);
        platform::send_bus::declare(15, nullptr, 0);
    }

    // A quietly sounding send is never idled (a floor of 2^15 would sit inside
    // the send bus's normal range and idle such a send a hold after its input
    // dropped under it). Input at 2^14, the quietest level read on the sends of
    // one OP-XY (a device reading this repository does not reproduce), for longer
    // than any hold: every block is processed.
    {
        settle(15, 4410, 0x40000000);
        for (u32 block = 0; block < kHold + 200; ++block)
            require(run(15, block % 2 ? (1 << 14) : -(1 << 14), 0, block % kFrames).called,
                    "A quietly sounding send was idled");
        references[15] = effects[15];
        until_idle(15, 60000);
    }

    // An effect whose output rests at a small residue with no input (34 raw here) idles one hold after its
    // input stops, under the floor of 64 raw; a floor of 8 raw would never idle it, and it would cost its
    // whole call every block.
    {
        settle(15, 4410, 0);
        effects[15].rest = references[15].rest = 34;
        require(run(15, kFloor, 0).called, "Input at the floor did not wake the resting send");
        until_idle(15, 60000);
        require(idled_on > last_audible + kHold - 2 && idled_on < last_audible + kHold + 3,
                "A send resting at 34 raw did not idle one hold after its last audible block");
    }

    // An SDK effect's own hold (send_bus.hpp, adapters.send_hold): a short
    // declared tail idles one short hold after its last audible block, while a
    // stale record (another vtable in the slot) falls back to the stock hold.
    {
        const u32 hold = (4410 + 63) / 64 + 64;
        settle(15, 4410, 0x60000000);
        vtables[15] = shifted_vtable;
        platform::send_bus::declare(15, shifted_vtable, hold);
        require(run(15, 0x20000000, 0).called, "The declared send did not run its impulse");
        until_idle(15, 60000);
        require(idled_on > last_audible + hold - 2 && idled_on < last_audible + hold + 3,
                "A send did not idle one declared hold after its tail");
        vtables[15] = other_vtable;
        require(run(15, 0x20000000, 0).called, "The other effect did not run its impulse");
        references[15] = effects[15];
        until_idle(15, 60000);
        require(idled_on > last_audible + kHold - 2, "A stale record's hold applied to another effect");
        // An unbounded tail (a looper, a freeze) never idles its send.
        vtables[15] = shifted_vtable;
        platform::send_bus::declare(15, shifted_vtable, platform::send_bus::endless);
        for (u32 block = 0; block < 3 * kHold; ++block)
            require(run(15, 0, 0).called, "A send with an unbounded tail idled");
        platform::send_bus::declare(15, nullptr, 0);
    }

    // Any other track passes straight through.
    const u32 before = calls[3];
    i32 bus[2 * kFrames] = {9};
    sdk_send_fx(kEngine, 3, bus);
    require(calls[3] == before + 1 && bus[0] == 9, "Another track did not reach stock");
    std::puts("ok");
    return 0;
}
