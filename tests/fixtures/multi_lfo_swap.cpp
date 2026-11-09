// Four LFOs end to end: the GUI shows another LFO through the real swap
// (multi_lfo_state.cpp) with stock's GUI storage, setters and messages as
// doubles; every GUI-to-audio message goes through one ordered queue (B3,
// stock's LFO type/on message and its word publication); the audio role's
// message task drains it; and the render runs stock's pass order per track
// (pass 2, the voice's passes 0 and 1, its restore, pass 4, the restores) with
// the real evaluation (multi_lfo_audio.cpp) around a stock evaluator double.
//
// "LFO 2 on and set, switch to LFO 1: LFO 2 still modulates", first with
// every message delivered, then with the swap's transaction and the copy
// published after it lost on the way (the audio role can't acknowledge): the
// GUI publishes every slot again in turn, so LFO 2 comes back without a
// restart and LFO 1 stops doubling.
#include <multi_lfo_state.cpp>
#include <multi_lfo_audio.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace ml = opxy::platform::multi_lfo;

namespace {
// Audio role: stock's LFO type, on flag, the track's words and save slot.
u32 audio_types[16];
unsigned char audio_on[16];
i32 audio_words[16][58];
i32 slot_index[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, slot_original[16];
alignas(4) unsigned char voice_state[ml::state_bytes];
alignas(4) unsigned char track_states[8][ml::state_bytes];

// GUI role: stock's LFO storage of each track, its LFO type/on publication
// cache (8d4dc794 sends only a change) and its word publication (8d5accf8
// marks the track, the GUI loop publishes it).
ml::Settings gui[8];
i32 typeon_sent[8] = {-999, -999, -999, -999, -999, -999, -999, -999};
bool words_marked[8];

// The one GUI-to-audio queue.
enum Kind { b3, typeon, track_words };
struct Message {
    Kind kind;
    u32 id;
    i32 value;
    u32 track, type, on;
    i32 words[ml::words];
};
const u32 kQueue = 4096;
Message queue[kQueue];
u32 queued;
// While set, the SDK's B3 messages are lost on the way; stock's still arrive.
bool lose_b3;

void push(const Message& m) {
    if (queued < kQueue) queue[queued++] = m;
}
void publish_typeon(u32 t, u32 type, u32 on) {
    const i32 sent = on ? static_cast<i32>(type) : static_cast<i32>(type) - 100;
    if (sent == typeon_sent[t]) return;
    typeon_sent[t] = sent;
    Message m = {typeon, 0, 0, t, type, on, {}};
    push(m);
}
void publish_words(u32 t) {
    Message m = {track_words, 0, 0, t, 0, 0, {}};
    for (u32 i = 0; i < ml::words; ++i) m.words[i] = gui[t].words[i];
    push(m);
}
// The audio role's message task.
void drain() {
    for (u32 n = 0; n < queued; ++n) {
        const Message& m = queue[n];
        if (m.kind == b3) {
            ml::receive(m.id, m.value);
        } else if (m.kind == typeon) {
            audio_types[m.track] = m.type;
            audio_on[m.track] = static_cast<unsigned char>(m.on);
        } else {
            for (u32 i = 0; i < ml::words; ++i) audio_words[m.track][ml::first_word + i] = m.words[i];
        }
    }
    queued = 0;
}

// Stock's evaluator in miniature: word 26 is the LFO's pass (0 per voice,
// 2 per track), word 24 the word it modulates and word 25 the amount, added
// through the track's one save slot as 8d518ec4 does.
u32 evaluate_double(Address, u32 track, Address words, u32 pass) {
    const i32* w = reinterpret_cast<const i32*>(words);
    if (pass != 3 && pass != static_cast<u32>(w[ml::first_word + 2])) return 0;
    const i32 index = w[ml::first_word];
    slot_index[track] = index;
    slot_original[track] = audio_words[track][index];
    audio_words[track][index] += w[ml::first_word + 1];
    return 1;
}
}

namespace opxy { namespace platform { namespace multi_lfo {
void send(u32 id, i32 value) {
    if (lose_b3) return;
    Message m = {b3, id, value, 0, 0, 0, {}};
    push(m);
}
void read_stock(u32 track, Settings& out) { out = gui[track]; }
void load_stock(u32 track, const Settings& s) {
    gui[track] = s;
    publish_typeon(track, s.type, s.on ? 1 : 0);
    words_marked[track] = true;
}
namespace stock_lfo {
u32 evaluate(Address state, u32 track, Address words, u32 pass) { return evaluate_double(state, track, words, pass); }
void reset(Address, u32, Address) {}
void restore(u32 track) {
    if (::slot_index[track] == -1) return;
    audio_words[track][::slot_index[track]] = ::slot_original[track];
    ::slot_index[track] = -1;
}
void telemetry(u32, u32, i32, i32) {}
bool on(u32 track) { return audio_on[track] != 0; }
unsigned char& on_flag(u32 track) { return audio_on[track]; }
u32& type(u32 track) { return audio_types[track]; }
i32* words(u32 track) { return audio_words[track]; }
i32& slot_index(u32 track) { return ::slot_index[track]; }
i32& slot_original(u32 track) { return ::slot_original[track]; }
}
}}}

namespace {
// What track 0's voice renders with: its words 1 and 2 during the voice.
i32 heard1, heard2;
// One audio block: every track's passes in stock's order, a voice on track 0.
void block() {
    for (u32 t = 0; t < 8; ++t) {
        const Address words = reinterpret_cast<Address>(audio_words[t]);
        const Address track = reinterpret_cast<Address>(track_states[t]);
        const u32 before = ml::track_pass(track, t, words, 2);
        if (t == 0) {
            const Address voice = reinterpret_cast<Address>(voice_state);
            ml::voice_pass0(voice, t, words);
            const u32 restore = ml::voice_pass1(voice, t, words);
            heard1 = audio_words[0][1];
            heard2 = audio_words[0][2];
            if (restore) ml::voice_restore(t);
        }
        if (ml::track_pass(track, t, words, 4)) ml::track_restore(t, ml::track_after);
        if (before) ml::track_restore(t, ml::track_before);
    }
}
// One GUI frame (the SDK's tick, then stock's word publication), the audio
// role's message task, then a block.
void frame() {
    ml::tick();
    for (u32 t = 0; t < 8; ++t)
        if (words_marked[t]) {
            words_marked[t] = false;
            publish_words(t);
        }
    drain();
    block();
}
void frames(u32 n) {
    for (u32 i = 0; i < n; ++i) frame();
}
// Stock's controls on the LFO page: M4 (on setter and type/on message) and an
// encoder edit of an LFO word (stored and live word, then its publication).
void m4(u32 t) {
    gui[t].on = gui[t].on ? 0 : 1;
    publish_typeon(t, gui[t].type, gui[t].on);
}
void edit(u32 t, u32 word, i32 value) {
    gui[t].words[word] = value;
    publish_words(t);
}
bool hears(i32 one, i32 two) { return heard1 == one && heard2 == two; }

// LFO 1 on (word 1, +10, per voice); LFO 2 shown, switched on and set to
// word 2, +20; then LFO 1 shown again, with `lose` saying whether the SDK's
// messages of that frame are lost. Returns the frames until both modulate
// again (0: at once), or a large number when they never do.
u32 run(bool lose) {
    // Start: a project load without the section, every copy published off.
    ml::load_begin();
    ml::load_end(0, true);
    frames(16);
    audio_words[0][1] = 100;
    audio_words[0][2] = 200;
    gui[0] = {0, 1, {1, 10, 0, 0, 0, 0, 0, 0}};
    words_marked[0] = true;
    m4(0);
    frame();
    if (!hears(110, 200)) return 1000;
    // LFO 2: a copy of LFO 1, off. LFO 1 runs on as a copy, unchanged.
    if (!ml::show(0, 1)) return 1001;
    frame();
    if (!hears(110, 200)) return 1002;
    m4(0);
    edit(0, 0, 2);
    edit(0, 1, 20);
    frame();
    if (!hears(110, 220)) return 1003;
    // Back to LFO 1: LFO 2 must keep running as a copy.
    lose_b3 = lose;
    if (!ml::show(0, 0)) return 1004;
    frame();
    lose_b3 = false;
    for (u32 n = 0; n < 200; ++n) {
        if (hears(110, 220)) return n;
        frame();
    }
    return 2000;
}
}

int main() {
    // Every message delivered: LFO 2 never stops.
    CHECK(run(false) == 0);
    // Words back after the block: nothing stays modulated.
    CHECK(audio_words[0][1] == 100 && audio_words[0][2] == 200 && ml::depth == 0);
    // The swap and the copy after it lost: until the slot is published again,
    // the audio role still runs LFO 1 as the copy (doubled) and LFO 2 is
    // silent. The GUI publishes each slot again in turn, starting with the one
    // just swapped, so both come back after four quiet ticks.
    const u32 healed = run(true);
    CHECK(healed > 0 && healed <= 4);
    CHECK(audio_words[0][1] == 100 && audio_words[0][2] == 200 && ml::depth == 0);
    // And it stays healed: the next publications change nothing.
    frames(ml::tracks * ml::copies * 4);
    CHECK(hears(110, 220));
    return 0;
}
