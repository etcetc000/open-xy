// Send-effect idling: a send whose effect has nothing to play skips it.
//
// Stock OS 1.2.1, audio side. The audio loop (8d510760) clears both sends'
// buses (84caa140, 84caa340: 64 interleaved stereo frames each), sums the
// tracks' sends into them, then runs each send through 8d50f594(engine, track,
// bus): from 8d5116a0 for track 14, and from 8d511908 for track 15 after
// adding track 14's result to its bus by track 14's word 23. That call, when
// the send has an effect object (11b01d88 + 4 track), copies the bus into the
// left and right scratch buffers (84ca9f40, 84caa040) and meters it
// (telemetry 0x1a for 14, 0x1b for 15); runs the object's process (+0x10) in
// place there with the track's words (84eab39c + 0xe8 track); applies the
// send's post filter while its flag byte (11b01dc8 + track) is set; fades the
// send's gain (8404caf8 + 4 track) towards full while the requested effect
// (84042700 + 4 track) is the current one (8404cb38 + 4 track), towards zero
// otherwise (the loop builds a requested effect in the slot once that gain is
// near zero); writes its level and pan matrix of the result back into the bus
// and meters that (0x1c, 0x1d). The loop then adds both buses to the master
// mix. Without an object it returns at once, and the bus passes as it is.
// Stock delay, reverb and chorus keep their lines in the send's scratchpad
// (8d50e048), at most 512 KiB a request; the delay reads the audio clock's
// tempo (84c69828), and SDK effects with a transport its BPM (84c69824) and
// the playing bytes (860c9b39).
//
// Both calls come here. A send idles, skipping the call and leaving silence in
// its bus, after its hold (below) of blocks in a row in which:
// - its object exists, is the requested effect, and its gain is full;
// - no engine reset is pending: the engine's first byte, which stock's reset
//   (8d50f23c, at the end of the same block) clears when it calls every
//   object's init;
// - its input, the effect's output and the bus's result all stayed below
//   kFloor, one floor in raw bus units for every effect, whatever its bus
//   shift: all four tests (the input, which also wakes an idle send, the
//   effect's left and right output, which a bus-shifted effect's adapter has
//   already lowered, and the result) read the raw bus scale. The send bus runs
//   far below Q31 full scale, so a floor relative to full scale would lie inside
//   its normal signal range and idle quietly sounding sends. kFloor = 2^6 (64
//   raw, about -150 dBFS): below it a send carries only rounding residue. It is
//   chosen, not derived: well under the send levels the author read on one
//   OP-XY, and high enough that the small residue a stock effect was seen to
//   leave at rest (in a simulator outside this repository) idles; both are the
//   author's observations, which this repository cannot reproduce;
// - nothing else the call reads changed: the track's words, both tempo
//   words, the playing bytes and the post filter flag.
// The hold is the effect's (send_bus.hpp): what its tail needs, from what it
// declares or from its memory read as a 16-bit mono line, plus 64 blocks; for
// stock's effects, 4096 + 64 blocks, a 512 KiB request read the same way. So an
// output that stayed below the floor for all of it leaves its lines below the
// floor too: idling cuts nothing that stock would still play, and parameter
// smoothing has settled. An effect with an unbounded tail never idles. The
// first block in which any of those
// conditions fails runs the call again, on that block and on the effect's
// state as it was, so a send resumes with the input that wakes it. On idling,
// the send's two meters are published as zero, where stock's would decay.
#include "idle_cut.hpp"
#include "send_bus.hpp"
#include "stock.hpp"

extern "C" void sdk_send_fx(opxy::u32 engine, opxy::u32 track, opxy::i32* bus) __asm__("sdk_send_fx");
// Each send's bus-shifted effect, as its init last recorded it (send_bus.hpp).
opxy::platform::send_bus::Slot sdk_send_bus[opxy::platform::send_bus::sends];

namespace {
using namespace opxy;
using namespace opxy::platform;

const u32 kProcessSend = 0x8d50f594;   // (engine, track, bus)
const u32 kPublish = 0x8d4d666a;       // telemetry (id, value)
const u32 kObjects = 0x11b01d88;       // effect object per track
const u32 kRequested = 0x84042700;     // requested effect type per track
const u32 kCurrent = 0x8404cb38;       // current effect type per track
const u32 kGain = 0x8404caf8;          // Q31 fade gain per track
const u32 kFilterFlags = 0x11b01dc8;   // post filter flag byte per track
const u32 kLeft = 0x84ca9f40;          // scratch: the effect's left output
const u32 kRight = 0x84caa040;         // and right
const u32 kTrackWords = 0x84eab39c;    // the audio side's track words
const u32 kTrackStride = 0xe8;
const u32 kTempo = 0x84c69824;         // two words: BPM, and the delay's tempo
const u32 kPlaying = 0x860c9b39;       // two bytes
const u32 kFirstSend = 14;
const u32 kSends = 2;
const u32 kFrames = 64;
const u32 kInputMeter = 0x1a;          // + send; the output meter is + 2
const i32 kFull = 0x7fffffff;
// The idle floor in raw bus units, for every effect (above).
const u32 kFloor = 1u << 6;
const u32 kWords = kTrackStride / 4;

// What the call reads besides audio, as the send last saw it.
struct Inputs {
    u32 words[kWords];
    u32 tempo[2];
    u32 flags;  // the two playing bytes and the post filter flag
};

struct Send {
    u32 quiet;  // blocks in a row that met every condition
    bool idle;
    Inputs seen;
};

Send sends[kSends];

// The largest magnitude of Count words, -2^31 counting as 2^31 - 1:
// idle_cut::peak's result. On the Blackfin one hardware loop of two instructions a
// word, the saturating ABS of a word issued with the load of the next, then the
// running MAX; it reads exactly the Count words and has no exit. (GCC's form of
// idle_cut::peak takes three: it does not pair the load with the ABS.)
// Count is a constant of at least 2: the loop runs Count - 1 passes after the first load,
// so 1 would judge a word past the buffer and 0 would run 2^32 passes.
template <u32 Count> inline u32 loudest(const i32* samples) {
    static_assert(Count >= 2, "the scan reads at least two words");
#if defined(__bfin__)
    i32 most = 0, word, magnitude;
    const i32* next = samples;
    __asm__ __volatile__(
        "%[word] = [%[next]++];\n\t"
        "LSETUP (1f, 2f) LC0 = %[loops];\n"
        "1:\t%[magnitude] = ABS %[word] || %[word] = [%[next]++] || NOP;\n"
        "2:\t%[most] = MAX (%[most], %[magnitude]);\n\t"
        "%[magnitude] = ABS %[word];\n\t"
        "%[most] = MAX (%[most], %[magnitude]);"
        : [most] "+d"(most), [word] "=&d"(word), [magnitude] "=&d"(magnitude), [next] "+a"(next)
        : [loops] "a"(Count - 1)
        : "LC0", "LT0", "LB0", "ASTAT", "memory");
    return static_cast<u32>(most);
#else
    return static_cast<u32>(idle_cut::peak(samples, Count));
#endif
}

// Every sample of the block within -(floor - 1)..floor - 1, that is, its largest
// magnitude below the floor (a buffer is read whole: on the near-silent blocks
// before a send idles every word is under the floor anyway).
template <u32 Count> bool quiet(const i32* samples, u32 floor) { return loudest<Count>(samples) < floor; }

// The send's hold: the stock hold, or, while the slot still holds the effect
// whose init recorded it, that effect's own hold.
u32 hold_of(u32 track, u32 index) {
    const platform::send_bus::Slot& bus = sdk_send_bus[index];
    const void* vtable = bus.vtable;
    const u32 object = *at<const volatile u32>(kObjects + track * 4);
    if (!vtable || !object || *at<const void* const volatile>(object) != vtable)
        return platform::send_bus::stock_hold;
    return bus.hold;
}

const volatile u32* track_words(u32 track) { return at<const volatile u32>(kTrackWords + track * kTrackStride); }

u32 flags(u32 track) {
    const volatile unsigned char* playing = at<const volatile unsigned char>(kPlaying);
    return playing[0] | playing[1] << 8 | *at<const volatile unsigned char>(kFilterFlags + track) << 16;
}

bool unchanged(const Inputs& seen, u32 track) {
    const volatile u32* words = track_words(track);
    for (u32 i = 0; i < kWords; ++i)
        if (words[i] != seen.words[i]) return false;
    const volatile u32* tempo = at<const volatile u32>(kTempo);
    return tempo[0] == seen.tempo[0] && tempo[1] == seen.tempo[1] && flags(track) == seen.flags;
}

void remember(Inputs& seen, u32 track) {
    const volatile u32* words = track_words(track);
    for (u32 i = 0; i < kWords; ++i) seen.words[i] = words[i];
    const volatile u32* tempo = at<const volatile u32>(kTempo);
    seen.tempo[0] = tempo[0];
    seen.tempo[1] = tempo[1];
    seen.flags = flags(track);
}

// The object exists and is settled: nothing is being built, faded or reset.
bool steady(u32 engine, u32 track) {
    const u32 offset = track * 4;
    return *at<const volatile u32>(kObjects + offset) != 0 &&
           *at<const volatile u32>(kRequested + offset) == *at<const volatile u32>(kCurrent + offset) &&
           *at<const volatile i32>(kGain + offset) == kFull &&
           *at<const volatile unsigned char>(engine) == 0;
}
}

void sdk_send_fx(u32 engine, u32 track, i32* bus) {
    typedef void Process(u32, u32, i32*);
    const u32 index = track - kFirstSend;
    if (index >= kSends) {
        stock<Process>(kProcessSend)(engine, track, bus);
        return;
    }
    Send& send = sends[index];
    const u32 hold = hold_of(track, index);
    if (hold == platform::send_bus::endless) {
        // An effect with an unbounded tail never idles: no scan can change that, so
        // none runs. Its count stays 0 and an idle send wakes, as below. The count
        // could matter only if the slot's hold became finite without a block that
        // breaks the conditions, and that takes another effect object or an engine
        // reset (the effect's init records the hold), which stock reaches only
        // through a fading gain, a requested effect or the reset byte: blocks that
        // set the count to 0 in any case.
        send.idle = false;
        send.quiet = 0;
        stock<Process>(kProcessSend)(engine, track, bus);
        return;
    }
    const bool silent = steady(engine, track) && quiet<2 * kFrames>(bus, kFloor);
    if (send.idle) {
        if (silent && unchanged(send.seen, track)) {
            for (u32 i = 0; i < 2 * kFrames; ++i) bus[i] = 0;
            return;
        }
        // Wake on this block, with the effect's state as it was.
        send.idle = false;
        send.quiet = 0;
    }
    stock<Process>(kProcessSend)(engine, track, bus);
    if (!silent || !quiet<kFrames>(at<const i32>(kLeft), kFloor) ||
        !quiet<kFrames>(at<const i32>(kRight), kFloor) || !quiet<2 * kFrames>(bus, kFloor)) {
        send.quiet = 0;
        return;
    }
    if (!send.quiet || !unchanged(send.seen, track)) {
        remember(send.seen, track);
        send.quiet = 1;
        return;
    }
    if (++send.quiet < hold) return;
    send.idle = true;
    typedef u32 Publish(u32, u32);
    stock<Publish>(kPublish)(kInputMeter + index, 0);
    stock<Publish>(kPublish)(kInputMeter + 2 + index, 0);
}
