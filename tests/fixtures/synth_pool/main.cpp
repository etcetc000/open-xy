// Runs the same random sequence of stock calls against the built-in adapter
// (voice.cpp.in, generated for the fake synth) and the generic pool, and
// requires the same callbacks and the same output, sample for sample.
#include "core.h"
#include "engine.cpp"
#include "stock.hpp"  // the fixture's stand-ins, extern so the adapter's unit sees them
#include "synth_pool.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace opxy;
namespace fake { Event log[1 << 16]; u32 logged; }
static u32 increments_storage[64];
namespace opxy { namespace platform {
const __UINTPTR_TYPE__ kPitchIncrements = reinterpret_cast<__UINTPTR_TYPE__>(increments_storage);
const __UINTPTR_TYPE__ kVoiceDefaultMethod = 0;
}}
static u32 clock_now;
// The template names these two through __asm__ (no platform underscore); so do we.
extern "C" u32 sdk_voice_ticks() __asm__("sdk_voice_ticks");
extern "C" u32 sdk_voice_ticks() { return clock_now; }
extern "C" {
void* fake_factory(u32) __asm__("fake_factory");
int fake_start(void*);
int fake_destroy(void*);
int fake_render(void*, i32*, const i32*);
}
namespace {
void require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "%s\n", what); std::exit(1); } }
u32 ticks() { return clock_now; }
using V = fake::Voice;
using S = fake::Shared;
void m_initialize(void* v, void* s, u32 id) { fake::initialize(*static_cast<V*>(v), *static_cast<S*>(s), id); }
void m_start(void* v, void* s, u32 id) { fake::start(*static_cast<V*>(v), *static_cast<S*>(s), id); }
void m_destroy(void* v, void* s, u32 id) { fake::destroy(*static_cast<V*>(v), *static_cast<S*>(s), id); }
void m_render(void* v, void* s, u32 id, i32* out, const i32* c, const u32* inc) {
    fake::render(*static_cast<V*>(v), *static_cast<S*>(s), id, out, c, inc);
}
const void* const pool_vtable[1] = {nullptr};

struct Trace { std::vector<fake::Event> events; std::vector<i32> output; };

struct Rng { u32 s; u32 next() { s = s * 1664525u + 1013904223u; return s >> 8; } };

template <class Ops> Trace run(Ops ops, u32 seed, u32 steps) {
    fake::logged = 0; clock_now = 0;
    Trace t;
    Rng r{seed};
    void* objects[synth::voice_indices] = {};
    i32 out[block_frames], controls[8] = {};
    for (u32 n = 0; n < steps; ++n) {
        const u32 index = r.next() % 12, what = r.next() % 100;
        controls[0] = static_cast<i32>(r.next() % 7);
        if (what < 20) objects[index] = ops.factory(index);
        else if (what < 35) { if (objects[index]) ops.start(objects[index]); }
        else if (what < 85) {
            // A block: stock renders every index whose object exists, in order.
            for (u32 i = 0; i < 12; ++i) if (objects[i] && r.next() % 4) {
                ops.render(objects[i], out, controls);
                t.output.insert(t.output.end(), out, out + block_frames);
            }
            clock_now += 4 + r.next() % 3;
        }
        else if (what < 92) { if (objects[index]) ops.destroy(objects[index]); }
        else clock_now += 50 + r.next() % 100;  // a long pause: notes end by silence
    }
    t.events.assign(fake::log, fake::log + fake::logged);
    return t;
}

struct Builtin {
    void* factory(u32 i) { return fake_factory(i); }
    void start(void* h) { fake_start(h); }
    void destroy(void* h) { fake_destroy(h); }
    void render(void* h, i32* o, const i32* c) { fake_render(h, o, c); }
};
struct Generic {
    synth::Pool* pool;
    void* factory(u32 i) { return synth::factory(*pool, i); }
    void start(void* h) { synth::start(*pool, h); }
    void destroy(void* h) { synth::destroy(*pool, h); }
    void render(void* h, i32* o, const i32* c) { synth::render(*pool, h, o, c); }
};
bool same(const fake::Event& a, const fake::Event& b) {
    return a.op == b.op && a.id == b.id && a.starts == b.starts && a.renders == b.renders;
}
}

int main() {
    const synth::Machine machine = {sizeof(V), alignof(V), 0, CAPACITY, m_initialize, m_start, m_destroy, m_render};
    std::vector<unsigned char> storage(synth::storage_bytes(machine));
    static synth::Pool pool;
    // The built-in adapter's state is static, so each seed runs in a fresh process.
    const u32 seed = static_cast<u32>(std::atoi(std::getenv("POOL_SEED") ? std::getenv("POOL_SEED") : "1"));
    const Trace a = run(Builtin{}, seed, 4000);
    require(synth::setup(pool, &machine, pool_vtable, ticks, increments_storage, storage.data()), "setup refused");
    const Trace b = run(Generic{&pool}, seed, 4000);
    require(a.events.size() == b.events.size(), "different number of callbacks");
    for (size_t i = 0; i < a.events.size(); ++i) require(same(a.events[i], b.events[i]), "callbacks differ");
    require(a.output == b.output, "output differs");
    require(a.events.size() > 100, "sequence too short to mean anything");
    std::printf("%zu callbacks, %zu samples equal\n", a.events.size(), a.output.size());
    return 0;
}
