// The four LFOs on each role (multi_lfo.hpp). Linked in every build: the
// project companion reads and writes them, and a build without an extension
// that shows them simply keeps LFO 1 shown and the copies off.
#include "multi_lfo.hpp"

namespace opxy { namespace platform { namespace multi_lfo {
namespace {
// GUI role.
Settings slots_gui[tracks][copies];
// The LFO stock's storage holds, and the LFO each slot holds.
unsigned char visible[tracks];
const unsigned char kHolders[copies] = {1, 2, 3};
unsigned char holders[tracks][copies] = {{1, 2, 3}, {1, 2, 3}, {1, 2, 3}, {1, 2, 3},
                                         {1, 2, 3}, {1, 2, 3}, {1, 2, 3}, {1, 2, 3}};
u32 dirty;  // bit track * copies + slot: to publish
u32 sequence;
// The last load's section. Stock loads every project into one buffer, so a
// load is told apart by its count, not by the project's address.
unsigned char loaded_bytes[save_bytes > v1_bytes ? save_bytes : v1_bytes];
u32 loaded_count, loaded_version;
bool loading;
u32 loads, adopted;

// Audio role: the copies the render runs, and each track's pending changes.
// The B3 receiver (the audio role's message task) adds them, the render takes
// them: one writer and one reader per ring.
Settings copies_audio[tracks][copies];
const u32 kPending = 8;
Change pending[tracks][kPending];
volatile u32 produced[tracks], consumed[tracks];
// The transaction being received.
Change staged;
u32 staged_track, staged_sequence, staged_mask, staged_full;
bool staging;

const Settings kOff = {};
bool in_range(u32 track, u32 slot) { return track < tracks && slot < copies; }
u32 bit(u32 track, u32 slot) { return 1u << (track * copies + slot); }
bool same(const Settings& a, const Settings& b) {
    if (a.on != b.on || a.type != b.type) return false;
    for (u32 i = 0; i < words; ++i)
        if (a.words[i] != b.words[i]) return false;
    return true;
}
u32 read_word(const unsigned char* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}
const unsigned char* read_settings(const unsigned char* p, Settings& s) {
    s.on = p[0] ? 1 : 0;
    s.type = p[1];
    for (u32 i = 0; i < words; ++i) s.words[i] = static_cast<i32>(read_word(p + 2 + 4 * i));
    return p + 2 + 4 * words;
}
u32 flags(const Settings& s) { return (s.on ? 1u : 0u) | s.type << 1; }
u32 next_sequence() { return sequence = (sequence + 1) & 0x1fffffu; }
void publish(u32 track, u32 slot) {
    const u32 n = next_sequence();
    const Settings& s = slots_gui[track][slot];
    send(message_begin, static_cast<i32>(track | slot << 3 | n << 5));
    send(message_flags, static_cast<i32>(flags(s)));
    for (u32 i = 0; i < words; ++i) send(message_word + i, s.words[i]);
    send(message_commit, static_cast<i32>(n));
}
void publish_swap(u32 track, u32 slot, const Settings& next, const Settings& stock) {
    const u32 n = next_sequence();
    send(swap_begin, static_cast<i32>(track | slot << 3 | n << 5));
    send(swap_slot_flags, static_cast<i32>(flags(next)));
    for (u32 i = 0; i < words; ++i) send(swap_slot_word + i, next.words[i]);
    send(swap_stock_flags, static_cast<i32>(flags(stock)));
    for (u32 i = 0; i < words; ++i) send(swap_stock_word + i, stock.words[i]);
    send(swap_commit, static_cast<i32>(n));
}
bool pristine(const Settings& s) { return same(s, kOff); }
void reset_track(u32 t) {
    for (u32 c = 0; c < copies; ++c) {
        slots_gui[t][c] = kOff;
        holders[t][c] = kHolders[c];
    }
    visible[t] = 0;
}
// A version-2 track: the LFO shown and the three slots' LFOs, each once.
bool valid_order(const unsigned char* p) {
    u32 seen = 1u << p[0];
    if (p[0] >= lfos) return false;
    for (u32 c = 0; c < copies; ++c) {
        const u32 lfo = p[1 + c * (3 + 4 * words)];
        if (lfo >= lfos || (seen & (1u << lfo))) return false;
        seen |= 1u << lfo;
    }
    return true;
}
}

bool valid_type(u32 type) { return type == 0 || type == 1 || type == 2 || type == 3 || type == 6; }

u32 shown(u32 track) { return track < tracks ? visible[track] : 0; }
u32 holder(u32 track, u32 slot) { return in_range(track, slot) ? holders[track][slot] : 0; }
const Settings& get(u32 track, u32 slot) { return in_range(track, slot) ? slots_gui[track][slot] : kOff; }

bool set(u32 track, u32 slot, const Settings& settings) {
    if (!in_range(track, slot) || !valid_type(settings.type)) return false;
    Settings next = settings;
    next.on = settings.on ? 1 : 0;
    Settings& s = slots_gui[track][slot];
    if (same(s, next)) return true;
    s = next;
    dirty |= bit(track, slot);
    return true;
}

bool show(u32 track, u32 lfo) {
    if (track >= tracks || lfo >= lfos) return false;
    if (visible[track] == lfo) return true;
    u32 slot = 0;
    while (slot < copies && holders[track][slot] != lfo) ++slot;
    if (slot == copies) return false;
    Settings leaving = {};
    read_stock(track, leaving);
    leaving.on = leaving.on ? 1 : 0;
    // A slot holds only stock's own types.
    if (!valid_type(leaving.type)) return false;
    Settings target = slots_gui[track][slot];
    if (pristine(target)) {
        target = leaving;
        target.on = 0;
    }
    // The swap goes first: stock's messages for the target follow it in the
    // same queue, and the audio role sets each value only once.
    publish_swap(track, slot, leaving, target);
    slots_gui[track][slot] = leaving;
    // The slot is published again as a copy on the next tick: the same value if
    // the swap arrived, and the slot's right LFO if a full queue lost it.
    dirty |= bit(track, slot);
    holders[track][slot] = static_cast<unsigned char>(visible[track]);
    visible[track] = static_cast<unsigned char>(lfo);
    load_stock(track, target);
    return true;
}

void clear() {
    for (u32 t = 0; t < tracks; ++t) reset_track(t);
    dirty = (1u << (tracks * copies)) - 1;
}

void load_begin() {
    loading = true;
    loaded_count = 0;
}
void load_byte(u32 byte) {
    if (!loading) return;
    if (loaded_count < sizeof(loaded_bytes)) loaded_bytes[loaded_count] = static_cast<unsigned char>(byte);
    ++loaded_count;
}
void load_end(u32 version, bool loaded) {
    if (!loading) return;
    loading = false;
    if (!loaded) return;
    loaded_version = (version == 1 && loaded_count == v1_bytes) || (version == 2 && loaded_count == save_bytes)
                         ? version : 0;
    ++loads;
}

void tick() {
    if (adopted != loads) {
        adopted = loads;
        clear();
        const unsigned char* p = loaded_bytes;
        if (loaded_version == 1) {
            // Saved by multi-lfo-001: LFO 1 in stock's storage, slot c holding LFO c + 2.
            for (u32 t = 0; t < tracks; ++t)
                for (u32 c = 0; c < copies; ++c) {
                    Settings s = {};
                    p = read_settings(p, s);
                    // An unknown type keeps that copy off.
                    if (valid_type(s.type)) slots_gui[t][c] = s;
                }
        } else if (loaded_version == 2) {
            for (u32 t = 0; t < tracks; ++t, p += track_bytes) {
                // A track whose LFO order does not add up keeps LFO 1 shown, its copies off.
                if (!valid_order(p)) continue;
                visible[t] = p[0];
                const unsigned char* q = p + 1;
                for (u32 c = 0; c < copies; ++c) {
                    holders[t][c] = q[0];
                    Settings s = {};
                    q = read_settings(q + 1, s);
                    if (valid_type(s.type)) slots_gui[t][c] = s;
                }
            }
        }
    }
    for (u32 sent = 0; dirty && sent < 2; ++sent) {
        u32 i = 0;
        while (!(dirty & (1u << i))) ++i;
        dirty &= ~(1u << i);
        publish(i / copies, i % copies);
    }
}

bool any() {
    for (u32 t = 0; t < tracks; ++t) {
        if (visible[t]) return true;
        for (u32 c = 0; c < copies; ++c)
            if (!same(slots_gui[t][c], kOff)) return true;
    }
    return false;
}

void save(unsigned char* out) {
    for (u32 t = 0; t < tracks; ++t) {
        *out++ = visible[t];
        for (u32 c = 0; c < copies; ++c) {
            const Settings& s = slots_gui[t][c];
            *out++ = holders[t][c];
            *out++ = static_cast<unsigned char>(s.on ? 1 : 0);
            *out++ = static_cast<unsigned char>(s.type);
            for (u32 i = 0; i < words; ++i) {
                const u32 w = static_cast<u32>(s.words[i]);
                for (u32 b = 0; b < 4; ++b) *out++ = static_cast<unsigned char>(w >> (8 * b));
            }
        }
    }
}

bool receive(u32 id, i32 value) {
    if (id < message_begin || id > swap_commit) return false;
    const u32 word = static_cast<u32>(value);
    if (id == message_begin || id == swap_begin) {
        staged = Change{};
        staged.swap = id == swap_begin;
        staged_track = word & 7u;
        staged.slot = (word >> 3) & 3u;
        staged_sequence = word >> 5;
        staged_mask = 0;
        // A copy: its flags and eight words. A swap: both settings' flags and words.
        staged_full = staged.swap ? 0x3ffffu : 0x1ffu;
        staging = value >= 0 && in_range(staged_track, staged.slot);
        return true;
    }
    if (!staging) return true;
    const bool swap_message = id > message_commit;
    if (swap_message != staged.swap) {
        staging = false;
        return true;
    }
    Settings& target = id >= swap_stock_flags && id < swap_commit ? staged.stock : staged.next;
    const u32 first = id >= swap_stock_flags ? swap_stock_flags : swap_message ? swap_slot_flags : message_flags;
    const u32 commit = swap_message ? swap_commit : message_commit;
    // Mask bits: the slot's flags and words 0-8, stock's 9-17.
    const u32 base = id >= swap_stock_flags ? 9 : 0;
    if (id == commit) {
        if (value >= 0 && word == staged_sequence && staged_mask == staged_full) {
            const u32 at = produced[staged_track];
            // A full ring (the render has not run for 8 changes) drops the newest.
            if (at - consumed[staged_track] < kPending) {
                pending[staged_track][at % kPending] = staged;
                produced[staged_track] = at + 1;
            }
        }
        staging = false;
    } else if (id == first) {
        target.on = word & 1u;
        target.type = word >> 1;
        staged_mask |= 1u << base;
        if (!valid_type(word >> 1)) staging = false;
    } else {
        target.words[id - first - 1] = value;
        staged_mask |= 1u << (base + id - first);
    }
    return true;
}

bool take(u32 track, Change& out) {
    if (track >= tracks) return false;
    const u32 at = consumed[track];
    if (at == produced[track]) return false;
    out = pending[track][at % kPending];
    copies_audio[track][out.slot] = out.next;
    consumed[track] = at + 1;
    return true;
}

const Settings& audio(u32 track, u32 slot) { return in_range(track, slot) ? copies_audio[track][slot] : kOff; }
}}}
