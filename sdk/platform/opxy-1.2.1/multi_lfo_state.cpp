// The copies of stock's LFO on each role (multi_lfo.hpp). Linked in every
// build: the project companion reads and writes them, and a build without an
// extension that sets them simply keeps them off.
#include "multi_lfo.hpp"

namespace opxy { namespace platform { namespace multi_lfo {
namespace {
// GUI role.
Settings copies_gui[tracks][copies];
u32 dirty;  // bit track * copies + copy: to publish
// The last load's section. Stock loads every project into one buffer, so a
// load is told apart by its count, not by the project's address.
unsigned char loaded_bytes[save_bytes];
u32 loaded_count;
bool loading, loaded_whole;
u32 loads, adopted;

// Audio role.
Settings copies_audio[tracks][copies];
Settings staged;
u32 staged_track, staged_copy, staged_sequence, staged_mask;
bool staging;

const Settings kOff = {};
bool in_range(u32 track, u32 copy) { return track < tracks && copy < copies; }
u32 bit(u32 track, u32 copy) { return 1u << (track * copies + copy); }
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
void publish(u32 track, u32 copy) {
    static u32 sequence = 0;
    sequence = (sequence + 1) & 0x1fffffu;
    const Settings& s = copies_gui[track][copy];
    send(message_begin, static_cast<i32>(track | copy << 3 | sequence << 5));
    send(message_flags, static_cast<i32>((s.on ? 1u : 0u) | s.type << 1));
    for (u32 i = 0; i < words; ++i) send(message_word + i, s.words[i]);
    send(message_commit, static_cast<i32>(sequence));
}
}

bool valid_type(u32 type) { return type == 0 || type == 1 || type == 2 || type == 3 || type == 6; }

const Settings& get(u32 track, u32 copy) { return in_range(track, copy) ? copies_gui[track][copy] : kOff; }

bool set(u32 track, u32 copy, const Settings& settings) {
    if (!in_range(track, copy) || !valid_type(settings.type)) return false;
    Settings next = settings;
    next.on = settings.on ? 1 : 0;
    Settings& s = copies_gui[track][copy];
    if (same(s, next)) return true;
    s = next;
    dirty |= bit(track, copy);
    return true;
}

void clear() {
    for (u32 t = 0; t < tracks; ++t)
        for (u32 c = 0; c < copies; ++c) copies_gui[t][c] = kOff;
    dirty = (1u << (tracks * copies)) - 1;
}

void load_begin() {
    loading = true;
    loaded_count = 0;
}
void load_byte(u32 byte) {
    if (!loading) return;
    if (loaded_count < save_bytes) loaded_bytes[loaded_count] = static_cast<unsigned char>(byte);
    ++loaded_count;
}
void load_end(bool whole, bool loaded) {
    if (!loading) return;
    loading = false;
    if (!loaded) return;
    loaded_whole = whole && loaded_count == save_bytes;
    ++loads;
}

void tick() {
    if (adopted != loads) {
        adopted = loads;
        clear();
        if (loaded_whole) {
            const unsigned char* p = loaded_bytes;
            for (u32 t = 0; t < tracks; ++t)
                for (u32 c = 0; c < copies; ++c, p += 2 + 4 * words) {
                    Settings s = {};
                    s.on = p[0] ? 1 : 0;
                    s.type = p[1];
                    for (u32 i = 0; i < words; ++i) s.words[i] = static_cast<i32>(read_word(p + 2 + 4 * i));
                    // An unknown type (a newer section's) keeps that copy off.
                    if (valid_type(s.type)) copies_gui[t][c] = s;
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
    for (u32 t = 0; t < tracks; ++t)
        for (u32 c = 0; c < copies; ++c)
            if (!same(copies_gui[t][c], kOff)) return true;
    return false;
}

void save(unsigned char* out) {
    for (u32 t = 0; t < tracks; ++t)
        for (u32 c = 0; c < copies; ++c) {
            const Settings& s = copies_gui[t][c];
            *out++ = static_cast<unsigned char>(s.on ? 1 : 0);
            *out++ = static_cast<unsigned char>(s.type);
            for (u32 i = 0; i < words; ++i) {
                const u32 w = static_cast<u32>(s.words[i]);
                for (u32 b = 0; b < 4; ++b) *out++ = static_cast<unsigned char>(w >> (8 * b));
            }
        }
}

bool receive(u32 id, i32 value) {
    if (id < message_begin || id > message_commit) return false;
    const u32 word = static_cast<u32>(value);
    if (id == message_begin) {
        staged = kOff;
        staged_track = word & 7u;
        staged_copy = (word >> 3) & 3u;
        staged_sequence = word >> 5;
        staged_mask = 0;
        staging = value >= 0 && in_range(staged_track, staged_copy);
    } else if (!staging) {
    } else if (id == message_flags) {
        staged.on = word & 1u;
        staged.type = word >> 1;
        staged_mask |= 1u;
        if (word >> 1 > 6 || !valid_type(word >> 1)) staging = false;
    } else if (id < message_commit) {
        staged.words[id - message_word] = value;
        staged_mask |= 2u << (id - message_word);
    } else {
        if (value >= 0 && word == staged_sequence && staged_mask == 0x1ffu)
            copies_audio[staged_track][staged_copy] = staged;
        staging = false;
    }
    return true;
}

const Settings& audio(u32 track, u32 copy) { return in_range(track, copy) ? copies_audio[track][copy] : kOff; }
}}}
