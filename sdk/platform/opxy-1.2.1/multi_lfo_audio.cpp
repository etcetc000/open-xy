// The audio role's evaluation of the copies of stock's LFO (multi_lfo_audio.hpp).
#include "multi_lfo_audio.hpp"

namespace opxy { namespace platform { namespace multi_lfo {
namespace {
// Each stock LFO state (a voice's at +0x98, or a track's) has a state per
// copy, made from stock's own state the first time the copy runs there, so
// fields stock sets once per voice start from stock's values; a note start
// then resets it as stock resets its own. Voices and tracks are fixed stock
// objects, so their states keep their addresses.
const u32 kStates = 64;
struct Owned {
    Address key;
    unsigned char state[copies][state_bytes] __attribute__((aligned(4)));
};
Owned owned[kStates];

Address copy_state(Address stock_state, u32 copy) {
    u32 at = static_cast<u32>(static_cast<u32>(stock_state >> 2) * 2654435761u) >> 26;
    for (u32 n = 0; n < kStates; ++n, at = (at + 1) & (kStates - 1)) {
        Owned& o = owned[at];
        if (o.key == stock_state) return reinterpret_cast<Address>(o.state[copy]);
        if (!o.key) {
            o.key = stock_state;
            const unsigned char* from = reinterpret_cast<const unsigned char*>(stock_state);
            for (u32 c = 0; c < copies; ++c)
                for (u32 i = 0; i < state_bytes; ++i) o.state[c][i] = from[i];
            return reinterpret_cast<Address>(o.state[copy]);
        }
    }
    return 0;  // full: copies do not run there
}

// The copies' saved words, last in first out, each with its track and scope.
const u32 kDepth = 96;
struct Saved { u32 track, scope, index; i32 original; };
Saved saved[kDepth];
u32 depth;

void put_back(u32 track, u32 scope) {
    while (depth && saved[depth - 1].track == track && saved[depth - 1].scope == scope) {
        const Saved& s = saved[--depth];
        stock_lfo::words(track)[s.index] = s.original;
    }
}

// Whether stock's LFO applied in the voice being rendered, and per track
// before and after its voices: only then does its own restore run.
bool stock_voice;
bool stock_before[tracks], stock_after[tracks];
// The voice being rendered pushed copies' words.
bool copies_voice;
// Set while a copy runs: its telemetry is not stock's LFO's.
bool running_copy;

// A copy's settings in stock's place (its type in the type table, its words
// in the track's LFO words) for one call of stock's code, then stock's back.
struct Swap {
    u32 track;
    i32* words;
    u32 stock_type;
    i32 stock_words[multi_lfo::words];
    Swap(u32 t, Address track_words, const Settings& s) : track(t), words(reinterpret_cast<i32*>(track_words)) {
        u32& type = stock_lfo::type(track);
        stock_type = type;
        type = s.type;
        for (u32 i = 0; i < multi_lfo::words; ++i) {
            stock_words[i] = words[first_word + i];
            words[first_word + i] = s.words[i];
        }
    }
    ~Swap() {
        for (u32 i = 0; i < multi_lfo::words; ++i) words[first_word + i] = stock_words[i];
        stock_lfo::type(track) = stock_type;
    }
};

// One evaluation of every copy that is on, then stock's LFO when it is on.
// Returns whether stock's LFO applied; `pushed` says whether a copy's words
// went on the stack.
bool evaluate(Address state, u32 track, Address track_words, u32 pass, u32 scope, bool& pushed) {
    pushed = false;
    if (track < tracks) {
        for (u32 c = 0; c < copies; ++c) {
            const Settings& s = audio(track, c);
            if (!s.on) continue;
            const Address own = copy_state(state, c);
            if (!own) continue;
            i32& slot = stock_lfo::slot_index(track);
            i32& original = stock_lfo::slot_original(track);
            const i32 stock_slot = slot, stock_original = original;
            slot = -1;
            {
                Swap swap(track, track_words, s);
                running_copy = true;
                stock_lfo::evaluate(own, track, track_words, pass);
                running_copy = false;
            }
            if (slot != -1 && depth < kDepth) {
                saved[depth++] = {track, scope, static_cast<u32>(slot), original};
                pushed = true;
            } else if (slot != -1) {
                // No room to put it back later: put it back now.
                stock_lfo::words(track)[slot] = original;
            }
            slot = stock_slot;
            original = stock_original;
        }
    }
    return stock_lfo::on(track) && stock_lfo::evaluate(state, track, track_words, pass) != 0;
}
}

void voice_pass0(Address state, u32 track, Address track_words) {
    // A voice whose restore never ran must not leave its words modulated.
    while (depth && saved[depth - 1].scope == voice_scope) {
        const Saved& s = saved[--depth];
        stock_lfo::words(s.track)[s.index] = s.original;
    }
    bool pushed = false;
    stock_voice = evaluate(state, track, track_words, 0, voice_scope, pushed);
    copies_voice = pushed;
}

u32 voice_pass1(Address state, u32 track, Address track_words) {
    bool pushed = false;
    stock_voice = evaluate(state, track, track_words, 1, voice_scope, pushed) || stock_voice;
    copies_voice = copies_voice || pushed;
    return stock_voice || copies_voice ? 1 : 0;
}

void voice_restore(u32 track) {
    if (stock_voice) stock_lfo::restore(track);
    put_back(track, voice_scope);
    stock_voice = copies_voice = false;
}

u32 track_pass(Address state, u32 track, Address track_words, u32 pass) {
    const u32 scope = pass == 4 ? track_after : track_before;
    bool pushed = false;
    const bool applied = evaluate(state, track, track_words, pass, scope, pushed);
    if (track < tracks) {
        bool& flag = scope == track_after ? stock_after[track] : stock_before[track];
        flag = flag || applied;
    }
    return applied || pushed ? 1 : 0;
}

void track_restore(u32 track, Scope scope) {
    if (track < tracks) {
        bool& flag = scope == track_after ? stock_after[track] : stock_before[track];
        if (flag) stock_lfo::restore(track);
        flag = false;
    } else {
        stock_lfo::restore(track);
    }
    put_back(track, scope);
}

void reset(Address state, u32 track, Address track_words) {
    stock_lfo::reset(state, track, track_words);
    if (track >= tracks) return;
    for (u32 c = 0; c < copies; ++c) {
        const Settings& s = audio(track, c);
        if (!s.on) continue;
        const Address own = copy_state(state, c);
        if (!own) continue;
        Swap swap(track, track_words, s);
        stock_lfo::reset(own, track, track_words);
    }
}

void telemetry(u32 track, u32 index, i32 wave, i32 amount) {
    if (!running_copy) stock_lfo::telemetry(track, index, wave, amount);
}
}}}
