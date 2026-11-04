#include "../../sdk/platform/opxy-1.2.1/stock_sample.hpp"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <initializer_list>
static void check(bool value, const char* reason) { if (!value) { std::puts(reason); std::exit(1); } }
int main() {
    using namespace opxy::platform;
    opxy::i32 empty[8] = {};
    check(sample_gui_region(empty).pitch_cents == 0, "blank pad acquired a pitch offset");
    sample_gui_pitch(empty, 10);
    check(sample_gui_region(empty).pitch_cents == 10, "explicit empty-pad tuning was ignored");
    check(!sample_replacement(empty).assigned && !sample_replacement(nullptr).assigned,
          "empty pad was treated as a sample replacement");
    // A replacement retains controls, but never restores stale sample bounds,
    // the old canonical key, unrelated flags or the old machine settings.
    for (unsigned reverse : {0u, 1u}) {
        opxy::u32 old[8] = {1000, 100, 900, 200, 800, sample_store_settings(1234), 0, 0};
        auto* old_bytes = reinterpret_cast<unsigned char*>(old);
        old_bytes[24] = 58; old_bytes[28] = 37; old_bytes[29] = 246;
        old_bytes[31] = static_cast<unsigned char>(reverse | 128u);
        const auto prior = sample_replacement(old);
        check(prior.assigned && prior.settings == old[5], "previous machine settings lost");
        opxy::u32 next[8] = {400, 0, 400, 0, 400, sample_store_settings(4321), 0x80543c, 0x40000500};
        const opxy::u32 flags = next[6] & 0xffffff00u;
        sample_restore_replacement(next, prior);
        auto* next_bytes = reinterpret_cast<unsigned char*>(next);
        check(next[0] == 400 && next[1] == 0 && next[2] == 400 && next[3] == 0 && next[4] == 400,
              "old sample bounds restored on replacement");
        check(next[5] == sample_store_settings(4321) && (next[6] & 0xffffff00u) == flags,
              "replacement metadata overwritten");
        check(next_bytes[24] == 58 && next_bytes[28] == 37 && next_bytes[29] == 246 &&
              next_bytes[31] == (64u | reverse), "pad pitch, level or reverse lost on replacement");
        sample_restore_replacement(nullptr, prior);
    }
    empty[0] = 100; empty[6] = empty[7] = 0;
    check(sample_gui_region(empty).pitch_cents == 6000, "assigned sample's MIDI-zero root was replaced");
    for (unsigned value = 0; value < 262144; ++value) {
        const unsigned stored = sample_store_settings(value);
        if (value < 65536)
            check(stored == (0x5a800000u | (value << 7)), "legacy pad encoding changed");
        const float saved = float(stored) / 2147483648.0f;
        const auto restored = sample_settings(unsigned(saved * 2147483648.0f));
        check(restored.saved && restored.value == value, "pad settings lost bits through float serialization");
        check(!sample_settings(stored ^ 1).saved, "malformed state was accepted");
    }
    for (unsigned fade : {0u, 0x20000000u, 0x40000000u, 0x7fffffffu, 0xffffffffu})
        check(!sample_settings(fade).saved, "stock fade misidentified as pad settings");
    for (int cents = 0; cents <= 12700; ++cents) {
        const double factor = 44100.0 * 65536.0 / (440.0 * std::pow(2.0, (cents / 100.0 - 69.0) / 12.0));
        check(sample_pitch_cents(unsigned(std::round(factor))) == 6000 - cents, "MIDI-cent conversion mismatch");
        check(std::abs(sample_pitch_cents(unsigned(float(factor))) - (6000 - cents)) <= 1, "float tuning conversion mismatch");
    }
    check(sample_pitch_cents(0) == 0, "missing tuning is not neutral");
    opxy::i32 words[] = {10, 20, 80, 90, 1, 0, 0, 0x08000000};
    auto region = sample_region(words, 100, 0);
    check(region.begin == 20 && region.end == 80 && region.reverse, "inner region lost");
    region = sample_region(words, 100, 0, true);
    check(region.begin == 10 && region.end == 90, "drum trim constrained by hidden loop markers");
    opxy::i32 gui[] = {100, 10, 90, 20, 80, 0, 60, 0};
    auto visible = sample_gui_region(gui);
    check(visible.begin == region.begin && visible.end == region.end, "GUI/audio marker layouts disagree");
    gui[1] = gui[3] = 0; gui[2] = gui[4] = 100;
    visible = sample_gui_region(gui);
    check(visible.begin == 0 && visible.end == 100, "new assignment displays loop start as end");
    gui[2] = -1; visible = sample_gui_region(gui);
    check(visible.end == 100, "GUI full-end sentinel lost");
    gui[2] = 0; visible = sample_gui_region(gui);
    check(visible.end == 0, "explicit empty trim displayed as full sample");
    for (int pitch = -1200; pitch <= 1200; pitch += 10) {
        sample_gui_pitch(gui, pitch);
        check(sample_gui_region(gui).pitch_cents == pitch, "saved pad fine pitch round trip failed");
        const double factor = 44100.0 * 65536.0 / (440.0 * std::pow(2.0, ((6000 - pitch) / 100.0 - 69.0) / 12.0));
        check(sample_pitch_cents(unsigned(std::round(factor))) == pitch, "GUI/audio fine pitch disagree");
    }
    for (int semitones = -36; semitones <= 36; ++semitones) {
        const auto value = opxy::i32((double(semitones + 36) + 0.5) * 2147483648.0 / 73.0);
        check(sample_track_transpose(value) == semitones, "track transpose quantization mismatch");
    }
    check(sample_track_transpose(0x40000000) == 0 && sample_track_transpose(0x7fffffff) == 36 &&
          sample_track_transpose(0) == -36, "track transpose endpoints mismatch");
    for (int octave = -4; octave <= 4; ++octave)
        for (unsigned pad = 53; pad < 77; ++pad)
            check(sample_drum_key(unsigned(int(pad) + 12 * octave), octave) == pad,
                  "octave changed the assigned drum pad");
    for (unsigned note = 0; note < 128; ++note)
        for (int octave = -4; octave <= 4; ++octave)
            check(sample_drum_key(note, octave) >= 53 && sample_drum_key(note, octave) < 77,
                  "drum key escaped the 24-pad range");
    words[4] = 4; region = sample_region(words, 100, 0);
    check(region.begin == 10 && region.end == 90 && !region.reverse, "outer region lost");
    words[3] = -1; words[7] = -1; region = sample_region(words, 100, 0);
    check(region.end == 100 && region.level == 0, "end sentinel or negative volume failed");
    std::puts("sample state, tuning and bounds contracts passed");
}
