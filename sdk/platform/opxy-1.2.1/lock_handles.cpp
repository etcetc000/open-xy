// SDK step locks by handle (lock_handles.hpp): the handle word in stock's patterns,
// clipboards and recorder snapshot, the active and staged pools, and the companion's
// open-xy.locks section. GUI role only: the sequencer, the editor, note start, save and
// load all run there.
#include "lock_handles.hpp"
#include "stock.hpp"

namespace opxy { namespace platform { namespace step_locks {
namespace {
// The active project's buffer, whose patterns stock's lock writer (8d586f40) and
// current-pattern lookup (8d586014) address. Load deserializes into the other one.
const u32 kProjectPointer = 0x840a0524;
// A project's patterns: track t at +0xd7c + t * 0x53f44, pattern k of it at
// + k * 0x53f4, the track's pattern count at its +0x53f40 (16 slots at most).
const Address kFirstTrack = 0xd7c, kTrackBytes = 0x53f44, kPatternBytes = 0x53f4, kPatternCount = 0x53f40;
const u32 kAllTracks = 16, kInstrumentTracks = 8, kPatternSlots = 16, kSteps = 128;
// A pattern's machine, as stock's track engine lookup reads it (8d58781c).
const Address kPatternEngine = 0x18;
// A pattern's locks: 42 halfwords a step from +0x258 (0x54 a step), a 64-bit mask a
// step from +0x2c58 (word i's bit), the union of the masks at +0x3058, the slide byte
// at +0x3060 and the 58 stored words from +0x3864.
const Address kValues = 0x258, kStepBytes = 0x54, kMasks = 0x2c58, kBase = 0x3864;
const Address kHandleSlot = kHandleWord * 2;
const u32 kHandleBit = 1u << (kHandleWord - 32);  // in a step mask's high word
// Stock's step clipboard (copy step 8d5824e0, paste step 8d582218): one step's 42
// values from 0x84ffd1ac, its mask at 0x84ffd200 (low word) and 0x84ffd204.
const Address kStepClipValues = 0x84ffd1ac, kStepClipMask = 0x84ffd200;
// The pattern clipboard (copy pattern 8d581a2c, copy for page duplication 8d5819cc,
// copy sound 8d581d48): one whole pattern.
const Address kPatternClip = 0x84ffd208;
// The recorder's snapshot of one pattern's steps (8d5adaf4, restored by 8d5adde4, the
// take's undo): the recorder at 0x860c6adc, the snapshot at its +0x461c, rows from
// +0x788 (0x54 a step) and masks from +0x3188.
const Address kSnapshot = 0x860c6adc + 0x461c, kSnapValues = 0x788, kSnapMasks = 0x3188;
// The GUI's live words, 58 a track (8d5acea8 writes them; the lock pass too).
const u32 kLive = 0x86bd027c + 0x4080, kLiveBytes = 0xe8;

Pool pools[2];
u32 active_pool;     // pools[active_pool] holds the active project's values
Address pending;     // the buffer a load staged pools[1 - active_pool] for, until active
const u32 kMarkBytes = (kHandles + 7) / 8;
u8 marks[kMarkBytes];
// The sequencer's step hook: by track, the handle its lock pass applied for the note
// about to start, while that note start has not taken it.
u16 step_handle[kAllTracks];
bool step_fresh[kAllTracks];
// The section text's budget for the save being written (sdk_step_locks_size).
u32 saved_limit;

volatile u32& word_at(Address address) { return *at<volatile u32>(address); }
volatile u16& half_at(Address address) { return *at<volatile u16>(address); }

Address active_project() { return *at<volatile Address>(kProjectPointer); }
Address pattern(Address project, u32 track, u32 index) {
    return project + kFirstTrack + track * kTrackBytes + index * kPatternBytes;
}
u32 patterns(Address project, u32 track) {
    const u32 n = word_at(project + kFirstTrack + track * kTrackBytes + kPatternCount);
    return n < kPatternSlots ? n : kPatternSlots;
}

// A handle word's place: its mask's high word, its value, and the pattern whose union
// stock keeps (0 for the step clipboard and the snapshot, which keep none).
struct Slot {
    Address mask, value, pattern;
    u32 handle() const { return word_at(mask) & kHandleBit ? half_at(value) : 0; }
};
Slot step_slot(Address p, u32 step) {
    return Slot{p + kMasks + step * 8 + 4, p + kValues + step * kStepBytes + kHandleSlot, p};
}

// Whether a pattern is a client's: its machine is (pattern_client).
bool client_pattern(Address p) { return pattern_client(word_at(p + kPatternEngine)); }

// Every slot holding a handle: in a project's client patterns (instrument tracks
// only), and (`stock_copies`) in the clipboards and the snapshot. The pattern
// clipboard is a whole pattern and names its machine; the step clipboard and the
// snapshot name none, so they are searched whatever track they came from.
template <class Visit> void each_in_pattern(Address p, Visit& visit) {
    for (u32 s = 0; s < kSteps; ++s) {
        const Slot slot = step_slot(p, s);
        if (const u32 h = slot.handle()) visit(slot, h);
    }
}
template <class Visit> void each_copy(Visit& visit) {
    if (client_pattern(kPatternClip)) each_in_pattern(kPatternClip, visit);
    const Slot clip = {kStepClipMask + 4, kStepClipValues + kHandleSlot, 0};
    if (const u32 h = clip.handle()) visit(clip, h);
    for (u32 s = 0; s < kSteps; ++s) {
        const Slot snap = {kSnapshot + kSnapMasks + s * 8 + 4, kSnapshot + kSnapValues + s * kStepBytes + kHandleSlot, 0};
        if (const u32 h = snap.handle()) visit(snap, h);
    }
}
template <class Visit> void each_reference(Address project, bool stock_copies, Visit& visit) {
    if (project)
        for (u32 t = 0; t < kInstrumentTracks; ++t)
            for (u32 k = 0, n = patterns(project, t); k < n; ++k) {
                const Address p = pattern(project, t, k);
                if (client_pattern(p)) each_in_pattern(p, visit);
            }
    if (stock_copies) each_copy(visit);
}

// Clears a slot's handle word: its mask bit and value; then (clear_slot) the
// pattern's union of its steps' masks, through stock's own (8d5843ec).
void clear_word(const Slot& slot) {
    word_at(slot.mask) &= ~kHandleBit;
    half_at(slot.value) = 0;
}
void union_of(Address p) {
    if (p) stock<void(Address)>(0x8d5843ec)(p);
}
void clear_slot(const Slot& slot) {
    clear_word(slot);
    union_of(slot.pattern);
}

struct Marker {
    void operator()(const Slot&, u32 h) { marks[h >> 3] = static_cast<u8>(marks[h >> 3] | 1u << (h & 7)); }
};
// Marks every handle the project (and stock's copies) holds.
void mark(Address project) {
    for (u32 i = 0; i < kMarkBytes; ++i) marks[i] = 0;
    Marker marker;
    each_reference(project, true, marker);
}

// The active pool, after making a staged one active once stock has made its project
// the active one.
void commit();
Pool& live() {
    if (pending && active_project() == pending) commit();
    return pools[active_pool];
}

// Garbage collection: only what the active project and stock's copies hold stays.
void collect() {
    mark(active_project());
    pools[active_pool].keep_only(marks);
}

// Carrying stock's copies over to a newly active project's pool, each old handle
// once under a new number (or cleared when it has no values).
struct Carry {
    Pool* from;
    Pool* to;
    u16 old_handles[2 * kSteps + 1], new_handles[2 * kSteps + 1];
    u32 count;
    void operator()(const Slot& slot, u32 h) {
        u32 i = 0;
        while (i < count && old_handles[i] != h) ++i;
        // A handle with no values (a dangling one, or a stock word on a track the step
        // clipboard or snapshot came from) is left as it is.
        if (!from->has(h)) return;
        if (i == count) {
            u32 next = to->unused(marks);
            if (next && !to->copy(next, *from, h)) next = 0;
            if (next) marks[next >> 3] = static_cast<u8>(marks[next >> 3] | 1u << (next & 7));
            if (count < 2 * kSteps + 1) {
                old_handles[count] = static_cast<u16>(h);
                new_handles[count++] = static_cast<u16>(next);
            }
            i = count - 1;
            if (old_handles[i] != h) {  // no room to remember: carry it alone
                if (next) half_at(slot.value) = static_cast<u16>(next);
                else clear_slot(slot);
                return;
            }
        }
        if (new_handles[i]) half_at(slot.value) = new_handles[i];
        else clear_slot(slot);
    }
};
Carry carry;

void commit() {
    Pool& from = pools[active_pool];
    Pool& to = pools[1 - active_pool];
    // The new project's own handles are taken; then each copy gets a free one.
    for (u32 i = 0; i < kMarkBytes; ++i) marks[i] = 0;
    Marker marker;
    each_reference(pending, false, marker);
    carry.from = &from;
    carry.to = &to;
    carry.count = 0;
    each_copy(carry);
    active_pool = 1 - active_pool;
    pending = 0;
    from.clear();
    for (u32 t = 0; t < kAllTracks; ++t) step_fresh[t] = false;
}

// The held step of a track's current pattern, as stock's editor finds it (8d5ad42c):
// the step key held (8d5b7e4c, 0..15) on the pattern's page (8d585fb4).
bool held_slot(u32 track, Slot& slot, u32& step) {
    if (!step_held() || track >= kAllTracks) return false;
    step = stock<u32()>(0x8d5b7e4c)() + 16 * stock<u32(u32)>(0x8d585fb4)(track);
    if (step >= kSteps) return false;
    slot = step_slot(stock<Address(u32)>(0x8d586014)(track), step);
    return true;
}

// Whether anything besides `self` holds `h`: another step, a clipboard, the snapshot.
struct Sharer {
    Address self;
    u32 handle;
    bool shared;
    void operator()(const Slot& slot, u32 h) {
        if (h == handle && slot.value != self) shared = true;
    }
};
bool shared(u32 h, const Slot& self) {
    Sharer sharer = {self.value, h, false};
    each_reference(active_project(), true, sharer);
    return sharer.shared;
}

// A fresh handle (held by no pool entry and nowhere in stock's data), collecting
// garbage once when none is left; 0 when the pool is full.
u32 fresh_handle(Pool& pool) {
    mark(active_project());
    if (const u32 h = pool.unused(marks)) return h;
    pool.keep_only(marks);
    return pool.unused(marks);
}

// After a lock edit: stock's own redraw request, as its editor makes after a write
// (8d5ad4d2, 8d5ad4d6).
void edited() { stock<void(Address)>(0x8d54b3b6)(stock<Address()>(0x8d54b438)()); }

// The section's text around its base64.
const char kOpen[] = "    \"open-xy.locks\": {\n      \"pool\": \"";
const char kClose[] = "\",\n      \"version\": 1\n    },\n";
u32 length_of(const char* s) {
    u32 n = 0;
    while (s[n]) ++n;
    return n;
}
u32 base64_bytes(u32 bytes) { return 4 * ((bytes + 2) / 3); }

// newlib's fwrite, as stock's project writer calls it (8d591304).
u32 file_write(const void* data, u32 size, u32 count, void* file) {
    return stock<u32(const void*, u32, u32, void*)>(0x8fed0780)(data, size, count, file);
}
const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
class Base64Out {
public:
    explicit Base64Out(void* file) : file_(file) {}
    void put(const char* s) { while (*s) text(*s++); }
    void byte(u8 b) {
        group_[held_++] = b;
        if (held_ == 3) emit();
    }
    void finish() {
        if (held_) emit();
        if (length_) file_write(buffer_, length_, 1, file_);
        length_ = 0;
    }

private:
    void text(char c) {
        if (length_ == sizeof buffer_) {
            file_write(buffer_, length_, 1, file_);
            length_ = 0;
        }
        buffer_[length_++] = c;
    }
    void emit() {
        const u32 v = group_[0] << 16 | (held_ > 1 ? group_[1] << 8 : 0) | (held_ > 2 ? group_[2] : 0);
        text(kBase64[v >> 18]);
        text(kBase64[v >> 12 & 63]);
        text(held_ > 1 ? kBase64[v >> 6 & 63] : '=');
        text(held_ > 2 ? kBase64[v & 63] : '=');
        held_ = 0;
    }
    void* file_;
    char buffer_[256];
    u32 length_ = 0, held_ = 0;
    u8 group_[3];
};
}

bool step_held() { return stock<u32()>(0x8d5b7e34)() != 0; }

u32 take_step(u32 track) {
    if (track >= kAllTracks || !step_fresh[track]) return 0;
    step_fresh[track] = false;
    return step_handle[track];
}

u32 handle_fields(u32 handle, u32 key, i32* values) { return handle ? live().fields(handle, key, values) : 0; }

bool held_value(u32 track, u32 key, u32 field, i32& value) {
    Slot slot;
    u32 step;
    return held_slot(track, slot, step) && live().get(slot.handle(), key, field, value);
}

bool held_set(u32 track, u32 key, u32 field, bool present, i32 value) {
    Slot slot;
    u32 step;
    if (!held_slot(track, slot, step) || field >= kFields) return false;
    Pool& pool = live();
    u32 h = slot.handle();
    if (h && !pool.has(h)) h = 0;  // no values (never expected): a fresh handle replaces it
    i32 old;
    const bool had = h && pool.own(h, key, field, old);
    if (present ? had && old == value : !had) return true;
    u32 next = h;
    // Copy-on-write: a handle another holder shares gets a copy for this step.
    if (h && shared(h, slot)) {
        next = fresh_handle(pool);
        if (!next || !pool.copy(next, pool, h)) return false;
    }
    if (!next) {
        if (!present) return true;
        next = fresh_handle(pool);
        if (!next) return false;
    }
    if (present) {
        if (!pool.set(next, key, field, value)) {
            // Room may come back from records nothing holds any longer.
            mark(active_project());
            marks[next >> 3] = static_cast<u8>(marks[next >> 3] | 1u << (next & 7));
            pool.keep_only(marks);
            if (!pool.set(next, key, field, value)) {
                if (next != h) pool.free(next);
                return false;
            }
        }
    } else {
        pool.erase(next, key, field);
    }
    // The step's handle word: the handle through stock's own writer, which sets the
    // mask and union bits; none left, the word cleared.
    if (pool.has(next)) {
        if (next != slot.handle()) stock<void(u32, u32, u32, u32)>(0x8d586f40)(track, step, kHandleWord, next);
    } else if (slot.handle()) {
        stock<void(u32, u32, u32, u32)>(0x8d586f40)(track, step, kHandleWord, 0);
        clear_slot(slot);
    }
    edited();
    return true;
}
}}}

using namespace opxy::platform;
using namespace opxy::platform::step_locks;
using opxy::u32;
using opxy::i32;

// 8d5939aa: stock's lock pass for the step of a note about to start (8d593980: base
// into live words, this pass, then note start). The pass writes the step's locks into
// the track's live words when its gate lets it, so the handle it applied is live word
// 37's (no slide) or, sliding, whatever differs from the stored word.
u32 sdk_lock_step(u32 project, u32 track, u32 position) {
    const u32 result = stock<u32(u32, u32, u32)>(0x8d5936c0)(project, track, position);
    if (track >= kAllTracks || !handle_client(track)) return result;
    live();
    const i32 step = (static_cast<i32>(position) + 240) / 480;  // as the pass finds it (8d593708..8d593714)
    u32 h = 0;
    if (step >= 0 && static_cast<u32>(step) < kSteps) {
        const Address p = stock<Address(u32)>(0x8d586014)(track);
        h = step_slot(p, static_cast<u32>(step)).handle();
        if (h) {
            const u32 now = word_at(kLive + track * kLiveBytes + kHandleWord * 4);
            const u32 stored = word_at(p + kBase + kHandleWord * 4);
            if ((now >> 16) != h && now == stored) h = 0;  // the gate held this step's locks back
        }
    }
    step_handle[track] = static_cast<opxy::u16>(h);
    step_fresh[track] = true;
    return result;
}

u32 sdk_step_locks_size(void* project, u32 budget) {
    saved_limit = 0;
    Pool& pool = live();
    // Only the active project's values are known; another buffer saves no section.
    const Address saved = reinterpret_cast<Address>(project);
    if (!saved || saved != active_project()) return 0;
    collect();
    const u32 fixed = length_of(kOpen) + length_of(kClose);
    if (budget <= fixed + 4) return 0;
    // Whole blocks, as many as the budget's base64 takes.
    u32 limit = (budget - fixed) / 4 * 3;
    if (limit > kSavedBytes) limit = kSavedBytes;
    const u32 bytes = pool.saved_bytes(limit);
    if (!bytes) return 0;
    saved_limit = limit;
    return fixed + base64_bytes(bytes);
}

void sdk_step_locks_write(void* file) {
    if (!saved_limit || !file) return;
    Base64Out out(file);
    out.put(kOpen);
    pools[active_pool].write(out, saved_limit);
    out.finish();
    out.put(kClose);
    out.finish();
    saved_limit = 0;
}

void sdk_step_locks_begin() {
    live();  // a project already made active takes its staged pool first
    pools[1 - active_pool].begin();
}

u32 sdk_step_locks_held_value(u32 track, u32 key, u32 field, i32* value) {
    return value && held_value(track, key, field, *value) ? 1 : 0;
}
u32 sdk_step_locks_held_set(u32 track, u32 key, u32 field, u32 present, i32 value) {
    return held_set(track, key, field, present != 0, value) ? 1 : 0;
}
// Once a GUI frame: the sequencer's step hook and its note start run in one call
// (8d593980), so a handle still fresh a frame later is one whose note start returned
// before the payload builder (8d59576a, 8d59582e, 8d595a02). It expires, so the next
// note played by hand on the track can't take it.
void sdk_step_locks_frame() {
    for (u32 t = 0; t < kAllTracks; ++t) step_fresh[t] = false;
}

void sdk_step_locks_byte(u32 byte) { pools[1 - active_pool].byte(static_cast<opxy::u8>(byte)); }

void sdk_step_locks_loaded(void* project, u32 valid) {
    Pool& staged = pools[1 - active_pool];
    // `valid`: the companion matches the project and its open-xy.locks is version 1.
    const bool whole = valid && staged.finish();
    if (!whole) staged.clear();
    const Address loaded = reinterpret_cast<Address>(project);
    if (!loaded) return;
    // Only a whole pool from a matching companion says which handles have values: each
    // the loaded project holds that it lacks is cleared, and each pattern's union
    // recomputed once. Without one the handles stay (lock_handles.hpp).
    struct Sweep {
        Pool* pool;
        Address dirty;
        void operator()(const Slot& slot, u32 h) {
            if (pool->has(h)) return;
            if (slot.pattern != dirty) union_of(dirty);
            clear_word(slot);
            dirty = slot.pattern;
        }
    } sweep = {&staged, 0};
    if (whole) {
        each_reference(loaded, false, sweep);
        union_of(sweep.dirty);
    }
    pending = loaded;
}
