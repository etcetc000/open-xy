// Punch-in effects: the punch-in track's second bank. A shift press while
// the punch-in track is selected turns page 2 on or off; on page 2 a key
// there is taken before stock's live note-on, latched per key, and runs that key's
// effect in stock's per-track, per-note and step passes on every instrument
// track. Stock's own punch-in keys are left alone. GUI/sequencer side; the
// step hook runs from the track player on the same role.
#include <opxy/punch.hpp>
#include "punch_bank.hpp"
#include "stock.hpp"

namespace {
using opxy::i32;
using opxy::i64;
using opxy::u8;
using opxy::u16;
using opxy::u32;
using namespace opxy::platform;
namespace punch = opxy::punch;
namespace bank = opxy::punch_bank;

const u32 kBuildMask = 0x8d58c6c0;     // (sequencer)
// Live notes from the keyboard handler (`8c092188`) for the selected track:
// note-on (sequencer, track, pitch, velocity, event) and note-off (sequencer,
// track, pitch, event). Stock's remap (sequencer, u8* track, u8* pitch,
// punched) moves a note on the punch-in track to its punch-in pitch, 101..124.
// Shift: the top-level button dispatcher stores its state through `8d51f198`
// (1 at `8c0b5b66`, 0 at `8c0b5bea`) for button 55 before any view sees it;
// the press is hooked to turn page 2 on and off. (The flag at sequencer +
// 0x3080 is set only when the current view accepts shift, which the punch-in
// track's view does not.)
const u32 kNoteOn = 0x8d594278;
const u32 kNoteOff = 0x8d5941ac;
const u32 kRemap = 0x8d5940c6;
const u32 kShiftSet = 0x8d51f198;      // (held): stores shift's state, called at each press and release
const u32 kPunchTrack = 9;
const u32 kTrackPass = 0x8d58ca10;     // (sequencer, track, bend, overrides)
const u32 kNotePass = 0x8d58cf58;      // (sequencer, track, unused, event, generated)
const u32 kStepRemap = 0x8d58c84c;     // (sequencer, track, step*) -> remapped
const u32 kOverride = 0x8d58c5b8;      // (track, parameter, value, overrides)
const u32 kMelodic = 0x8d587830;       // (track) -> nonzero unless a drum engine
const u32 kBend = 0x8d58c9e4;          // () -> stock's punch-in modulation
// Stock's note player, as its own repeat and fill keys call it: (sequencer,
// track, pitch, length (2 words), velocity, 0, 0, time (2 words), flags, 0).
// Length counts 480 per step; flag 4 marks a note a punch-in effect generated.
const u32 kPlay = 0x8d595738;
const u32 kNow = 0x8d59357c;           // () -> 64-bit event time
const u32 kSequencer = 0x860c6adc;
const u32 kPlayGenerated = 4;
const u32 kPlayTicksPer96 = 20;        // 480 per step, 24 ticks96 per step
// The flip-dot screen picks the latest note held on the punch-in track, maps it
// to one of 24 key positions (the drum octave's twelve, then the melodic
// octave's) and plays that position's animation from the table at 8406c4c8.
// Second-bank presses never reach that list, so stock picks around them.
const u32 kAnimation = 0x8d554ae0;     // (node) -> animation record
// Master parameters: the GUI's sender for each MIX page, (id, value) queued to
// the audio side as messages 0xb3, 0xc6 and 0xa2, and the offset of the
// page's four project words in the active project (`8d584210`). Stock sends
// these from the same main loop as the sequencer tick.
const u32 kProject = 0x8d584210;
const u32 kMasterSenders[3] = {0x8d4dc84c, 0x8d4dc890, 0x8d4dc808};
const u32 kMasterWords[3] = {0x68, 0x78, 0x88};
// The note event the sequencer plays: pitch 255 means no note.
const u32 kPitch = 0xd, kVelocity = 0xf;
const u8 kNoNote = 0xff;
bank::Bank held;           // second-bank keys held
bool page2;                // the punch-in track's keys play the second bank
// While page 2 is on and none of its keys is held, the punch-in track's
// flip-dot screen shows the example's page-2 screen (sdk_punch_page2_screen),
// a dot hopping across as on stock's punch-in screen. While latch is on, every
// screen shown carries the latch mark (with_latch_mark).
// Encoders: the key layer's handler for a button event (`8c0b5d58`, event
// words: code, down) marks an encoder press (codes 10..13) as a pending click
// at `08023c4c` + index, then calls `8d5b8100` (event); on release it plays the
// click if still pending, then calls `8d5b8030` (event). A turn reaches
// `8c0b5c0c` (index 0..3, turn), whose first word is the change, Q31 (1 % a
// detent, more when turned fast), and clears the pending click; punch_hooks.s
// lets the SDK take a turn right after its prologue.
const u32 kEncoderDown = 0x8d5b8100, kEncoderUp = 0x8d5b8030;
const u32 kClickPending = 0x08023c4c;
const i32 kKnobFull = 0x7fffffff, kKnobScale = 4;
u8 encoders_held;          // bit per encoder pressed now
u8 encoders_turned;        // bit per held encoder turned since its press
i32 knob;                  // page 2's intensity knob as the effects see it, Q31
i32 knob_latched;          // what unheld turns left it at; a click returns it to 0
i32 signed_knob, signed_latched;   // the same turns, allowed below 0 too (Context::turn)
// Latch: a click of the rightmost encoder on page 2 turns it on; then a key's
// release is ignored and its next press ends it. Another click, or leaving
// page 2, turns latch off and ends every key.
bool latch;
u32 swallow[4];            // pitches whose next note-off belongs to a latch release
// Stock's own punch-in keys under latch (page 1): their keyboard release is
// held back; pressing the key again, or latch going off, sends stock the
// note-off. A keyboard release carries no event (0, from stock's release
// wrapper 8c090dbc), so sending one later is safe; MIDI releases (an event) pass.
u32 stock_latched[4];
void* stock_sequencer;
bool stock_holds(u32 pitch) { return (stock_latched[pitch / 32] >> (pitch % 32)) & 1u; }
void stock_release(u32 pitch) {
    stock_latched[pitch / 32] &= ~(1u << (pitch % 32));
    stock<u32(void*, u32, u32, const void*)>(kNoteOff)(stock_sequencer, kPunchTrack, pitch, nullptr);
}
void stock_release_all() {
    for (u32 pitch = 0; pitch < 128; ++pitch)
        if (stock_holds(pitch)) stock_release(pitch);
}
bool swallowed(u32 pitch) {
    u32& bits = swallow[pitch / 32];
    const u32 bit = 1u << (pitch % 32);
    if (!(bits & bit)) return false;
    return bits &= ~bit, true;
}
// The knob as a bend size: 0 untouched, its full travel reaching full
// intensity past the dead zone.
i32 knob_bend() { return knob ? punch::intensity_dead_zone + knob / (kKnobFull / punch::intensity_span) : 0; }
// The bend effects see: the larger of the punch-in modulation's size and the knob's.
i32 effective_bend(i32 bend) {
    const i32 size = bend < 0 ? -bend : bend, turned = knob_bend();
    if (!knob) return bend;
    return turned > size ? turned : size;
}
u32 engaged[3][kTracks];   // per callback kind: keys that ran on a track last time

// Notes effects asked to play, played from the start of a sequencer tick.
struct Pending { u32 due, length; u8 track, pitch, velocity, waiting; };
const u32 kPending = 128;
Pending pending[kPending];
u32 last_ticks;
bool playing_generated;    // inside stock's note player for one of our notes

// Master overrides: those set since the last tick, those the audio side holds,
// and per parameter the value set, the value sent and the project's value.
u16 master_set, master_held;
i32 master_wanted[punch::master_count], master_sent[punch::master_count], master_project[punch::master_count];
u32 tick_led;              // effects (by index) that have led since the last round
u32 round_next;            // the track that continues the current 0..7 run of passes
u32 master_moved = ~0u;    // sequencer position of the last change sent to a held value

// After each complete run of per-track passes over tracks 0..7 (stock's own
// per-update pass, or note starts covering every track), every held effect has
// run on its tracks, so the set values are complete. Send each newly set value,
// and each one the project's own value replaced (a knob edit or a project
// publication sends it); send a changed value of one already held at most once
// per sequencer tick. For each one no longer set, send the project's value.
// Project memory is only read.
void master_apply() {
    if (!master_set && !master_held) return;
    const u32 now = word(kTicks96);
    bool moved = false;
    const char* project = stock<const char*()>(kProject)();
    for (u32 i = 0; i < punch::master_count; ++i) {
        const u16 bit = static_cast<u16>(1u << i);
        const u32 page = i / 4, id = i % 4;
        const i32 own = project ? *reinterpret_cast<const volatile i32*>(project + kMasterWords[page] + 4 * id)
                                : master_project[i];
        const bool changed = (master_held & bit) && own != master_project[i];
        master_project[i] = own;
        if (master_set & bit) {
            if ((master_held & bit) && !changed &&
                (master_sent[i] == master_wanted[i] || now == master_moved)) continue;
            moved |= (master_held & bit) && !changed;
            stock<void(u32, i32)>(kMasterSenders[page])(id, master_wanted[i]);
            master_sent[i] = master_wanted[i];
            master_held |= bit;
        } else if (master_held & bit) {
            stock<void(u32, i32)>(kMasterSenders[page])(id, own);
            master_held &= static_cast<u16>(~bit);
        }
    }
    if (moved) master_moved = now;
    master_set = 0;
}

void play_due() {
    const u32 now = word(kTicks96);
    if (now < last_ticks)  // the position went back: drop what was waiting
        for (u32 i = 0; i < kPending; ++i) pending[i].waiting = 0;
    last_ticks = now;
    for (u32 i = 0; i < kPending; ++i) {
        Pending& p = pending[i];
        if (!p.waiting || static_cast<i32>(now - p.due) < 0) continue;
        p.waiting = 0;
        const i64 time = stock<i64()>(kNow)();
        playing_generated = true;
        stock<void(void*, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32)>(kPlay)(
            reinterpret_cast<void*>(kSequencer), p.track, p.pitch, p.length * kPlayTicksPer96, 0, p.velocity, 0, 0,
            static_cast<u32>(time), static_cast<u32>(static_cast<unsigned long long>(time) >> 32), kPlayGenerated, 0);
        playing_generated = false;
    }
}

bool drum(u32 track) { return !stock<u32(u32)>(kMelodic)(track); }

// Runs `call` for every held second-bank key on `track`, with its context.
template <class Call> bool each(u32 kind, u32 track, i32 bend, Call call) {
    held.expire(word(kTicks96));
    return bank::run(held.keys(), sdk_punch_effects, sdk_punch_effect_count, engaged[kind][track], track,
                     drum(track), word(kTicks96), effective_bend(bend), kind == 0 ? &tick_led : nullptr, call,
                     knob_bend(), signed_knob / (kKnobFull / 1000));
}
}

extern "C" {
void sdk_punch_mask(void* sequencer) __asm__("sdk_punch_mask");
u32 sdk_punch_key_on(void* sequencer, u32 track, u32 pitch, u32 velocity, const void* event) __asm__("sdk_punch_key_on");
u32 sdk_punch_key_off(void* sequencer, u32 track, u32 pitch, const void* event) __asm__("sdk_punch_key_off");
void sdk_punch_shift(u32 held) __asm__("sdk_punch_shift");
void sdk_punch_encoder_down(const u32* event) __asm__("sdk_punch_encoder_down");
void sdk_punch_encoder_up(const u32* event) __asm__("sdk_punch_encoder_up");
u32 sdk_punch_turn(u32 index, const i32* turn) __asm__("sdk_punch_turn");
void sdk_punch_track(void* sequencer, u32 track, i32 bend, void* overrides) __asm__("sdk_punch_track");
void sdk_punch_note(void* sequencer, u32 track, u32 unused, u8* event, u32 generated) __asm__("sdk_punch_note");
u32 sdk_punch_step(void* sequencer, u32 track, i32* step) __asm__("sdk_punch_step");
void sdk_punch_play(u32 track, u8 pitch, u8 velocity, u32 length, u32 delay) __asm__("sdk_punch_play");
void sdk_punch_master(u32 parameter, i32 value) __asm__("sdk_punch_master");
i32 sdk_punch_master_project(u32 parameter) __asm__("sdk_punch_master_project");
void sdk_punch_aux(u32 parameter, i32 value) __asm__("sdk_punch_aux");
void sdk_punch_track_set(u32 track, u32 parameter, i32 value) __asm__("sdk_punch_track_set");
i32 sdk_punch_track_own(u32 track, u32 parameter) __asm__("sdk_punch_track_own");
i32 sdk_punch_live(u32 track, u32 parameter) __asm__("sdk_punch_live");
void sdk_punch_filter(u32 track, u32 type) __asm__("sdk_punch_filter");
bool sdk_punch_key_held(u32 key) __asm__("sdk_punch_key_held");
const punch::Animation* sdk_punch_animation(void* node) __asm__("sdk_punch_animation");
}

void sdk_punch_override(u32 track, u32 parameter, i32 value, void* store) {
    if (track < kTracks && parameter < 58) stock<void(u32, u32, i32, void*)>(kOverride)(track, parameter, value, store);
}

void sdk_punch_play(u32 track, u8 pitch, u8 velocity, u32 length, u32 delay) {
    if (track >= kTracks || pitch > 127) return;
    for (u32 i = 0; i < kPending; ++i) {
        if (pending[i].waiting) continue;
        pending[i] = Pending{word(kTicks96) + delay, length ? length : 1, static_cast<u8>(track), pitch,
                             static_cast<u8>(velocity & 0x7f), 1};
        return;
    }
}

i32 sdk_punch_master_project(u32 parameter) {
    if (parameter >= punch::master_count) return 0;
    const char* project = stock<const char*()>(kProject)();
    return project ? *reinterpret_cast<const volatile i32*>(project + kMasterWords[parameter / 4] + 4 * (parameter % 4))
                   : master_project[parameter];
}

// Aux 5's live words: stock's LIVE-only setter `8d5acea8` (parameters, track,
// word, value) publishes one to the audio side; the live array is at
// parameters + 0x4080, 58 words a track (the live input's monitor uses the
// same path for track 12).
const u32 kLiveParameters = 0x86bd027c, kLiveSet = 0x8d5acea8, kAuxTrack = 12, kTrackWords = 58;
// Overridden words of tracks 8..15, each kept with its own value while held.
struct LiveWord { u8 track, parameter, set, held; i32 wanted, own; };
const u32 kLiveWords = 8;
LiveWord live_words[kLiveWords];
u32 live_address(u32 track, u32 parameter) { return kLiveParameters + 0x4080 + (track * kTrackWords + parameter) * 4; }
LiveWord* live_word(u32 track, u32 parameter, bool add) {
    LiveWord* free = nullptr;
    for (LiveWord& w : live_words) {
        if ((w.set || w.held) && w.track == track && w.parameter == parameter) return &w;
        if (!w.set && !w.held && !free) free = &w;
    }
    if (!add || !free) return nullptr;
    free->track = static_cast<u8>(track), free->parameter = static_cast<u8>(parameter);
    return free;
}
// Track filters (M3): stock's GUI sender `8d4dc6dc` (track, type 0..18, on)
// keeps each track's last value at `840396d4` + 4 * track (the type, or the
// type - 100 while off) and, when it changes, queues message 0xa5 (track,
// type, on) for the audio side, which sets the type and on byte the voice
// renderer reads (`8d50ebd2`). The project is never written.
const u32 kFilterSend = 0x8d4dc6dc, kFilterSent = 0x840396d4, kFilterTypes = 19;
struct TrackFilter { u8 wanted, set, held; i32 own; };
TrackFilter filters[kTracks];
bool sdk_punch_key_held(u32 key) { return key < bank::kKeys && (held.keys() >> key & 1u); }
void sdk_punch_filter(u32 track, u32 type) {
    if (track < kTracks && type < kFilterTypes) filters[track].wanted = static_cast<u8>(type), filters[track].set = 1;
}
// After each full pass: switches what effects set, and gives each track back
// its own filter (as stock last sent it) once nobody sets it.
void filter_apply() {
    for (u32 track = 0; track < kTracks; ++track) {
        TrackFilter& f = filters[track];
        const i32 sent = static_cast<i32>(word(kFilterSent + 4 * track));
        if (f.set) {
            if (!f.held) f.own = sent, f.held = 1;
            if (sent != f.wanted) stock<void(u32, u32, u32)>(kFilterSend)(track, f.wanted, 1);
        } else if (f.held) {
            const bool on = f.own >= 0;
            const i32 type = on ? f.own : f.own + 100;
            if (type >= 0 && type < static_cast<i32>(kFilterTypes))
                stock<void(u32, u32, u32)>(kFilterSend)(track, static_cast<u32>(type), on ? 1 : 0);
            f.held = 0;
        }
        f.set = 0;
    }
}

i32 sdk_punch_live(u32 track, u32 parameter) {
    if (track >= kTracks || parameter >= kTrackWords) return 0;
    return static_cast<i32>(word(kLiveParameters + 0x4080 + (track * kTrackWords + parameter) * 4));
}
void sdk_punch_track_set(u32 track, u32 parameter, i32 value) {
    if (track < kTracks || track >= 16 || parameter >= kTrackWords) return;
    if (LiveWord* w = live_word(track, parameter, true)) w->wanted = value < 0 ? 0 : value, w->set = 1;
}
i32 sdk_punch_track_own(u32 track, u32 parameter) {
    if (track >= 16 || parameter >= kTrackWords) return 0;
    const LiveWord* w = live_word(track, parameter, false);
    return w && w->held ? w->own : static_cast<i32>(word(live_address(track, parameter)));
}
void sdk_punch_aux(u32 parameter, i32 value) { sdk_punch_track_set(kAuxTrack, parameter, value); }
// After each full pass: sends what effects set (again when stock rewrote it),
// and gives back the value from before once nobody sets it.
void aux_apply() {
    for (LiveWord& w : live_words) {
        if (!w.set && !w.held) continue;
        const i32 current = static_cast<i32>(word(live_address(w.track, w.parameter)));
        if (w.set) {
            if (!w.held) w.own = current, w.held = 1;
            if (current != w.wanted) stock<void(u32, u32, u32, i32)>(kLiveSet)(kLiveParameters, w.track, w.parameter, w.wanted);
        } else {
            stock<void(u32, u32, u32, i32)>(kLiveSet)(kLiveParameters, w.track, w.parameter, w.own);
            w.held = 0;
        }
        w.set = 0;
    }
}

void sdk_punch_master(u32 parameter, i32 value) {
    if (parameter >= punch::master_count) return;
    master_wanted[parameter] = value < 0 ? 0 : value;
    master_set |= static_cast<u16>(1u << parameter);
}

// Stock builds the held-effect mask once or more per sequencer update; notes
// effects queued fall due from here.
void sdk_punch_mask(void* sequencer) {
    stock<void(void*)>(kBuildMask)(sequencer);
    play_due();
}

// Shift, pressed or released, from the top-level button dispatcher before any
// view sees it. A press while the punch-in track is selected turns page 2 on
// or off; turning it off also ends the keys still latched (an effect with a
// release quantum runs on to it), so nothing keeps playing unseen.
void sdk_punch_shift(u32 pressed) {
    stock<void(u32)>(kShiftSet)(pressed);
    if (!pressed || *reinterpret_cast<const volatile u8*>(kSelectedTrack) != kPunchTrack) return;
    page2 = !page2;
    knob = knob_latched = signed_knob = signed_latched = 0;
    if (!page2 && !latch) held.release_all(word(kTicks96), sdk_punch_effects, sdk_punch_effect_count);
}

void toggle_latch() {
    latch = !latch;
    if (latch) return;
    held.release_all(word(kTicks96), sdk_punch_effects, sdk_punch_effect_count);
    stock_release_all();
    knob = knob_latched = signed_knob = signed_latched = 0;
}

// Page 2's intensity knob, on the punch-in track: an encoder turned without
// holding it moves the knob and leaves it there; held and turned it moves it
// for as long as it is held, then the knob returns to where unheld turns left
// it; a click (a press and release without a turn) returns it to 0.
bool on_page2() { return page2 && *reinterpret_cast<const volatile u8*>(kSelectedTrack) == kPunchTrack; }
void sdk_punch_encoder_down(const u32* event) {
    const u32 index = event[0] - 10;
    if (index < 4) {
        encoders_held = static_cast<u8>(encoders_held | 1u << index);
        encoders_turned = static_cast<u8>(encoders_turned & ~(1u << index));
    }
    stock<void(const u32*)>(kEncoderDown)(event);
}
void sdk_punch_encoder_up(const u32* event) {
    const u32 index = event[0] - 10;
    if (index < 4) {
        const u8 bit = static_cast<u8>(1u << index);
        const bool clicked = (encoders_held & bit) && !(encoders_turned & bit);
        encoders_held = static_cast<u8>(encoders_held & ~bit);
        encoders_turned = static_cast<u8>(encoders_turned & ~bit);
        if (clicked && index == 3 && *reinterpret_cast<const volatile u8*>(kSelectedTrack) == kPunchTrack) toggle_latch();
        else if (clicked && on_page2()) knob_latched = signed_latched = 0;
        if (!encoders_held) knob = knob_latched, signed_knob = signed_latched;
    }
    stock<void(const u32*)>(kEncoderUp)(event);
}
// As knob_moved, but between -kKnobFull and kKnobFull, for a centred control.
i32 signed_moved(i32 value, i32 step) {
    const i32 limit = kKnobFull / kKnobScale;
    const i32 change = (step > limit ? limit : step < -limit ? -limit : step) * kKnobScale;
    if (change > 0) return value > kKnobFull - change ? kKnobFull : value + change;
    return value < -kKnobFull - change ? -kKnobFull : value + change;
}
i32 knob_moved(i32 value, i32 step) {
    // A full turn (about 24 detents) from rest to full: stock's 1 % a detent, times 4.
    const i32 limit = kKnobFull / kKnobScale;
    const i32 change = (step > limit ? limit : step < -limit ? -limit : step) * kKnobScale;
    return change > 0 ? (value > kKnobFull - change ? kKnobFull : value + change)
                      : (value < -change ? 0 : value + change);
}
// Every turn on page 2 of the punch-in track moves the knob and never reaches
// stock; a held encoder's also clears the pending click, as stock's turn does,
// so its release plays no click. Every other turn is stock's (returns 0);
// punch_hooks.s then runs the rest of stock's turn.
u32 sdk_punch_turn(u32 index, const i32* turn) {
    if (index >= 4 || !on_page2()) return 0;
    const u8 bit = static_cast<u8>(1u << index);
    if (encoders_held & bit) {
        *reinterpret_cast<volatile u8*>(kClickPending + index) = 0;
        encoders_turned = static_cast<u8>(encoders_turned | bit);
        knob = knob_moved(knob, turn[0]);
        signed_knob = signed_moved(signed_knob, turn[0]);
    } else {
        knob_latched = knob_moved(knob_latched, turn[0]);
        signed_latched = signed_moved(signed_latched, turn[0]);
        if (!encoders_held) knob = knob_latched, signed_knob = signed_latched;
    }
    return 1;
}

// A live note-on for the selected track. On the punch-in track with page 2
// on, a key an effect plays is latched into the second bank and stock never
// sees it: no stock effect, no recording, no held note.
u32 sdk_punch_key_on(void* sequencer, u32 track, u32 pitch, u32 velocity, const void* event) {
    if (latch && track == kPunchTrack && pitch < 128 && stock_holds(pitch)) {   // a latched stock key pressed again
        stock_release(pitch);
        swallow[pitch / 32] |= 1u << (pitch % 32);
        return 0;
    }
    if (track == kPunchTrack && pitch < 128 && page2) {
        u8 to_track = static_cast<u8>(track), to_pitch = static_cast<u8>(pitch);
        stock<void(void*, u8*, u8*, u32)>(kRemap)(sequencer, &to_track, &to_pitch, 0);
        if (latch && held.holds(static_cast<u8>(pitch))) {   // a latched key pressed again ends it
            held.release(static_cast<u8>(pitch), word(kTicks96), sdk_punch_effects, sdk_punch_effect_count);
            swallow[pitch / 32] |= 1u << (pitch % 32);
            if (!held.pressed()) knob = knob_latched = signed_knob = signed_latched = 0;
            return 0;
        }
        const i32 key = bank::take(true, to_pitch, sdk_punch_effects, sdk_punch_effect_count);
        if (key >= 0 && held.press(static_cast<u8>(pitch), static_cast<u8>(key))) return 0;
    }
    return stock<u32(void*, u32, u32, u32, const void*)>(kNoteOn)(sequencer, track, pitch, velocity, event);
}

// A live note-off for the selected track: the release of a latched key ends
// its effect, whatever track is selected now and whether shift is held; an
// effect with a release quantum runs on to the next multiple of it.
u32 sdk_punch_key_off(void* sequencer, u32 track, u32 pitch, const void* event) {
    if (pitch < 128 && swallowed(pitch)) return 0;
    if (pitch < 128 && latch && held.holds(static_cast<u8>(pitch))) return 0;   // latched: it sticks
    if (pitch < 128 && held.release(static_cast<u8>(pitch), word(kTicks96), sdk_punch_effects, sdk_punch_effect_count)) {
        if (!held.pressed()) knob = knob_latched = signed_knob = signed_latched = 0;   // the next press starts from rest
        return 0;
    }
    if (latch && track == kPunchTrack && pitch < 128 && !event) {   // a stock key under latch sticks
        stock_latched[pitch / 32] |= 1u << (pitch % 32), stock_sequencer = sequencer;
        return 0;
    }
    return stock<u32(void*, u32, u32, const void*)>(kNoteOff)(sequencer, track, pitch, event);
}

// Stock's per-track parameter pass, then the effects' tick on the same overrides.
void sdk_punch_track(void* sequencer, u32 track, i32 bend, void* overrides) {
    stock<void(void*, u32, i32, void*)>(kTrackPass)(sequencer, track, bend, overrides);
    if (track >= kTracks) return;
    const punch::Overrides out{track, overrides};
    each(0, track, bend, [&](const punch::Effect& effect, const punch::Context& context) {
        if (effect.tick) effect.tick(context, out);
        return false;
    });
    round_next = track == round_next ? track + 1 : track == 0 ? 1 : 0;
    if (round_next == kTracks) {
        master_apply();
        aux_apply();
        filter_apply();
        tick_led = 0;
        round_next = 0;
    }
}

// Stock's per-note pass over a note about to play, then the effects' note.
void sdk_punch_note(void* sequencer, u32 track, u32 unused, u8* event, u32 generated) {
    stock<void(void*, u32, u32, u8*, u32)>(kNotePass)(sequencer, track, unused, event, generated);
    if (track >= kTracks || event[kPitch] == kNoNote) return;
    const i32 bend = stock<i32()>(kBend)();
    each(1, track, bend, [&](const punch::Effect& effect, const punch::Context& context) {
        if (!effect.note || event[kPitch] == kNoNote) return false;
        punch::Note note{event[kPitch], event[kVelocity], generated != 0 || playing_generated};
        if (!effect.note(context, note)) event[kPitch] = kNoNote;
        else event[kPitch] = note.pitch & 0x7f, event[kVelocity] = note.velocity & 0x7f;
        return false;
    });
}

// Stock's step remap for the track player, then the effects' step.
u32 sdk_punch_step(void* sequencer, u32 track, i32* step) {
    u32 remapped = stock<u32(void*, u32, i32*)>(kStepRemap)(sequencer, track, step);
    if (track >= kTracks) return remapped;
    const i32 bend = stock<i32()>(kBend)();
    if (each(2, track, bend, [&](const punch::Effect& effect, const punch::Context& context) {
            return effect.step ? effect.step(context, *step) : false;
        }))
        remapped = 1;
    return remapped;
}

// Stock's animation choice, replaced by the latest second-bank key's own while
// it is held.
extern "C" const punch::Animation* const sdk_punch_page2_screen;   // the example's strip
const punch::Animation* animation_for(void* node) {
    const punch::Animation* chosen = stock<const punch::Animation*(void*)>(kAnimation)(node);
    const i32 key = held.latest();
    if (key < 0) return page2 ? sdk_punch_page2_screen : chosen;
    const u32 index = bank::effect_index(sdk_punch_effects, sdk_punch_effect_count, static_cast<u32>(key));
    const punch::Animation* own = index < sdk_punch_effect_count ? sdk_punch_effects[index]->animation : nullptr;
    return own ? own : chosen;
}

// The latch mark, an L of three dots in the top right, drawn into a copy of
// whatever animation is shown (frames are read-only). The copy is redone when
// the animation or its frames change; unused frame slots repeat frame 0.
const u32 kMarkedFrames = 48;
u8 marked[kMarkedFrames][punch::flipdot_frame_bytes];
const u8* marked_frames[kMarkedFrames];
punch::Animation marked_view{marked_frames, 0, 1};
const punch::Animation* marked_source;
const u8* const* marked_source_frames;
u32 marked_source_count;
void mark_dot(u8* frame, u32 x, u32 y) { frame[y * (punch::flipdot_columns / 8) + x / 8] |= static_cast<u8>(1u << (x % 8)); }
const punch::Animation* with_latch_mark(const punch::Animation* shown) {
    if (!latch || !shown || !shown->frames || !shown->count) return shown;
    if (shown != marked_source || shown->frames != marked_source_frames || shown->count != marked_source_count) {
        const u32 count = shown->count < kMarkedFrames ? shown->count : kMarkedFrames;
        for (u32 i = 0; i < count; ++i) {
            for (u32 b = 0; b < punch::flipdot_frame_bytes; ++b) marked[i][b] = shown->frames[i][b];
            mark_dot(marked[i], 37, 1), mark_dot(marked[i], 37, 2), mark_dot(marked[i], 38, 2);
        }
        for (u32 i = 0; i < kMarkedFrames; ++i) marked_frames[i] = marked[i < count ? i : 0];
        marked_view.count = count;
        marked_source = shown, marked_source_frames = shown->frames, marked_source_count = shown->count;
    }
    return &marked_view;
}
const punch::Animation* sdk_punch_animation(void* node) { return with_latch_mark(animation_for(node)); }
