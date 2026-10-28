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
// A block that fades owner's level to zero, no step larger than a 64th of it.
bool faded(unsigned index) {
    opxy::i32 output[64];
    block(index, output);
    const opxy::i32 level = static_cast<opxy::i32>((index + 1) << 16);
    opxy::i32 last = level;
    for (opxy::i32 word : output) {
        if (word > last || last - word > level / 64 + 1) return false;
        last = word;
    }
    return last == 0;
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
    return !foreign && count == live && live >= 0 && live <= 2;
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
    // A third note takes the voice of the note that started first. That note
    // renders one more block, faded to zero, and then the voice goes over.
    start(200);
    if (destroyed[1] || initialized[201] || !faded(0)) return 5;
    if (destroyed[1] != 1 || rendered(0) != 0 || rendered(200) != 201 || rendered(1) != 2 || !paired()) return 13;
    // A note that still holds its voice keeps it: no new initialize.
    const unsigned before = voices[0].voice.initialized + voices[1].voice.initialized;
    start(1);
    if (voices[0].voice.initialized + voices[1].voice.initialized != before || rendered(1) != 2) return 6;
    // Ending a note frees its voice for the next one without taking another's.
    end(200);
    if (destroyed[201] != 1 || rendered(200) != 0) return 7;
    start(7);
    if (rendered(7) != 8 || rendered(1) != 2 || destroyed[2] != 0) return 8;
    // Ending a note whose voice was taken destroys nothing again.
    end(0);
    if (destroyed[1] != 1) return 9;
    // A new note that renders before the old one starts one block late.
    start(9);
    if (rendered(9) != 0 || !faded(1) || rendered(9) != 10 || !paired()) return 14;
    // If the old note does not render, the new one takes the voice on its second block.
    start(11);
    if (rendered(11) != 0 || rendered(11) != 12 || destroyed[8] != 1 || rendered(7) != 0 || !paired()) return 15;
    // An old note that ends during the hand-over gives the voice at once.
    start(13);
    end(9);
    if (destroyed[10] != 1 || rendered(13) != 14 || !paired()) return 16;
    // A waiting note that ends leaves the voice to its old note, unfaded.
    start(15);
    end(15);
    if (initialized[16] || rendered(11) != 12 || rendered(15) != 0 || !paired()) return 17;
    // An old note that starts again during its hand-over gives the voice at
    // once and takes another as a new note.
    start(17);
    start(11);
    if (rendered(17) != 18 || !faded(13) || rendered(11) != 12 || rendered(13) != 0 || !paired()) return 18;
    // When every voice is already going to another note, the oldest goes at once.
    start(20); start(21); start(22);
    if (rendered(22) != 23 || !paired()) return 19;
    // A long run of notes, ends and blocks on every index never fails: a note
    // sounds by its second block, and the lifecycle stays paired.
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
            const opxy::i32 first = rendered(index);
            if (first != 0 && first != static_cast<opxy::i32>(index + 1)) return 21;
            if (!first && rendered(index) != static_cast<opxy::i32>(index + 1)) return 22;
        }
        if (!paired()) return 23;
    }
    for (unsigned i = 0; i < 256; ++i) end(i);
    if (live || !paired()) return 24;
    return 0;
}
