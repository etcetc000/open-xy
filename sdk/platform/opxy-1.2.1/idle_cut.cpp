// Idle cut on the DSP board (idle_cut.hpp): the project's idle threshold for voices
// stock's envelope floor does not reach. One call replaced by a call:
//
// - 8d51c7a6, in the track's voice loop (8d51c708), its CALL 8d51bbaa: the
//   renderer chooser (the normal renderer 11a09308, or the drum renderer 8d51b274
//   for a voice whose drum byte +0x4e is set). sdk_idle_render runs it, then, for
//   a voice it did not finish, asks whether nothing can make the voice louder
//   again and it has fallen below the threshold; if so, stock's own kill
//   (8d51a998: gate off, amp and filter envelopes to stage 6) fades it over its
//   next block and the loop retires it after that (stage 7, gate off, at 8d51b1ec),
//   as stock does a choke.
//
// Voices it asks about:
// - Stock's drum sampler (engine 3, method table 8404d648): its sample's table
//   (idle_cut.hpp), while the note is not held.
// - SDK synths whose engine has a `settled` hook (voice.cpp.in), once the note is
//   released (gate +0x51 off): the adapter's own follower.
// SDK drum kits cut their own voices (drum_voice.cpp.in, the same hook and a choke
// fade). Stock's normal renderer's released notes keep stock's floor. Stock
// Drumline (engine 4) is not cut: its eleven synthesis routines give no quantity
// that can only fall (docs/abi-1.2.1.md "Idle cut").
//
// The threshold is the release_floor setting's: probed once from stock's envelope
// routine (idle_cut.hpp, probe_floor). Without the setting nothing here acts
// beyond a compare a voice.
#include "idle_cut.hpp"
#include "stock.hpp"

using namespace opxy;
using namespace opxy::platform;
namespace cut = opxy::platform::idle_cut;

extern "C" {
int sdk_idle_render(unsigned char* voice, i32* output) __asm__("sdk_idle_render");
i32 sdk_idle_floor() __asm__("sdk_idle_floor");
bool sdk_idle_register(const void* methods, int (*settled)(void*)) __asm__("sdk_idle_register");
}

namespace {
// Stock voice fields (docs/abi-1.2.1.md "Stock voice allocation", "Synthesis drum transport").
const u32 kRegion = 0x10, kStage = 0x24, kDrum = 0x4e, kNote = 0x50, kGate = 0x51, kTrack = 0x55, kHandle = 0x94;
const u32 kFreeNote = 0xff;
// Stock's drum sampler (its method table, and its object's fields, 11a07c2c).
const u32 kSamplerMethods = 0x8404d648;
const u32 kSample = 0x04, kPosition = 0x18, kStart = 0x20, kLevel = 0x34, kState = 0x3c,
          kCount = 0x40, kReverse = 0x45;
// Its sample object: PCM, frames, stereo.
const u32 kPcm = 0x08, kFrames = 0x30, kStereo = 0x3c;
// The pad's words: play mode 3, level 7 (Q27); the track's play mode, word 3.
const u32 kModeWord = 0xc, kPadLevel = 0x1c;
const u32 kFrameIndex = 0x84a50b98;  // u16, counts audio blocks (stock.hpp)

i32 floor_q31 = -1;  // -1 until probed
u32 last_block = 0x10000, budget, now;

u32 field(const unsigned char* p, u32 offset) { return *reinterpret_cast<const u32*>(p + offset); }

i32 floor_now() {
    if (floor_q31 < 0) {
        floor_q31 = cut::probe_floor([](i32 level) {
            u32 envelope[8] = {0, static_cast<u32>(level), 0, 0, 4, 0, 0, 0};
            stock<void(u32*, u32, u32, i32, u32)>(0x8d50e110)(envelope, 0, 0, 0, 0);
            return envelope[4];
        });
    }
    return floor_q31;
}

void kill(unsigned char* voice) { stock<void(unsigned char*)>(0x8d51a998)(voice); }

// --- Stock's drum sampler ---
cut::Table tables[cut::kSlots];
// What the cut follows per voice, by the voice's address.
struct Followed { unsigned char* voice; cut::Track track; };
const u32 kFollowed = 64;
Followed followed[kFollowed];

cut::Track* track_for(unsigned char* voice) {
    const u32 home = static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(voice) >> 4) % kFollowed;
    for (u32 i = 0; i < kFollowed; ++i) {
        Followed& f = followed[(home + i) % kFollowed];
        if (f.voice == voice) return &f.track;
        if (!f.voice) {
            f.voice = voice;
            f.track = cut::Track();
            return &f.track;
        }
    }
    return nullptr;
}

cut::Table* table_for(const i16* pcm, u32 frames, u32 stereo, bool note_start) {
    cut::Table* oldest = &tables[0];
    for (u32 i = 0; i < cut::kSlots; ++i) {
        cut::Table& t = tables[i];
        if (t.pcm && cut::matches(t, pcm, frames, stereo)) {
            // Another sample loaded where this one was, as long: scan it again.
            if (note_start && t.signature != cut::signature(pcm, frames, stereo)) cut::begin(t, pcm, frames, stereo, now);
            t.used = now;
            return &t;
        }
        if (!t.pcm || now - t.used > now - oldest->used) oldest = &t;
    }
    cut::begin(*oldest, pcm, frames, stereo, now);
    return oldest;
}

bool sampler_idle(unsigned char* voice, unsigned char* sampler, i32 floor) {
    const u32 sample = field(sampler, kSample);
    if (!sample) return false;
    const unsigned char* s = at<const unsigned char>(sample);
    const u32 pcm = field(s, kPcm), frames = field(s, kFrames);
    if (!pcm || !frames) return false;
    cut::Track* k = track_for(voice);
    if (!k) return false;
    cut::Sampler v;
    v.state = field(sampler, kState);
    v.count = field(sampler, kCount);
    v.reverse = sampler[kReverse] != 0;
    v.position = field(sampler, kPosition) >> 11;
    v.start = field(sampler, kStart) >> 11;
    const unsigned char* region = field(voice, kRegion) ? at<const unsigned char>(field(voice, kRegion)) : nullptr;
    v.mode = 3;  // without its pad, as if it may loop
    if (region) {
        const unsigned char* track = stock<unsigned char*(u32, u32)>(0x8d518d94)(0x84eab39c, voice[kTrack]);
        v.mode = cut::sampler_mode(track ? static_cast<i32>(field(track, kModeWord)) : 0x40000000,
                                   static_cast<i32>(field(region, kModeWord)));
    }
    cut::Table* t = table_for(at<const i16>(pcm), frames, s[kStereo], cut::new_note(*k, v));
    if (!t->complete && budget) budget = cut::scan(*t, budget);
    if (!region) return false;
    v.level = static_cast<i32>(field(sampler, kLevel));
    v.pad_level = static_cast<i32>(field(region, kPadLevel));
    return cut::sampler_cut(*k, v, *t, floor);
}

// --- SDK synths with a settled hook ---
struct Settles { const void* methods; int (*settled)(void*); };
const u32 kSettles = 16;
Settles settles[kSettles];
u32 registered;
}

i32 sdk_idle_floor() { return floor_now(); }

bool sdk_idle_register(const void* methods, int (*settled)(void*)) {
    for (u32 i = 0; i < registered; ++i)
        if (settles[i].methods == methods) return true;
    if (registered == kSettles) return false;
    settles[registered++] = {methods, settled};
    return true;
}

int sdk_idle_render(unsigned char* voice, i32* output) {
    const int done = stock<int(unsigned char*, i32*)>(0x8d51bbaa)(voice, output);
    if (done) return done;
    const i32 floor = floor_now();
    if (!floor) return 0;
    const u32 block = *at<const volatile u16>(kFrameIndex);
    if (block != last_block) {
        last_block = block;
        budget = cut::kScanBudget;
        ++now;
    }
    const u32 stage = field(voice, kStage);
    if (stage == 0 || stage == 6 || stage == 7 || voice[kNote] == kFreeNote) return 0;
    const u32 handle = field(voice, kHandle);
    if (!handle) return 0;
    unsigned char* object = at<unsigned char>(handle);
    const u32 methods = field(object, 0);
    if (voice[kDrum]) {
        if (methods == kSamplerMethods && sampler_idle(voice, object, floor)) kill(voice);
        return 0;
    }
    if (voice[kGate]) return 0;  // a held synth note is never cut
    for (u32 i = 0; i < registered; ++i)
        if (reinterpret_cast<__UINTPTR_TYPE__>(settles[i].methods) == static_cast<__UINTPTR_TYPE__>(methods)) {
            if (settles[i].settled(object)) kill(voice);
            break;
        }
    return 0;
}
