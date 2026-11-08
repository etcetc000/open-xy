// The copies of stock's LFO on both roles, with stock's LFO code as doubles:
// GUI settings reach the audio role through B3 transactions, each copy runs
// through "stock's" evaluator with its own state and its settings swapped in,
// words modulated by several LFOs come back last in first out, and stock's
// telemetry and restore stay stock's LFO's.
#include <multi_lfo_state.cpp>
#include <multi_lfo_audio.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace ml = opxy::platform::multi_lfo;

namespace {
// Stock's audio-side tables: the LFO type per track, each track's words, the
// save slot, and whether stock's own LFO is on.
u32 types[16];
i32 track_words[16][58];
i32 slots[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, originals[16];
bool stock_on[16];
// A per-sample buffer that LFOs scale in place (buf *= 1 + lfo / 100).
i32 buffer = 1000;
u32 evaluations, stock_evaluations, restores, telemetry_calls, resets;
Address reset_states[8];
u32 reset_count;
u32 messages;
bool drop_word;  // lose one B3 word in transit
// A voice's and a track's stock LFO state; stock's evaluator counts its runs
// in the state's first word, so each copy's own state is told apart.
alignas(4) unsigned char voice_state[ml::state_bytes];
alignas(4) unsigned char track_state[ml::state_bytes];

// Stock's evaluator, in miniature: type 1 (value) adds word 25 to the word that word 24
// names (a word destination, through the one save slot); type 6 (duck) scales
// the buffer by word 25 percent. Pass 0 for voices, 2 for tracks; 3 any.
u32 evaluate_double(Address state, u32 track, Address words, u32 pass) {
    ++evaluations;
    i32* w = reinterpret_cast<i32*>(words);
    u32& runs = *reinterpret_cast<u32*>(state);
    const u32 own_pass = w[ml::first_word + 2];
    if (pass != own_pass && pass != 3) return 0;
    ++runs;
    // Stock calls its telemetry from inside the evaluator: the SDK hooks that call.
    ml::telemetry(track, 0, static_cast<i32>(runs), w[ml::first_word + 1]);
    if (types[track] == 1) {
        const i32 index = w[ml::first_word];
        slots[track] = index;
        originals[track] = w[index];
        w[index] += w[ml::first_word + 1];
    } else if (types[track] == 6) {
        buffer += buffer * w[ml::first_word + 1] / 100;
    }
    return 1;
}
}

namespace opxy { namespace platform { namespace multi_lfo {
void send(u32 id, i32 value) {
    ++messages;
    if (drop_word && id == message_word + 3) return;
    if (!receive(id, value)) __builtin_trap();
}
namespace stock_lfo {
u32 evaluate(Address state, u32 track, Address words, u32 pass) {
    if (state == reinterpret_cast<Address>(voice_state) || state == reinterpret_cast<Address>(track_state))
        ++stock_evaluations;
    return evaluate_double(state, track, words, pass);
}
void reset(Address state, u32, Address) {
    *reinterpret_cast<u32*>(state) = 0;
    if (reset_count < 8) reset_states[reset_count++] = state;
    ++resets;
}
void restore(u32 track) {
    ++restores;
    if (slots[track] >= 0) track_words[track][slots[track]] = originals[track];
    slots[track] = -1;
}
void telemetry(u32, u32, i32, i32) { ++telemetry_calls; }
bool on(u32 track) { return stock_on[track]; }
u32& type(u32 track) { return types[track]; }
i32* words(u32 track) { return track_words[track]; }
i32& slot_index(u32 track) { return slots[track]; }
i32& slot_original(u32 track) { return originals[track]; }
}
}}}

namespace {
Address voice() { return reinterpret_cast<Address>(voice_state); }
Address words(u32 track) { return reinterpret_cast<Address>(track_words[track]); }
// LFO words: destination word, amount, pass.
ml::Settings lfo(u32 type, i32 destination, i32 amount, i32 pass) {
    ml::Settings s = {1, type, {destination, amount, pass, 0, 0, 0, 0, 0}};
    return s;
}
void stock_lfo_words(u32 track, i32 destination, i32 amount, i32 pass) {
    track_words[track][24] = destination;
    track_words[track][25] = amount;
    track_words[track][26] = pass;
}
// The GUI tick until everything changed is published.
u32 publish() {
    u32 ticks = 0;
    do { ml::tick(); ++ticks; } while (ml::dirty && ticks < 32);
    return ticks;
}
}

int main() {
    // GUI: settings are checked, kept and published two copies per tick, each
    // one B3 transaction of 11 messages.
    CHECK(!ml::set(8, 0, lfo(1, 3, 10, 0)) && !ml::set(0, 3, lfo(1, 3, 10, 0)));
    CHECK(!ml::set(0, 0, lfo(4, 3, 10, 0)) && !ml::any());
    CHECK(ml::set(2, 0, lfo(1, 3, 10, 0)) && ml::set(2, 1, lfo(1, 3, 20, 0)) && ml::set(2, 2, lfo(6, 0, 50, 0)));
    CHECK(ml::any() && ml::get(2, 1).words[1] == 20);
    ml::tick();
    CHECK(messages == 2 * 11 && ml::audio(2, 0).on && ml::audio(2, 1).on && !ml::audio(2, 2).on);
    ml::tick();
    CHECK(messages == 3 * 11 && ml::audio(2, 2).type == 6 && ml::audio(2, 2).words[1] == 50);
    ml::tick();
    CHECK(messages == 3 * 11);  // nothing changed, nothing sent
    // An incomplete transaction changes nothing on the audio role.
    drop_word = true;
    CHECK(ml::set(2, 2, lfo(6, 0, 60, 0)));
    ml::tick();
    CHECK(ml::audio(2, 2).words[1] == 50);
    drop_word = false;
    CHECK(ml::set(2, 2, lfo(6, 0, 61, 0)));
    ml::tick();
    CHECK(ml::audio(2, 2).words[1] == 61);
    CHECK(ml::set(2, 2, lfo(6, 0, 50, 0)));
    ml::tick();
    // B3 ids outside the copies' are not ours; a bad type or a stale commit is refused.
    CHECK(!ml::receive(137, 0) && !ml::receive(171, 0));
    ml::receive(ml::message_begin, 2 | 1 << 3 | 77 << 5);
    ml::receive(ml::message_flags, 1 | 4 << 1);
    for (u32 i = 0; i < 8; ++i) ml::receive(ml::message_word + i, 99);
    ml::receive(ml::message_commit, 77);
    CHECK(ml::audio(2, 1).words[1] == 20);
    ml::receive(ml::message_begin, 2 | 1 << 3 | 78 << 5);
    ml::receive(ml::message_flags, 1 | 1 << 1);
    for (u32 i = 0; i < 8; ++i) ml::receive(ml::message_word + i, 99);
    ml::receive(ml::message_commit, 79);
    CHECK(ml::audio(2, 1).words[1] == 20);

    // Audio: a voice on track 3 with stock's LFO on, adding 5 to word 3. Two
    // copies add 10 and 20 to the same word; the third ducks the buffer.
    stock_on[2] = true;
    types[2] = 1;
    stock_lfo_words(2, 3, 5, 0);
    track_words[2][3] = 100;
    ml::voice_pass0(voice(), 2, words(2));
    CHECK(track_words[2][3] == 135 && buffer == 1500);
    // The swap put stock's own type and LFO words back.
    CHECK(types[2] == 1 && track_words[2][24] == 3 && track_words[2][25] == 5 && track_words[2][26] == 0);
    // Stock's slot is stock's LFO's: index 3 with the copies' sum as its original.
    CHECK(slots[2] == 3 && originals[2] == 130);
    // Only stock's LFO reports telemetry; every copy kept its own state.
    CHECK(telemetry_calls == 1 && stock_evaluations == 1 && *reinterpret_cast<u32*>(voice_state) == 1);
    // Pass 1: nothing is on pass 1, but the voice still needs its restore.
    CHECK(ml::voice_pass1(voice(), 2, words(2)) == 1);
    ml::voice_restore(2);
    CHECK(track_words[2][3] == 100 && restores == 1 && slots[2] == -1);

    // Stock's LFO off: copies still run, and stock's restore does not.
    stock_on[2] = false;
    const u32 stock_runs = stock_evaluations;
    ml::voice_pass0(voice(), 2, words(2));
    CHECK(track_words[2][3] == 130 && stock_evaluations == stock_runs);
    CHECK(ml::voice_pass1(voice(), 2, words(2)) == 1);
    ml::voice_restore(2);
    CHECK(track_words[2][3] == 100 && restores == 1);
    // A voice whose restore never ran is put back before the next voice.
    ml::voice_pass0(voice(), 2, words(2));
    ml::voice_pass0(voice(), 2, words(2));
    CHECK(track_words[2][3] == 130);
    ml::voice_restore(2);
    CHECK(track_words[2][3] == 100);
    // No copy on and stock's LFO off: no restore needed.
    CHECK(ml::voice_pass1(voice(), 5, words(5)) == 0);

    // Tracks: copies on pass 2 (before the voices) and pass 4 (after them).
    CHECK(ml::set(2, 0, lfo(1, 3, 10, 2)) && ml::set(2, 1, lfo(1, 4, 20, 4)) && ml::set(2, 2, lfo(1, 4, 0, 9)));
    publish();
    const Address tracks = reinterpret_cast<Address>(track_state);
    track_words[2][4] = 7;
    CHECK(ml::track_pass(tracks, 2, words(2), 2) == 1 && track_words[2][3] == 110 && track_words[2][4] == 7);
    CHECK(ml::track_pass(tracks, 2, words(2), 4) == 1 && track_words[2][4] == 27);
    ml::track_restore(2, ml::track_after);
    CHECK(track_words[2][4] == 7 && track_words[2][3] == 110 && restores == 1);
    ml::track_restore(2, ml::track_before);
    CHECK(track_words[2][3] == 100 && restores == 1);
    // Stock's LFO on at pass 2: its restore runs for the scope it applied in.
    stock_on[2] = true;
    stock_lfo_words(2, 4, 1, 2);
    CHECK(ml::track_pass(tracks, 2, words(2), 2) == 1 && track_words[2][3] == 110 && track_words[2][4] == 8);
    ml::track_restore(2, ml::track_after);
    CHECK(restores == 1 && track_words[2][4] == 8);
    ml::track_restore(2, ml::track_before);
    CHECK(restores == 2 && track_words[2][4] == 7 && track_words[2][3] == 100);

    // A note start resets stock's state, then each copy's own state.
    reset_count = 0;
    ml::reset(voice(), 2, words(2));
    CHECK(reset_count == 4 && reset_states[0] == voice());
    for (u32 i = 1; i < 4; ++i) CHECK(reset_states[i] != voice() && reset_states[i] != reset_states[i - 1]);
    CHECK(types[2] == 1 && track_words[2][24] == 4);
    // Telemetry from stock's call site stays stock's.
    ml::telemetry(2, 0, 1, 1);
    CHECK(telemetry_calls == 3);

    // Saving: 816 bytes, each copy's flags then its words, little-endian.
    unsigned char bytes[ml::save_bytes];
    ml::save(bytes);
    const u32 at = (2 * ml::copies + 1) * (2 + 4 * ml::words);
    CHECK(bytes[at] == 1 && bytes[at + 1] == 1 && bytes[at + 2] == 4 && bytes[at + 6] == 20 && bytes[at + 10] == 4);
    // A load that failed keeps the copies; one that succeeded takes its section.
    ml::load_begin();
    for (u32 i = 0; i < ml::save_bytes; ++i) ml::load_byte(i == at + 6 ? 33 : bytes[i]);
    ml::load_end(true, false);
    publish();
    CHECK(ml::get(2, 1).words[1] == 20);
    ml::load_begin();
    for (u32 i = 0; i < ml::save_bytes; ++i) ml::load_byte(i == at + 6 ? 33 : bytes[i]);
    ml::load_end(true, true);
    CHECK(publish() == 12);  // every copy is published again, two per tick
    CHECK(ml::get(2, 1).words[1] == 33 && ml::audio(2, 1).words[1] == 33 && ml::audio(2, 0).on);
    // A project saved without the section clears every copy.
    ml::load_begin();
    ml::load_end(false, true);
    publish();
    CHECK(!ml::any() && !ml::audio(2, 0).on && !ml::audio(2, 1).on);
    // A short section is not whole.
    ml::load_begin();
    for (u32 i = 0; i + 1 < ml::save_bytes; ++i) ml::load_byte(bytes[i]);
    ml::load_end(true, true);
    publish();
    CHECK(!ml::any());
    return 0;
}
