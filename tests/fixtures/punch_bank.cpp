// Drives the punch-in track's second bank (punch_bank.hpp) with the example
// effects: which presses it takes, latching, and global targeting.
#include <cstdio>
#include <cstdlib>
#include <opxy/punch.hpp>
#include "punch_bank.hpp"

namespace {
void require(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "failed: %s\n", what); std::exit(1); }
}
unsigned overrides[8], level[8];
}

void sdk_punch_override(opxy::u32 track, opxy::u32 parameter, opxy::i32, void*) {
    ++overrides[track];
    if (parameter == opxy::punch::parameter_level) ++level[track];
}
void sdk_punch_master(opxy::u32, opxy::i32) {}
opxy::i32 sdk_punch_master_project(opxy::u32) { return 0x40000000; }
void sdk_punch_aux(opxy::u32, opxy::i32) {}
void sdk_punch_track_set(opxy::u32, opxy::u32, opxy::i32) {}
opxy::i32 sdk_punch_track_own(opxy::u32, opxy::u32) { return 0; }
opxy::i32 sdk_punch_live(opxy::u32, opxy::u32) { return 0x60000000; }   // every track at 75 %
void sdk_punch_filter(opxy::u32, opxy::u32) {}
bool sdk_punch_key_held(opxy::u32) { return false; }
void sdk_punch_play(opxy::u32, opxy::u8, opxy::u8, opxy::u32, opxy::u32) {}

int main() {
    using namespace opxy;
    namespace bank = opxy::punch_bank;
    const punch::Effect* const* effects = sdk_punch_effects;
    const u32 count = sdk_punch_effect_count;

    // Low octave: every key. High octave: F .. G# (12..15), wah on B (18), the
    // FX boosts on C and C# (19, 20), the sweep on A (16) and octave on A# (17);
    // D, D# and E (21..23) are free.
    const u8 layout[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    require(count == sizeof layout, "every key but high D, D# and E");
    for (u8 key : layout) {
        u32 found = 0;
        for (u32 i = 0; i < count; ++i) found += effects[i]->key == key;
        require(found == 1, "each layout key registered exactly once");
    }

    // Only shift + a key an effect plays; each octave has its own effects.
    require(bank::take(false, 101, effects, count) == -1 && bank::take(false, 113, effects, count) == -1, "no shift: stock");
    require(bank::take(true, 101, effects, count) == 0 && bank::take(true, 113, effects, count) == 12, "F per octave");
    require(bank::take(true, 111, effects, count) == 10 && bank::take(true, 112, effects, count) == 11, "clip and gain side by side");
    require(bank::take(true, 103, effects, count) == 2, "low G: the snare roll");
    require(bank::take(true, 105, effects, count) == 4 && bank::take(true, 109, effects, count) == 8, "A and C#");
    require(bank::take(true, 102, effects, count) == 1 && bank::take(true, 104, effects, count) == 3, "F# lowpass, G# hipass (low)");
    require(bank::take(true, 115, effects, count) == 14, "high G: the gate");
    require(bank::take(true, 120, effects, count) == 19 && bank::take(true, 121, effects, count) == 20, "uppermost: FX I, FX II");
    const u8 free_keys[] = {122, 123, 124};
    require(bank::take(true, 117, effects, count) == 16 && bank::take(true, 118, effects, count) == 17, "high A: the sweep, A#: octave");
    for (u8 pitch : free_keys)
        require(bank::take(true, pitch, effects, count) == -1, "high D, D#, E: no effect");
    require(bank::take(true, 100, effects, count) == -1 && bank::take(true, 125, effects, count) == -1, "outside");
    require(bank::take(true, 101, effects, 0) == -1, "no effects: all stock");

    // Latched per key at press: the release of that keyboard pitch ends it,
    // shift or not; another pitch's release stays stock's.
    bank::Bank held{};
    require(held.latest() == -1 && held.keys() == 0, "empty");
    require(held.press(53, 4) && held.press(60, 9), "two presses");
    require(held.keys() == (1u << 4 | 1u << 9) && held.latest() == 9, "both held, latest D");
    require(!held.release(61, 0, effects, count), "an unlatched release is stock's");
    require(held.release(60, 0, effects, count) && held.keys() == 1u << 4 && held.latest() == 4, "D released");
    require(held.press(53, 4) && held.keys() == 1u << 4, "a repeated pitch latches once");
    require(held.release(53, 0, effects, count) && !held.release(53, 0, effects, count) && held.keys() == 0,
            "released once");

    // Kill (low A#, 5) releases on the next bar: its release keeps it held
    // until the position reaches a multiple of 384 ticks.
    require(held.press(70, 5) && held.release(70, 500, effects, count), "kill released at 500");
    require(held.keys() == 1u << 5 && !held.release(70, 510, effects, count), "held after key-off, once");
    held.expire(767);
    require(held.keys() == 1u << 5, "still held before the bar");
    held.expire(768);
    require(held.keys() == 0, "ended on the bar");
    require(held.press(70, 5) && held.release(70, 768, effects, count), "released exactly on a bar");
    held.expire(1000);
    require(held.keys() == 1u << 5, "runs to the following bar");
    held.expire(10);
    require(held.keys() == 0, "the position went back: ended");
    require(held.press(70, 5) && held.release(70, 100, effects, count) && held.press(70, 5), "pressed again");
    held.expire(384);
    require(held.keys() == 1u << 5 && held.release(70, 400, effects, count), "a new press holds past the bar");
    held.expire(768);
    require(held.keys() == 0, "...until its own release's bar");
    // Leaving page 2 releases everything: plain keys at once, kill on its bar.
    require(held.press(53, 4) && held.press(54, 0) && held.press(70, 5), "three held");
    held.release_all(100, effects, count);
    require(held.keys() == 1u << 5, "only kill runs on");
    held.expire(384);
    require(held.keys() == 0, "kill ended on the bar");
    for (u8 pitch = 0; pitch < bank::Bank::kLatches; ++pitch) require(held.press(pitch, pitch % 12), "full keyboard");
    require(!held.press(100, 0), "beyond the keyboard: left to stock");

    // Global: a held key runs on every instrument track 0..7 whatever track
    // is selected; first per track, lead once per round.
    u32 engaged[8] = {};
    u32 led = 0, leads = 0, calls = 0;
    const u32 pump = 1u << 4, squash = 1u << 8;
    for (int round = 0; round < 2; ++round) {
        led = 0;
        for (u32 track = 0; track < 8; ++track) {
            const bool drum = track < 4;
            const punch::Overrides out{track, nullptr};
            bank::run(pump | squash, effects, count, engaged[track], track, drum, 0, 0, &led,
                      [&](const punch::Effect& effect, const punch::Context& context) {
                          require(context.track == track && context.drum == drum, "context track");
                          require(context.first == (round == 0), "first only on the first pass");
                          leads += context.lead, ++calls;
                          if (effect.tick) effect.tick(context, out);
                          return false;
                      });
        }
    }
    require(calls == 2 * 2 * 8, "both keys on all eight tracks, each round");
    require(leads == 2 * 2, "one lead per effect per round");
    for (u32 track = 0; track < 8; ++track)
        require((level[track] != 0) == (track >= 4), "the pump dips melodic tracks only (at beat start)");
    // Released: no call, and the next press is first again.
    bank::run(0, effects, count, engaged[5], 5, false, 0, 0, nullptr,
              [&](const punch::Effect&, const punch::Context&) { require(false, "nothing held"); return false; });
    bool first = false;
    bank::run(pump, effects, count, engaged[5], 5, false, 0, 0, nullptr,
              [&](const punch::Effect&, const punch::Context& context) { first = context.first; return false; });
    require(first, "a new press starts fresh");
    {   // latch: holds() tells a pressed key from one running on after release
        bank::Bank latched{};
        require(latched.press(110, 9) && latched.holds(110) && !latched.holds(111), "a pressed key is held");
        latched.press(112, 7);   // kill (low B) releases on the bar
        require(latched.release(112, 10, effects, count) && !latched.holds(112) && latched.pressed() == 1,
                "a released key running on is not held");
        require(latched.release(110, 10, effects, count) && !latched.holds(110) && latched.pressed() == 0, "released");
    }
    return 0;
}
