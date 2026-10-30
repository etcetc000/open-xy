// Stock OS 1.2.1 side of sample assignment for owned pads: the header worker's
// frame-count hook, the region initializer hooks of file loads and completed
// recordings, and the retry decision. Stock calls the three hooks with the
// ordinary C ABI (each replaces a CALL to the function it forwards to); the
// retry stubs and the owned initializer call are in pad_assignment.s.
#include "sample_machines.hpp"
#include "stock_sample.hpp"
#include "stock.hpp"
#include "barrier.hpp"

extern "C" {
// The hooked stock key lookup: canonical pad for an owned drum page, else stock's.
opxy::u32 extensions_sample_key(opxy::u32, opxy::u32) __asm__("extensions_sample_key");
opxy::u32 sdk_sample_assignment(opxy::u32, opxy::u32, opxy::u32, const opxy::u32*, opxy::u32, bool)
    __asm__("sdk_sample_assignment");
opxy::u32 sdk_sample_assign_owned(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_sample_assign_owned");
opxy::u32 sdk_sample_assignment_frames(opxy::u32) __asm__("sdk_sample_assignment_frames");
opxy::u32 sdk_sample_assign_region(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_sample_assign_region");
opxy::u32 sdk_sample_record_region(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_sample_record_region");
opxy::u32 sdk_sample_retry(opxy::u32, opxy::u32) __asm__("sdk_sample_retry");
}

namespace {
using opxy::u32;
using namespace opxy::platform;
const u32 kJob = 0x0801f440;                // header worker job: +0 raw key, +4 track, +12 generation
const u32 kActiveGeneration = 0x0801f44c;
const u32 kPendingGeneration = 0x0801f2e8;
const u32 kActivePath = 0x0801f424;         // the worker's own path, never the browser's pending choice
const u32 kWorkerBusy = 0x0801f404;         // byte
const u32 kInitializeRegion = 0x8d589b8c;   // (track, key, ...) initializes a new pad region
const u32 kStockRate = 44100;   // the recorder's rate; assumed without verified header metadata
bool owned(u32 track) { return track < kTracks && pad_settings(track_engine(track)); }

// The header worker's metadata for the job it just read, consumed once by the
// region initializer of the same job. valid is published last. Pad settings
// are read at commit time so edits made during loading are not lost.
struct Hint { u32 valid, rate, track, key, generation, frames; };
volatile Hint hint;

u32 initialize_stock(u32 track, u32 key, u32 third, u32 fourth, u32 fifth, u32 frames, u32 last) {
    return stock<u32(u32, u32, u32, u32, u32, u32, u32)>(kInitializeRegion)(track, key, third, fourth, fifth, frames, last);
}

// Initializes an owned pad's region with the settings the machine chooses.
u32 assign_owned(u32 track, u32 key, u32 frames, u32 last, u32 rate, const u32* path, bool recorded) {
    void* raw = pad_region(track, key);
    const SampleReplacement previous = sample_replacement(raw);
    const u32 settings = sdk_sample_assignment(track, frames, rate, path, previous.settings, recorded);
    // Stock normalizes its lookup key but stores the input key. Keep the raw
    // note for lookup; the owned fade hook supplies the canonical stored key.
    // Passing the canonical key into stock would apply the octave twice.
    const u32 result = sdk_sample_assign_owned(track, key, settings, frames, last, extensions_sample_key(track, key));
    // No publisher runs inside the stock initializer. Restore saved controls
    // before returning to the stock caller and subsequent GUI publication.
    sample_restore_replacement(raw, previous);
    return result;
}
}

// 8c0a22fe: the header worker holds this reader until its completion is
// queued. Keep stock's full frame count; the rate is another constant-time getter.
u32 sdk_sample_assignment_frames(u32 reader) {
    const u32 frames = stock<u32(u32)>(0x8d4fd436)(reader);
    hint.valid = 0;
    barrier();
    const u32 track = word(kJob + 4), key = word(kJob), generation = word(kJob + 12);
    if (!owned(track)) return frames;
    hint.rate = stock<u32(u32)>(0x8d4fd41c)(reader);
    hint.track = track;
    hint.key = key;
    hint.generation = generation;
    hint.frames = frames;
    barrier();
    hint.valid = 1;
    return frames;
}

// 8c0a23f4: stock initializes a new region for a loaded file. For an owned
// pad, let the machine combine saved settings with metadata from this exact
// job. Stock has replaced the path but not yet reset pad metadata.
u32 sdk_sample_assign_region(u32 track, u32 key, u32 third, u32 fourth, u32 fifth, u32 frames, u32 last) {
    if (!owned(track)) return initialize_stock(track, key, third, fourth, fifth, frames, last);
    u32 rate = kStockRate;
    const u32* path = nullptr;
    if (hint.valid == 1) {
        barrier();
        hint.valid = 0;
        if (hint.track == track && hint.key == key && hint.frames == frames &&
            word(kActiveGeneration) == hint.generation && word(kPendingGeneration) == hint.generation) {
            rate = hint.rate;
            path = reinterpret_cast<const u32*>(kActivePath);
        }
    }
    return assign_owned(track, key, frames, last, rate, path, false);
}

// 8c0a4440: the recorder initializes the region of its completed file at the
// raw target key. It records at 44.1 kHz and has no header job, so the
// worker's hint is neither used nor consumed, even if key and frames match.
u32 sdk_sample_record_region(u32 track, u32 key, u32 third, u32 fourth, u32 fifth, u32 frames, u32 last) {
    if (!owned(track)) return initialize_stock(track, key, third, fourth, fifth, frames, last);
    return assign_owned(track, key, frames, last, kStockRate, nullptr, true);
}

// Retry stubs: stock's path-equality result, cleared when an owned pad on the
// selected track may retry. Active: the active and pending generations differ.
// Pending: the worker is idle.
u32 sdk_sample_retry(u32 equal, u32 pending) {
    if (!equal) return 0;
    if (pending ? byte(kWorkerBusy) != 0 : word(kActiveGeneration) == word(kPendingGeneration)) return equal;
    return owned(byte(kSelectedTrack)) ? 0 : equal;
}
