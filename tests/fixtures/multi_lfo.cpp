// The four LFOs on both roles, with stock's LFO code and storage as doubles:
// GUI settings reach the audio role through B3 transactions that the render
// takes at a track's block start, each copy runs through "stock's" evaluator
// with its own state and its settings swapped in, words modulated by several
// LFOs come back last in first out, stock's telemetry and restore stay stock's
// LFO's, and showing another LFO swaps it with stock's without restarting or
// doubling either.
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
// Stock's GUI storage of each track's LFO, and what its setters were given.
ml::Settings stock_gui[8];
u32 stock_loads;
// Stock's own messages for a load reach the audio role at once (before the
// render takes the swap) when this is set, else only when deliver() runs.
bool stock_first;
ml::Settings undelivered[8];
bool waiting[8];
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
void read_stock(u32 track, Settings& out) { out = stock_gui[track]; }
void deliver(u32 track);
void load_stock(u32 track, const Settings& s) {
    ++stock_loads;
    stock_gui[track] = s;
    undelivered[track] = s;
    waiting[track] = true;
    if (stock_first) deliver(track);
}
// Stock's messages for its LFO (type and on, then the words) on the audio role.
void deliver(u32 track) {
    if (!waiting[track]) return;
    waiting[track] = false;
    types[track] = undelivered[track].type;
    stock_on[track] = undelivered[track].on != 0;
    for (u32 i = 0; i < words; ++i) track_words[track][first_word + i] = undelivered[track].words[i];
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
unsigned char& on_flag(u32 track) { return *reinterpret_cast<unsigned char*>(&stock_on[track]); }
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
// The render's block start on every track: each takes its pending changes.
void blocks() {
    for (u32 t = 0; t < ml::tracks; ++t) ml::apply(t);
}
// The GUI tick until everything changed is published, then a block.
u32 publish() {
    u32 ticks = 0;
    do { ml::tick(); ++ticks; } while (ml::dirty && ticks < 32);
    blocks();
    return ticks;
}
u32 runs(Address state) { return *reinterpret_cast<u32*>(state); }
bool equal(const ml::Settings& a, const ml::Settings& b) {
    if (a.on != b.on || a.type != b.type) return false;
    for (u32 i = 0; i < ml::words; ++i)
        if (a.words[i] != b.words[i]) return false;
    return true;
}
bool stock_audio_is(u32 track, const ml::Settings& s) {
    if (types[track] != s.type || stock_on[track] != (s.on != 0)) return false;
    for (u32 i = 0; i < ml::words; ++i)
        if (track_words[track][ml::first_word + i] != s.words[i]) return false;
    return true;
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
    // Received, but the render takes them only at the track's next block.
    CHECK(messages == 2 * 11 && !ml::audio(2, 0).on);
    ml::apply(2);
    CHECK(ml::audio(2, 0).on && ml::audio(2, 1).on && !ml::audio(2, 2).on);
    ml::tick();
    ml::apply(2);
    CHECK(messages == 3 * 11 && ml::audio(2, 2).type == 6 && ml::audio(2, 2).words[1] == 50);
    ml::tick();
    CHECK(messages == 3 * 11);  // nothing changed, nothing sent
    // An incomplete transaction changes nothing on the audio role.
    drop_word = true;
    CHECK(ml::set(2, 2, lfo(6, 0, 60, 0)));
    publish();
    CHECK(ml::audio(2, 2).words[1] == 50);
    drop_word = false;
    CHECK(ml::set(2, 2, lfo(6, 0, 61, 0)));
    publish();
    CHECK(ml::audio(2, 2).words[1] == 61);
    CHECK(ml::set(2, 2, lfo(6, 0, 50, 0)));
    publish();
    // B3 ids outside the LFOs' are not ours; a bad type or a stale commit is refused.
    CHECK(!ml::receive(137, 0) && !ml::receive(191, 0) && ml::receive(171, -1));
    ml::receive(ml::message_begin, 2 | 1 << 3 | 77 << 5);
    ml::receive(ml::message_flags, 1 | 4 << 1);
    for (u32 i = 0; i < 8; ++i) ml::receive(ml::message_word + i, 99);
    ml::receive(ml::message_commit, 77);
    blocks();
    CHECK(ml::audio(2, 1).words[1] == 20);
    ml::receive(ml::message_begin, 2 | 1 << 3 | 78 << 5);
    ml::receive(ml::message_flags, 1 | 1 << 1);
    for (u32 i = 0; i < 8; ++i) ml::receive(ml::message_word + i, 99);
    ml::receive(ml::message_commit, 79);
    // A swap's messages inside a copy's transaction end it.
    ml::receive(ml::message_begin, 2 | 1 << 3 | 80 << 5);
    ml::receive(ml::message_flags, 1 | 1 << 1);
    ml::receive(ml::swap_stock_word, 99);
    for (u32 i = 0; i < 8; ++i) ml::receive(ml::message_word + i, 99);
    ml::receive(ml::message_commit, 80);
    blocks();
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

    // Showing another LFO. Stock's GUI storage holds what its audio side runs.
    stock_gui[2] = {1, 1, {4, 1, 2, 0, 0, 0, 0, 0}};
    for (u32 i = 3; i < 8; ++i) track_words[2][ml::first_word + i] = 0;
    CHECK(stock_audio_is(2, stock_gui[2]) && ml::shown(2) == 0 && ml::holder(2, 1) == 2);
    // Every LFO state of the track: stock's and the copies' in the voice and the track.
    ml::voice_pass0(voice(), 2, words(2));
    ml::voice_restore(2);
    const Address voice_copy = ml::copy_state(voice(), 1, 2), track_copy = ml::copy_state(tracks, 1, 2);
    *reinterpret_cast<u32*>(voice_state) = 111;
    *reinterpret_cast<u32*>(voice_copy) = 222;
    *reinterpret_cast<u32*>(track_state) = 333;
    *reinterpret_cast<u32*>(track_copy) = 444;
    // Show LFO 3 (slot 1): one swap transaction of 20 messages, then stock's setters.
    const ml::Settings leaving = stock_gui[2], target = ml::get(2, 1);
    u32 sent = messages;
    CHECK(ml::show(2, 2) && messages == sent + 20 && stock_loads == 1);
    CHECK(ml::shown(2) == 2 && ml::holder(2, 1) == 0 && equal(ml::get(2, 1), leaving) && equal(stock_gui[2], target));
    // Nothing changes on the audio role until the track's next block.
    CHECK(stock_audio_is(2, leaving) && equal(ml::audio(2, 1), target) && runs(voice()) == 111);
    ml::apply(2);
    CHECK(stock_audio_is(2, target) && equal(ml::audio(2, 1), leaving));
    // The two LFOs' states are exchanged: each keeps its phase.
    CHECK(runs(voice()) == 222 && runs(voice_copy) == 111 && runs(tracks) == 444 && runs(track_copy) == 333);
    // Stock's own messages for the target arrive after: nothing changes.
    ml::deliver(2);
    CHECK(stock_audio_is(2, target) && equal(ml::audio(2, 1), leaving));
    // The next tick publishes the slot once more as a copy, which changes nothing.
    sent = messages;
    ml::tick();
    ml::apply(2);
    CHECK(messages == sent + 11 && equal(ml::audio(2, 1), leaving) && stock_audio_is(2, target));
    CHECK(runs(voice()) == 222);
    // Back to LFO 1, with stock's messages first and a newer edit before the block.
    stock_first = true;
    CHECK(ml::show(2, 0) && ml::shown(2) == 0 && ml::holder(2, 1) == 2 && equal(stock_gui[2], leaving));
    CHECK(stock_audio_is(2, leaving));
    track_words[2][ml::first_word + 1] = 77;
    ml::apply(2);
    CHECK(track_words[2][ml::first_word + 1] == 77 && track_words[2][ml::first_word + 2] == 2);
    CHECK(types[2] == 1 && stock_on[2] && equal(ml::audio(2, 1), target));
    CHECK(runs(voice()) == 111 && runs(voice_copy) == 222 && runs(tracks) == 333 && runs(track_copy) == 444);
    track_words[2][ml::first_word + 1] = 1;
    stock_first = false;
    // The same LFO again changes nothing; bad numbers are refused.
    sent = messages;
    CHECK(ml::show(2, 0) && !ml::show(2, 4) && !ml::show(8, 0) && messages == sent && stock_loads == 2);
    // A stock type a copy cannot hold is refused.
    stock_gui[5] = {1, 4, {}};
    CHECK(!ml::show(5, 1) && messages == sent && ml::shown(5) == 0);
    // An LFO shown for the first time starts as a copy of the one it replaces, off.
    const ml::Settings track4 = {1, 2, {5, 6, 7, 8, 0, 0, 0, 1}};
    stock_gui[4] = track4;
    undelivered[4] = track4;
    waiting[4] = true;
    ml::deliver(4);
    CHECK(ml::show(4, 3) && ml::shown(4) == 3 && ml::holder(4, 2) == 0 && equal(ml::get(4, 2), track4));
    ml::Settings seeded = track4;
    seeded.on = 0;
    CHECK(equal(stock_gui[4], seeded));
    ml::apply(4);
    CHECK(stock_audio_is(4, seeded) && equal(ml::audio(4, 2), track4));
    publish();
    // A ring of pending changes the render never took drops the newest.
    for (i32 n = 1; n <= 9; ++n) {
        CHECK(ml::set(6, 0, lfo(1, 3, n, 0)));
        ml::tick();
    }
    ml::apply(6);
    CHECK(ml::audio(6, 0).words[1] == 8);
    CHECK(ml::set(6, 0, lfo(1, 3, 10, 0)));
    publish();
    CHECK(ml::audio(6, 0).words[1] == 10);
    CHECK(ml::set(6, 0, ml::Settings{}));
    publish();

    // Saving (version 2): per track the LFO shown, then each slot's LFO, flags and words.
    unsigned char bytes[ml::save_bytes];
    ml::save(bytes);
    const u32 at = 2 * ml::track_bytes + 1 + (3 + 4 * ml::words);
    CHECK(bytes[2 * ml::track_bytes] == 0 && bytes[at] == 2 && bytes[at + 1] == 1 && bytes[at + 2] == 1);
    CHECK(bytes[at + 3] == 4 && bytes[at + 7] == 20 && bytes[at + 11] == 4);
    const u32 four = 4 * ml::track_bytes;
    CHECK(bytes[four] == 3 && bytes[four + 1] == 1 && bytes[four + 1 + 35] == 2 && bytes[four + 1 + 70] == 0);
    CHECK(bytes[four + 72] == 1 && bytes[four + 73] == 2 && bytes[four + 74] == 5);
    // A load that failed keeps everything; one that succeeded takes its section.
    ml::load_begin();
    for (u32 i = 0; i < ml::save_bytes; ++i) ml::load_byte(i == at + 7 ? 33 : bytes[i]);
    ml::load_end(2, false);
    publish();
    CHECK(ml::get(2, 1).words[1] == 20);
    ml::load_begin();
    for (u32 i = 0; i < ml::save_bytes; ++i) ml::load_byte(i == at + 7 ? 33 : bytes[i]);
    ml::load_end(2, true);
    CHECK(publish() == 12);  // every copy is published again, two per tick
    CHECK(ml::get(2, 1).words[1] == 33 && ml::audio(2, 1).words[1] == 33 && ml::audio(2, 0).on);
    CHECK(ml::shown(4) == 3 && ml::holder(4, 2) == 0 && ml::holder(4, 0) == 1 && equal(ml::get(4, 2), track4));
    // A track whose LFO order does not add up loads with LFO 1 shown, its copies off.
    ml::load_begin();
    for (u32 i = 0; i < ml::save_bytes; ++i) ml::load_byte(i == four ? 1 : bytes[i]);
    ml::load_end(2, true);
    publish();
    CHECK(ml::shown(4) == 0 && ml::holder(4, 2) == 3 && !ml::get(4, 2).on && ml::get(2, 1).words[1] == 20);
    // Version 1 (multi-lfo-001): LFO 1 shown, slot c holding LFO c + 2.
    unsigned char v1[ml::v1_bytes] = {};
    const u32 old = (2 * ml::copies + 1) * (2 + 4 * ml::words);
    v1[old] = 1;
    v1[old + 1] = 6;
    v1[old + 2] = 9;
    ml::load_begin();
    for (u32 i = 0; i < ml::v1_bytes; ++i) ml::load_byte(v1[i]);
    ml::load_end(1, true);
    publish();
    CHECK(ml::shown(4) == 0 && ml::holder(2, 1) == 2 && ml::get(2, 1).type == 6 && ml::get(2, 1).words[0] == 9);
    CHECK(ml::audio(2, 1).type == 6 && !ml::get(2, 0).on);
    // The wrong length for its version is not whole: everything clears.
    ml::load_begin();
    for (u32 i = 0; i < ml::v1_bytes; ++i) ml::load_byte(v1[i]);
    ml::load_end(2, true);
    publish();
    CHECK(!ml::any());
    // A project saved without the section clears every copy.
    CHECK(ml::set(2, 0, lfo(1, 3, 10, 2)));
    publish();
    ml::load_begin();
    ml::load_end(0, true);
    publish();
    CHECK(!ml::any() && !ml::audio(2, 0).on && !ml::audio(2, 1).on);
    return 0;
}
