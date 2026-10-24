#include <opxy/player.hpp>

// Chord player. On the OP-XY's two-octave F-to-E keyboard, the lower octave
// plays chord roots and the upper octave holds chord modifiers, which never
// sound, whatever octave the keyboard is shifted to:
//   white F G A B C D E  chord type  dim aug min maj sus dom half
//   black F# G# A# C# D#  extension  6 m7 M7 9 11
// A root plays the most recently held chord type with every held extension,
// or itself alone when no type is held. Notes outside the keyboard's two
// octaves (from the sequencer) are roots.
namespace chords {
using opxy::u8;
using opxy::u32;
using opxy::i32;
using opxy::player::Controls;
using opxy::player::Display;
using opxy::player::Plan;

const u32 kF = 5;  // pitch class of the keyboard's first key
enum Type : u8 { kNone, kDim, kAug, kMin, kMaj, kSus, kDom, kHalf, kTypes };
const u32 kExtensionCount = 5;

struct State {
    u8 held;        // bit per chord type
    u8 last;        // most recently pressed held type
    u8 extensions;  // bit per extension
    u8 descending;  // alternate direction: the next chord's order
    u32 seed;       // random direction
    u8 root;        // the last root played, plus one; 0 before the first
    u8 notes[12];   // the note holding each modifier key, plus one; 0 when up
    u8 unused[3];
};

namespace {
// Semitones above the root. Dominant and half-diminished are seventh chords;
// the others are triads. Extensions: 6, m7, M7, 9 and 11.
const u8 kChordTones[kTypes] = {0, 3, 3, 3, 3, 3, 4, 4};
const u8 kChords[kTypes][4] = {{0}, {0, 3, 6}, {0, 4, 8}, {0, 3, 7}, {0, 4, 7}, {0, 5, 7}, {0, 4, 7, 10}, {0, 3, 6, 10}};
const u8 kExtensions[kExtensionCount] = {9, 10, 11, 14, 17};
const i32 kVoicingSteps = 6;  // the voicing encoder spans -6..+6 inversion steps

// Key of the modifier octave (0 = F) -> chord type (1..7) or extension (kTypes + 0..4).
const u8 kModifier[12] = {kDim, kTypes, kAug, kTypes + 1, kMin, kTypes + 2, kMaj, kSus, kTypes + 3, kDom, kTypes + 4, kHalf};
// The modifier octave key of each chord type and extension, for the page.
const u8 kTypeKey[kTypes] = {0, 0, 2, 4, 6, 7, 9, 11};
const u8 kExtensionKey[kExtensionCount] = {1, 3, 5, 8, 10};

// The modifier key (0..11 from F) a note plays, or -1 for a root.
int modifier_key(u32 note, u32 lowest_key) {
    return note >= lowest_key + 12 && note < lowest_key + 24 ? static_cast<int>(note - lowest_key - 12) : -1;
}

u32 steps(i32 value, u32 count) {  // Q31 0..1 -> 0..count-1, count <= 65536
    const u32 v = value < 0 ? 0u : static_cast<u32>(value) >> 16;
    const u32 step = (v * count) >> 15;
    return step < count ? step : count - 1;
}

void add(i32* notes, u32& count, i32 pitch) {
    for (u32 i = 0; i < count; ++i)
        if (notes[i] == pitch) return;
    if (count < opxy::player::max_tones) notes[count++] = pitch;
}

void sort(i32* notes, u32 count) {
    for (u32 i = 1; i < count; ++i)
        for (u32 j = i; j > 0 && notes[j - 1] > notes[j]; --j) {
            const i32 t = notes[j];
            notes[j] = notes[j - 1];
            notes[j - 1] = t;
        }
}

// Voicing: each step up moves the lowest note an octave up, each step down
// the highest note an octave down.
void voice(i32* notes, u32 count, i32 voicing) {
    for (; voicing > 0; --voicing) { notes[0] += 12; sort(notes, count); }
    for (; voicing < 0; ++voicing) { notes[count - 1] -= 12; sort(notes, count); }
}

// Bounded text: appends while there is room, always terminated.
struct Text {
    char* out;
    u32 size, length;
    Text& operator<<(const char* s) {
        while (*s && length + 1 < size) out[length++] = *s++;
        out[length] = 0;
        return *this;
    }
    Text& operator<<(i32 n) {
        char digits[12];
        u32 count = 0, v = n < 0 ? static_cast<u32>(-n) : static_cast<u32>(n);
        do { digits[count++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        if (n < 0) *this << "-";
        while (count) { const char c[2] = {digits[--count], 0}; *this << c; }
        return *this;
    }
};
template <u32 N> Text text(char (&out)[N]) { out[0] = 0; return Text{out, N, 0}; }

const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// "C minor 7": the root, then the chord in words. Dominant and half-diminished
// read as major and diminished with a minor seventh.
void name(Text& out, u32 root, u32 type, u32 extensions) {
    out << kNoteNames[root % 12];
    if (type == kNone) return;
    bool m7 = extensions & 2, M7 = extensions & 4;
    const bool e6 = extensions & 1, e9 = extensions & 8, e11 = extensions & 16;
    u32 q = type;
    if (type == kDom) { q = kMaj; m7 = true; }
    if (type == kHalf) { q = kDim; m7 = true; }
    const bool seventh = m7 || M7;
    const char* n = e11 ? "11" : e9 ? "9" : "7";
    static const char* const quality[] = {"", "diminished", "augmented", "minor", "major", "sus4"};
    out << " ";
    if (m7) {
        static const char* const words[] = {"", "half-dim ", "augmented ", "minor ", "dominant ", ""};
        out << words[q] << n;
        if (q == kSus) out << " sus4";
    } else if (M7) {
        static const char* const words[] = {"", "dim major ", "aug major ", "minor major ", "major ", "major "};
        out << words[q] << n;
        if (q == kSus) out << " sus4";
    } else if (e6 && q == kDim) {
        out << "diminished 7";
    } else {
        out << quality[q];
    }
    if (m7 && M7) out << " + maj7";
    if (e6 && (seventh || q != kDim)) out << (seventh ? " 13" : " 6");
    if (!seventh && e9) out << " add 9";
    if (!seventh && e11) out << " add 11";
}
}

// A modifier is released by the note that pressed it, so an octave shift
// while it is held cannot leave it stuck.
bool release(State& state, u32 note) {
    int key = -1;
    for (int k = 0; k < 12; ++k)
        if (state.notes[k] == note + 1) key = k;
    if (key < 0) return false;
    state.notes[key] = 0;
    const int m = kModifier[key];
    if (m >= kDim && m < kTypes) {
        state.held &= static_cast<u8>(~(1u << m));
        if (state.last == m) {
            state.last = kNone;
            for (u8 t = kDim; t < kTypes; ++t)
                if (state.held & (1u << t)) state.last = t;
        }
    } else {
        state.extensions &= static_cast<u8>(~(1u << (m - kTypes)));
    }
    return true;
}

bool press(State& state, const Controls& controls, u32 note, u32 velocity, Plan& plan) {
    plan.count = 0;
    const int key = modifier_key(note, controls.lowest_key);
    if (key >= 0) state.notes[key] = static_cast<u8>(note % 128 + 1);
    const int m = key >= 0 ? kModifier[key] : -1;
    if (m >= kDim && m < kTypes) {
        state.held |= static_cast<u8>(1u << m);
        state.last = static_cast<u8>(m);
        return true;
    }
    if (m >= kTypes) {
        state.extensions |= static_cast<u8>(1u << (m - kTypes));
        return true;
    }
    state.root = static_cast<u8>(note % 128 + 1);

    i32 notes[opxy::player::max_tones];
    u32 count = 0;
    if (state.last == kNone) {
        notes[count++] = static_cast<i32>(note);
    } else {
        for (u32 i = 0; i < kChordTones[state.last]; ++i) notes[count++] = static_cast<i32>(note + kChords[state.last][i]);
        // Seventh chords hold at most 4 + 4 distinct tones: their m7 is already present.
        for (u32 i = 0; i < kExtensionCount; ++i)
            if (state.extensions & (1u << i)) add(notes, count, static_cast<i32>(note + kExtensions[i]));
    }
    sort(notes, count);
    voice(notes, count, static_cast<i32>(steps(controls.value[2], 2 * kVoicingSteps + 1)) - kVoicingSteps);
    // Spread opens the voicing: every second note rises an octave.
    if (steps(controls.value[3], 2)) {
        for (u32 i = 1; i < count; i += 2) notes[i] += 12;
        sort(notes, count);
    }

    // Direction: up, down, alternating per chord, or a random rotation.
    const u32 direction = steps(controls.value[1], 4);
    bool down = direction == 1;
    if (direction == 2) {
        down = state.descending != 0;
        state.descending = !state.descending;
    }
    u32 rotate = 0;
    if (direction == 3) {
        state.seed = state.seed * 1664525u + 1013904223u;
        rotate = (state.seed >> 16) % count;
    }
    const u32 strum = steps(controls.value[0], 64);
    for (u32 i = 0; i < count; ++i) {
        const u32 k = (i + rotate) % count;
        const i32 pitch = notes[down ? count - 1 - k : k];
        if (pitch < 0 || pitch > 127) continue;
        opxy::player::Tone& tone = plan.tones[plan.count];
        tone.pitch = static_cast<u8>(pitch);
        tone.velocity = static_cast<u8>(velocity);
        tone.delay = static_cast<opxy::u16>(plan.count * strum);
        ++plan.count;
    }
    return true;
}

// The page: dots on the selected root (lower octave) and on every held
// chord type and extension (upper octave), the chord in words, and the four
// encoder values.
void display(const State& state, const Controls& controls, Display& display) {
    display.lit = 0;
    if (state.root) display.lit |= 1u << ((state.root - 1 + 12 - kF) % 12);
    if (state.last != kNone) display.lit |= 1u << (12 + kTypeKey[state.last]);
    for (u32 i = 0; i < kExtensionCount; ++i)
        if (state.extensions & (1u << i)) display.lit |= 1u << (12 + kExtensionKey[i]);
    Text line = text(display.text);
    if (state.root) name(line, state.root - 1, state.last, state.extensions);
    Text strum = text(display.values[0]);
    strum << static_cast<i32>(steps(controls.value[0], 64));
    static const char* const directions[4] = {"up", "dn", "alt", "rnd"};
    Text direction = text(display.values[1]);
    direction << directions[steps(controls.value[1], 4)];
    const i32 voicing = static_cast<i32>(steps(controls.value[2], 2 * kVoicingSteps + 1)) - kVoicingSteps;
    Text voice_text = text(display.values[2]);
    if (voicing > 0) voice_text << "+";
    voice_text << voicing;
    Text spread = text(display.values[3]);
    spread << (steps(controls.value[3], 2) ? "on" : "off");
}
}
