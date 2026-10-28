// SDK ABI fixture: drive the generated synth voice adapter as stock does, with
// a machine of two voices and every stock voice index 0..255.
#include "voice.cpp"
#include <cstring>

int initialized[257], destroyed[257], live, foreign;
// Every encoder shows an alternate. Record names must still identify raw slots.
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }

namespace {
typedef int (*Method)(void*);
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
void* handle[256];
int call(unsigned index, unsigned entry) {
    const void* const* table = *static_cast<const void* const* const*>(handle[index]);
    return reinterpret_cast<Method>(const_cast<void*>(table[entry]))(handle[index]);
}
void block(unsigned index, opxy::i32* output) {
    const void* const* table = *static_cast<const void* const* const*>(handle[index]);
    opxy::i32 parameters[8] = {};
    for (unsigned i = 0; i < 64; ++i) output[i] = -7;
    reinterpret_cast<Render>(const_cast<void*>(table[2]))(handle[index], output, parameters);
}
// The owner a voice index renders as, 0 for silence; -1 when output is mixed.
opxy::i32 rendered(unsigned index) {
    opxy::i32 output[64];
    block(index, output);
    for (opxy::i32 word : output)
        if (word != output[0]) return -1;
    return output[0] / 65536;
}
// A note that fades from its level to zero over 16 blocks, smoothly: no sample
// step larger than the raised cosine's steepest (pi / 2048 of the level).
bool faded(unsigned index) {
    const opxy::i32 level = static_cast<opxy::i32>((index + 1) << 16);
    opxy::i32 last = level;
    for (unsigned b = 0; b < 16; ++b) {
        opxy::i32 output[64];
        block(index, output);
        for (opxy::i32 word : output) {
            if (word > last || last - word > level / 640) return false;
            last = word;
        }
    }
    return last == 0 && rendered(index) == 0;
}
void start(unsigned index) { call(index, 4); }
void end(unsigned index) { call(index, 0); }
// Lifecycle calls pair: each owner is initialized at most once more than it is
// destroyed, and no callback runs for an owner its voice does not hold.
bool paired() {
    int count = 0;
    for (unsigned i = 0; i < 257; ++i) {
        if (initialized[i] - destroyed[i] != 0 && initialized[i] - destroyed[i] != 1) return false;
        count += initialized[i] - destroyed[i];
    }
    unsigned sounding = 0;
    for (const Slot& slot : voices) sounding += slot.owner && !slot.fading;
    return !foreign && count == live && live >= 0 && live <= 6 && sounding <= 2;
}
}

int main() {
    const char* labels[] = {"shape", "width", "level", "motion", "extra 0", "extra 1", "extra 2", "extra 3"};
    for (unsigned i = 0; i < 8; ++i)
        if (std::strcmp(probe_parameter_name(i), labels[i])) return 11;
    if (*probe_parameter_name(8) || *probe_parameter_name(0xffffffffu)) return 12;
    // Every stock index gets an object; none past the last.
    for (unsigned i = 0; i < 256; ++i)
        if (!(handle[i] = probe_factory(i))) return 1;
    if (probe_factory(256)) return 2;
    // Nothing sounds before a note.
    if (rendered(200) != 0) return 3;
    // Two notes take the two voices.
    start(0); start(1);
    if (rendered(0) != 1 || rendered(1) != 2) return 4;
    // A third note starts at once in a spare voice; the note that started first
    // fades out over 16 blocks, and only then is its voice destroyed.
    start(200);
    if (initialized[201] != 1 || rendered(200) != 201 || rendered(1) != 2 || destroyed[1] || !paired()) return 5;
    if (!faded(0) || destroyed[1] != 1 || rendered(200) != 201 || !paired()) return 13;
    // A note that still holds its voice keeps it: no new initialize.
    unsigned before = 0;
    for (const Slot& slot : voices) before += slot.voice.initialized;
    start(1);
    unsigned after = 0;
    for (const Slot& slot : voices) after += slot.voice.initialized;
    if (after != before || rendered(1) != 2) return 6;
    // Ending a note frees its voice for the next one without taking another's.
    end(200);
    if (destroyed[201] != 1 || rendered(200) != 0) return 7;
    start(7);
    if (rendered(7) != 8 || rendered(1) != 2 || destroyed[2] != 0) return 8;
    // Ending a note whose voice faded out destroys nothing again.
    end(0);
    if (destroyed[1] != 1) return 9;
    // A fading note that ends is destroyed at once.
    start(9);
    if (rendered(1) != -1) return 14;
    end(1);
    if (destroyed[2] != 1 || rendered(1) != 0 || rendered(9) != 10 || rendered(7) != 8 || !paired()) return 15;
    // A note that starts again while it fades takes a new voice and sounds whole.
    start(11);
    if (rendered(7) != -1) return 16;
    start(7);
    if (destroyed[8] != 1 || initialized[8] != 2 || rendered(7) != 8 || rendered(9) != -1 || !paired()) return 17;
    // More taken voices within one fade than spares: the fading voice nearest
    // its end goes to the new note at once.
    for (unsigned i = 30; i < 36; ++i) start(i);
    if (rendered(35) != 36 || rendered(34) != 35 || !paired()) return 18;
    for (unsigned i = 0; i < 256; ++i) end(i);
    if (live || !paired()) return 19;
    // A long run of notes, ends and blocks on every index never fails: a note
    // sounds in its first block, and the lifecycle stays paired.
    unsigned seed = 1;
    for (unsigned n = 0; n < 20000; ++n) {
        seed = seed * 1103515245u + 12345u;
        const unsigned index = (seed >> 8) % 256, action = (seed >> 20) % 4;
        if (action == 0) {
            end(index);
        } else if (action == 1) {
            const opxy::i32 sound = rendered(index);
            if (sound != 0 && sound != -1 && sound != static_cast<opxy::i32>(index + 1)) return 20;
        } else {
            start(index);
            if (rendered(index) != static_cast<opxy::i32>(index + 1)) return 21;
        }
        if (!paired()) return 23;
    }
    for (unsigned i = 0; i < 256; ++i) end(i);
    if (live || !paired()) return 24;
    return 0;
}
