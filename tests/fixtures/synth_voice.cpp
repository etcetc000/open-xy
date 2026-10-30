// SDK ABI fixture: drive the generated synth voice adapter as stock does, with
// a machine of two voices (three with the spare) and every stock voice index
// 0..255. Stock renders each sounding index once per 64-sample block, asks for
// an index's object before each note there, and ends a note by no longer
// rendering it. The adapter's clock is the CPU cycle count, here simulated.
#include "voice.cpp"
#include <cstring>
#include <initializer_list>

int destroyed[257];
// Every encoder shows an alternate. Record names must still identify raw slots.
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }
// A 64-sample block at 400 MHz, as stock budgets it.
const unsigned long long kBlockCycles = 580352;
unsigned long long cycles = 1ull << 40;
opxy::u32 sdk_voice_ticks() { return static_cast<opxy::u32>(cycles >> 17); }

namespace {
typedef int (*Method)(void*);
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
void* handle[256];
int call(unsigned index, unsigned entry) {
    const void* const* table = *static_cast<const void* const* const*>(handle[index]);
    return reinterpret_cast<Method>(const_cast<void*>(table[entry]))(handle[index]);
}
// The owner a voice index renders as, 0 for silence; -1 when output changes
// within the block, as in a fade.
opxy::i32 rendered(unsigned index) {
    const void* const* table = *static_cast<const void* const* const*>(handle[index]);
    opxy::i32 output[64], parameters[8] = {};
    for (opxy::i32& word : output) word = -7;
    reinterpret_cast<Render>(const_cast<void*>(table[2]))(handle[index], output, parameters);
    for (opxy::i32 word : output)
        if (word != output[0]) return -1;
    return output[0] >> 16;
}
// One stock block: each listed index renders once, as the given owner (0:
// silence, -1: a fade); then a block's worth of cycles passes.
struct Expect { unsigned index; opxy::i32 owner; };
bool block(std::initializer_list<Expect> expected) {
    bool ok = true;
    for (const Expect& e : expected) ok = rendered(e.index) == e.owner && ok;
    cycles += kBlockCycles;
    return ok;
}
// Blocks in which the listed indices sound as themselves.
bool sound(std::initializer_list<unsigned> indices, unsigned count) {
    bool ok = true;
    for (unsigned b = 0; b < count; ++b) {
        for (unsigned index : indices) ok = rendered(index) == static_cast<opxy::i32>(index + 1) && ok;
        cycles += kBlockCycles;
    }
    return ok;
}
// A note as stock starts it: a fresh object for the index, then start.
void note(unsigned index) {
    handle[index] = probe_factory(index);
    call(index, 4);
}
unsigned initialized() {
    unsigned total = 0;
    for (const Slot& slot : voices) total += slot.voice.initialized;
    return total;
}
// A small random number generator for the long runs.
unsigned seed = 1;
unsigned random(unsigned range) {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 8) % range;
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
    if (!block({{200, 0}})) return 3;
    // Two notes take the two voices; a third, while both sound, the spare.
    note(0); note(1);
    if (!block({{0, 1}, {1, 2}})) return 4;
    note(200);
    if (destroyed[1] || !block({{0, 1}, {1, 2}, {200, 201}})) return 5;
    // A note that still holds its voice keeps it: no new initialize.
    const unsigned before = initialized();
    call(1, 4);
    if (initialized() != before || !block({{0, 1}, {1, 2}, {200, 201}})) return 6;
    // A fourth note while all sound takes a voice past its first 8 blocks, the
    // one whose note started first: that note fades out over its next block and
    // is then destroyed, and the new note sounds in the voice from then on.
    if (!sound({0, 1, 200}, 10)) return 7;
    note(5);
    if (destroyed[1] || !block({{0, -1}, {1, 2}, {200, 201}, {5, 6}}) || destroyed[1] != 1) return 8;
    if (!block({{0, 0}, {1, 2}, {200, 201}, {5, 6}})) return 13;
    // Should the new note render first, it waits; the old one still fades.
    if (!sound({1, 200, 5}, 10)) return 14;
    note(9);  // takes 200's voice: 1's restart counts as a newer note
    if (!block({{9, 0}, {200, -1}, {1, 2}, {5, 6}}) || destroyed[201] != 1) return 15;
    if (!block({{9, 10}, {200, 0}, {1, 2}, {5, 6}})) return 16;
    // A young note is spared while an older one sounds: 9 is new, 1 is not.
    note(10);
    if (!block({{9, 10}, {1, -1}, {5, 6}, {10, 11}}) || destroyed[2] != 1) return 17;
    // Two notes in one block take two different voices.
    if (!sound({5, 9, 10}, 10)) return 18;
    note(11); note(12);
    if (!block({{5, -1}, {9, -1}, {10, 11}, {11, 12}, {12, 13}}) || destroyed[6] != 1 || destroyed[10] != 1) return 19;
    // A taken note stock stops rendering needs no fade: the waiting note starts
    // once the old one has missed two blocks of renders.
    if (!sound({10, 11, 12}, 10)) return 20;
    note(13);  // takes 10's voice, which stock then stops rendering
    if (!block({{11, 12}, {12, 13}, {13, 0}}) || !block({{11, 12}, {12, 13}, {13, 0}}) || destroyed[11]) return 21;
    if (!block({{11, 12}, {12, 13}, {13, 14}}) || destroyed[11] != 1) return 22;
    // A note restarted while its voice fades for another hands the voice over
    // and takes the next one in turn.
    if (!sound({11, 12, 13}, 10)) return 23;
    note(14);  // takes 11's voice
    call(11, 4);  // 11 restarts without a fresh object: 14 starts, 11 takes 12's voice
    if (!block({{11, 0}, {12, -1}, {13, 14}, {14, 15}}) || destroyed[12] != 1 || destroyed[13] != 1) return 24;
    if (!block({{11, 12}, {13, 14}, {14, 15}})) return 25;
    // A fresh object for an index ends the note its voice held.
    handle[13] = probe_factory(13);
    if (destroyed[14] != 1 || !block({{13, 0}, {11, 12}, {14, 15}})) return 26;
    // Ending a note through the method table still frees its voice, once.
    call(14, 0);
    if (destroyed[15] != 1 || !block({{14, 0}})) return 9;
    call(14, 0);
    if (destroyed[15] != 1) return 27;
    // After a long pause, voices whose notes ended before it are free: two new
    // notes sound in their first block, though nothing rendered in between.
    note(20); note(21);
    if (!sound({11, 20, 21}, 1)) return 30;
    cycles += 44100 / 64 * kBlockCycles;  // a second without a render
    note(15); note(16);
    if (!block({{15, 16}, {16, 17}}) || destroyed[21] != 1 || destroyed[22] != 1) return 31;
    for (unsigned i = 0; i < 256; ++i) call(i, 0);
    cycles += 4 * kBlockCycles;
    // A whole chord that ends at once frees its voices for the next chord a few
    // milliseconds later, though no block was counted in between.
    note(50); note(51); note(52);
    if (!sound({50, 51, 52}, 10)) return 41;
    cycles += 25 * 400000;  // 25 ms with nothing of the machine rendering
    note(53); note(54); note(55);
    if (destroyed[51] != 1 || destroyed[52] != 1 || destroyed[53] != 1) return 43;  // all ended voices released
    if (!block({{53, 54}, {54, 55}, {55, 56}})) return 42;
    // Blocks counted far apart, as when stock renders several per interrupt,
    // never make a sounding note look ended: a new note waits for a fade.
    if (!sound({53, 54, 55}, 10)) return 44;
    cycles += 10 * 400000;  // 10 ms since the last counted block
    note(56);
    if (destroyed[54] || destroyed[55] || destroyed[56]) return 45;
    if (!block({{53, -1}, {54, 55}, {55, 56}, {56, 57}}) || destroyed[54] != 1) return 46;
    for (unsigned i = 0; i < 256; ++i) call(i, 0);
    cycles += 4 * kBlockCycles;
    // A note stock stops rendering is destroyed within a few blocks while the
    // machine renders others, with no new note to reclaim its voice.
    note(30); note(31); note(32);
    if (!sound({30, 31, 32}, 10)) return 32;
    if (!sound({30, 32}, 4) || destroyed[32] != 1) return 33;
    // A waiting note that ends before its voice frees is forgotten: once the
    // note it waited on ends too, the voice is released and free again.
    note(33);
    if (!sound({30, 32, 33}, 10)) return 34;
    note(34);  // waits for 30's voice; then stock stops rendering both
    if (!sound({32, 33}, 4) || destroyed[31] != 1 || destroyed[35]) return 35;
    note(36);
    if (!block({{32, 33}, {33, 34}, {36, 37}})) return 36;
    // A note displaced from its wait takes a voice once one is free.
    if (!sound({32, 33, 36}, 10)) return 37;
    note(40); note(41); note(42); note(43);  // three wait; 43 displaces one
    if (!block({{32, -1}, {33, -1}, {36, -1}, {40, 0}, {41, 42}, {42, 43}, {43, 44}})) return 38;
    // 41, 42 and 43 hold the voices; stock ends 41, and 40 takes its voice.
    unsigned voiced = 0;
    for (unsigned b = 0; b < 8 && voiced != 3; ++b) {
        voiced = 0;
        for (unsigned index : {40u, 42u, 43u}) {
            const opxy::i32 owner = rendered(index);
            if (owner > 0 && owner != static_cast<opxy::i32>(index + 1)) return 39;
            voiced += owner > 0;
        }
        cycles += kBlockCycles;
    }
    if (voiced != 3 || destroyed[42] != 1) return 40;
    for (unsigned i = 0; i < 256; ++i) call(i, 0);
    cycles += 4 * kBlockCycles;
    // A long run as stock plays with as many notes as the machine has voices:
    // notes end by going unrendered, a new note takes a free stock index, and
    // when all are busy stock takes one note and restarts on its index (as the
    // OP-XY does once a track reaches its voice limit). No sounding note ever
    // loses its voice, and a new note sounds by its second block (a voice whose
    // note ended in the last block still looks sounding in this one).
    unsigned sounding[4] = {}, count = 0;
    for (unsigned n = 0; n < 20000; ++n) {
        if (count && random(3) == 0) {
            const unsigned gone = random(count);
            sounding[gone] = sounding[--count];
        }
        unsigned index = random(256);
        while ((count > 0 && sounding[0] == index) || (count > 1 && sounding[1] == index)) index = (index + 1) % 256;
        if (count == 2) index = sounding[random(2)];
        else sounding[count++] = index;
        note(index);
        for (unsigned b = 0; b < 4; ++b) {
            for (unsigned i = 0; i < count; ++i) {
                const opxy::i32 owner = rendered(sounding[i]);
                if (owner != static_cast<opxy::i32>(sounding[i] + 1) && (b > 0 || sounding[i] != index || owner))
                    return 10;
            }
            cycles += kBlockCycles;
        }
    }
    for (unsigned i = 0; i < 256; ++i) call(i, 0);
    cycles += 4 * kBlockCycles;
    // A long run with more notes than voices (the machine on several tracks),
    // in a varying render order: the adapter takes sounding voices, and a note
    // that loses its voice always renders one fading block first, never a cut
    // from full level to silence.
    count = 0;
    opxy::i32 last[4] = {};
    for (unsigned n = 0; n < 20000; ++n) {
        if (count && random(4) == 0) {
            const unsigned gone = random(count);
            --count;
            sounding[gone] = sounding[count];
            last[gone] = last[count];
        }
        if (count < 4 && random(2) == 0) {
            unsigned index = random(256);
            for (bool clash = true; clash;) {
                clash = false;
                for (unsigned i = 0; i < count; ++i) clash = clash || sounding[i] == index;
                if (clash) index = (index + 1) % 256;
            }
            sounding[count] = index;
            last[count++] = 0;
            note(index);
        }
        const unsigned first = count ? random(count) : 0;
        for (unsigned k = 0; k < count; ++k) {
            const unsigned at = (first + k) % count;
            const opxy::i32 owner = rendered(sounding[at]);
            if (owner > 0 && owner != static_cast<opxy::i32>(sounding[at] + 1)) return 28;
            if (owner == 0 && last[at] > 0) return 29;  // cut without a fade
            last[at] = owner;
        }
        cycles += kBlockCycles;
    }
    return 0;
}
