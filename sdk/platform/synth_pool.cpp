// Ported from voice.cpp.in; comments there explain each rule in full.
#include "synth_pool.hpp"
#include <opxy/audio.hpp>
#include <opxy/q31.hpp>
namespace opxy { namespace synth {
namespace {
u32 age(const Pool& p, u16 stamp) { return static_cast<u16>(p.blocks - stamp); }
u32 owner(const Pool& p, const Handle* h) { return static_cast<u32>(h - p.handles) + 1; }
Slot* held(Pool& p, const Handle* h) {
    return h->slot && p.slots[h->slot - 1].owner == owner(p, h) ? &p.slots[h->slot - 1] : nullptr;
}
bool sounding(const Pool& p, const Slot& s, u32 now) {
    if (now - p.counted > silent_ticks && now - s.since > silent_ticks) return false;
    return age(p, p.handles[s.owner - 1].seen) <= 2 || age(p, s.born) <= 2;
}
void begin(Pool& p, Slot& s, Handle* h, u32 now) {
    const u32 id = owner(p, h);
    for (u32 i = 0; i < p.machine->voice_bytes; ++i) s.voice[i] = 0;
    s.owner = id; s.next = 0; s.born = p.blocks; s.since = now;
    h->slot = static_cast<u8>(&s - p.slots + 1);
    h->wants = 0;
    p.machine->initialize(s.voice, p.shared, id);
    p.machine->start(s.voice, p.shared, id);
}
void release(Pool& p, Slot& s, u32 now) {
    p.machine->destroy(s.voice, p.shared, s.owner);
    s.owner = 0;
    if (s.next) begin(p, s, &p.handles[s.next - 1], now);
}
Slot* pending(Pool& p, const Handle* h) {
    Slot* s = h->slot ? &p.slots[h->slot - 1] : nullptr;
    return s && s->owner && s->next == owner(p, h) ? s : nullptr;
}
void forget(Pool& p, Slot& s) {
    if (!s.next) return;
    const Handle& waiter = p.handles[s.next - 1];
    if (age(p, waiter.seen) > 2 && age(p, waiter.asked) > 2) s.next = 0;
}
bool ended(Pool& p, Slot& s, u32 now) { forget(p, s); return !s.next && !sounding(p, s, now); }
void sweep(Pool& p, u32 now) {
    for (u32 i = 0; i < p.voices; ++i)
        if (p.slots[i].owner && ended(p, p.slots[i], now)) release(p, p.slots[i], now);
}
bool before(const Pool& p, const Slot& a, const Slot& b) {
    if (!a.next != !b.next) return !a.next;
    const bool a_young = age(p, a.born) < young_blocks, b_young = age(p, b.born) < young_blocks;
    if (a_young != b_young) return !a_young;
    const u32 a_quiet = age(p, p.handles[a.owner - 1].seen), b_quiet = age(p, p.handles[b.owner - 1].seen);
    if (a_quiet != b_quiet) return a_quiet > b_quiet;
    return age(p, a.born) > age(p, b.born);
}
void acquire(Pool& p, Handle* h, u32 now, bool steal) {
    Slot* open = nullptr;
    Slot* chosen = nullptr;
    for (u32 i = 0; i < p.voices; ++i) {
        Slot& s = p.slots[i];
        if (!s.owner) { open = &s; break; }
        if (ended(p, s, now)) { if (!open) open = &s; }
        else if (steal && (!chosen || before(p, s, *chosen))) chosen = &s;
    }
    if (open) {
        if (open->owner) release(p, *open, now);
        begin(p, *open, h, now);
        return;
    }
    h->slot = 0;
    h->wants = 1;
    if (!steal) return;
    if (chosen->next) {
        Handle& displaced = p.handles[chosen->next - 1];
        displaced.slot = 0;
        displaced.wants = 1;
    }
    chosen->next = owner(p, h);
    h->slot = static_cast<u8>(chosen - p.slots + 1);
    h->asked = p.blocks;
    h->wants = 0;
}
void end(Pool& p, Handle* h) {
    if (Slot* s = held(p, h)) release(p, *s, p.ticks());
    else if (Slot* s = pending(p, h)) s->next = 0;
    h->slot = 0;
    h->wants = 0;
}
u32 round_up(u32 v, u32 a) { return (v + a - 1) & ~(a - 1); }
u32 voices_of(const Machine& m) { return (m.capacity < stock_voices ? m.capacity : stock_voices) + 1; }
}

u32 storage_bytes(const Machine& m) {
    const u32 a = m.voice_alignment ? m.voice_alignment : 1;
    return round_up(m.voice_bytes, a) * voices_of(m) + round_up(m.shared_bytes ? m.shared_bytes : 1, a) + a;
}

bool setup(Pool& p, const Machine* m, const void* const* vtable, u32 (*ticks)(), const u32* increments,
           unsigned char* storage) {
    if (!m || !vtable || !ticks || !storage || !m->initialize || !m->start || !m->destroy || !m->render ||
        !m->voice_bytes || !m->capacity || m->voice_bytes > 0x100000 || m->shared_bytes > 0x100000 ||
        (m->voice_alignment & (m->voice_alignment - 1)) || m->voice_alignment > 64) return false;
    const u32 a = m->voice_alignment ? m->voice_alignment : 1;
    unsigned char* aligned = reinterpret_cast<unsigned char*>(
        (reinterpret_cast<__UINTPTR_TYPE__>(storage) + a - 1) & ~static_cast<__UINTPTR_TYPE__>(a - 1));
    p.machine = m; p.vtable = vtable; p.ticks = ticks; p.increments = increments;
    p.voices = voices_of(*m);
    p.blocks = 1; p.counted = 0;
    for (u32 i = 0; i < voice_indices; ++i) p.handles[i] = Handle{nullptr, 0, 0, 0, 0};
    const u32 stride = round_up(m->voice_bytes, a);
    for (u32 i = 0; i < max_voices; ++i) p.slots[i] = Slot{i < p.voices ? aligned + i * stride : nullptr, 0, 0, 0, 0};
    p.shared = aligned + stride * p.voices;
    for (u32 i = 0; i < (m->shared_bytes ? m->shared_bytes : 1); ++i) p.shared[i] = 0;
    return true;
}

bool owns(const Pool& p, const void* handle) {
    const Handle* h = static_cast<const Handle*>(handle);
    return h >= p.handles && h < p.handles + voice_indices;
}

void* factory(Pool& p, u32 index) {
    if (index >= voice_indices) return nullptr;
    Handle* h = &p.handles[index];
    end(p, h);
    h->vtable = p.vtable;
    return h;
}

int start(Pool& p, void* self) {
    Handle* h = static_cast<Handle*>(self);
    const u32 now = p.ticks();
    if (now - p.counted > silent_ticks) sweep(p, now);
    if (pending(p, h)) return 0;
    if (Slot* s = held(p, h)) {
        if (!s->next) {
            s->born = p.blocks;
            s->since = now;
            p.machine->start(s->voice, p.shared, owner(p, h));
            return 0;
        }
        release(p, *s, now);
    }
    acquire(p, h, now, true);
    return 0;
}

int destroy(Pool& p, void* self) { end(p, static_cast<Handle*>(self)); return 0; }

int render(Pool& p, void* self, i32* output, const i32* parameters) {
    Handle* h = static_cast<Handle*>(self);
    if (h->seen == p.blocks) {
        ++p.blocks;
        p.counted = p.ticks();
        sweep(p, p.counted);
    }
    h->seen = p.blocks;
    const u32 now = p.counted;
    if (Slot* s = pending(p, h))
        if (!sounding(p, *s, now)) release(p, *s, now);
    if (h->wants && !held(p, h) && !pending(p, h)) acquire(p, h, now, false);
    if (Slot* s = held(p, h)) {
        p.machine->render(s->voice, p.shared, s->owner, output, parameters, p.increments);
        if (s->next) {
            const i32 frames = static_cast<i32>(block_frames), step = 0x7fffffff / (frames - 1);
            for (i32 i = 0; i < frames; ++i) output[i] = q31::multiply(output[i], step * (frames - 1 - i));
            release(p, *s, now);
        }
    } else {
        for (u32 i = 0; i < block_frames; ++i) output[i] = 0;
    }
    return 0;
}
}}
