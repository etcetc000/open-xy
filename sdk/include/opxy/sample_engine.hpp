#pragma once
#include <opxy/audio.hpp>
#include <opxy/sample.hpp>
#include <opxy/sample_analysis.hpp>
#include <opxy/transport.hpp>

namespace opxy {
// An 18-bit value (0..262143) the machine defines and the SDK saves with each
// pad (padSettings). Its meaning belongs to the machine.
typedef u32 PadSettings;
// SampleRegion.level is unsigned Q27: this is unity gain.
const u32 sample_unity_level = 1u << 27;

// The playing voice's pad region, read once per block. Bounds are half-open,
// clamped to the sample; an empty interval means silence.
struct SampleRegion {
    u32 begin;
    u32 end;
    u32 level;        // unsigned Q27 gain; sample_unity_level is unity
    bool reverse;
    i32 pitch_cents;  // the pad's saved tuning, in cents from the sample's root
};

// A sample being assigned to a pad, before stock publishes the new region.
struct SampleAssignment {
    const char* filename;  // the file's path, filename_bytes long, not NUL-terminated; may be null
    u32 filename_bytes;
    u32 frames;            // loaded frames
    u32 rate;              // source sample rate
    u32 bpm_x10;           // project tempo, as Transport::bpm_x10
    // Whether the pad already held a sample with saved settings, which the
    // machine may keep. Otherwise previous is sample_defaults().
    bool replacing;
    PadSettings previous;
    // A completed recording from the stock recorder rather than a file
    // (a file's filename may be null too).
    bool recorded;
};

// Drum page (drumControls). A machine-routed encoder event: zero-based knob,
// turn direction, press, and whether Shift is held.
// held is true for a turn while the encoder is physically held down.
// pressed is a separate click; turning suppresses that click on release.
struct DrumControl { u32 knob; i32 direction; bool pressed; bool shifted; bool held; };
// What the drum page shows for the selected pad, as the machine plays it with
// the track's live parameters applied. The SDK only projects these values. It
// passes the panel in with pitch 0, the saved direction and an unchanged level,
// so a machine sets what its controls change.
struct DrumPanel {
    i32 pitch_cents;      // live offset in cents; the SDK adds the pad's saved pitch
    bool repeat;          // loop (true) or play once (false)
    bool reverse;         // the direction the pad plays in
    u32 level;            // Q31 scale of the pad's saved volume; 0x7fffffff leaves it unchanged
    char shifted[2][16];  // two NUL-terminated Shift captions of at most 15 characters
};

// Frozen controls for a forward, one-pass lazy-chop audition. The source range
// is the waveform's outer range; live trim/reverse/loop controls do not change
// that range while taps divide it. No PCM or analysis is accessed by this
// GUI-side timing callback.
struct SampleSlice {
    u32 frames, rate, bpm_x10;
    SampleRegion region;
    PadSettings settings;
    const i32* parameters;
    i32 track_transpose;
};
// Source frames per 44.1 kHz output frame, multiplied by unsigned Q12 tuning.
// Denominator and tuning must be positive; a zero numerator means no advance.
// The same frozen inputs drive audition audio; the GUI converts this ratio
// into source frames per elapsed second.
struct SampleSliceRate { u32 numerator, denominator, tuning; };

// One audio block. All pointers are borrowed for this call. Analysis is
// immutable and can be absent while preparation is pending, unsupported or out
// of cache budget; it belongs to the same retained PCM as the voice.
struct SampleBlock {
    Output output;          // 64 words, or 64 interleaved L/R frames when output_channels is 2
    Parameters parameters;  // the track's eight live Q31 parameters
    Pitch pitch;
    Transport transport;
    SampleRegion region;
    const SampleAnalysis* analysis;
    PadSettings settings;   // the pad's saved settings or sample_defaults(); 0 without padSettings
    u32 output_channels;    // 1 or 2 (stereoOutput)
    i32 track_transpose;    // track settings transpose, -36..36 semitones
    // Page 7: semitones from the note's canonical pad to the played note (a
    // multiple of 12), captured at note start. 0 on page 2.
    i32 drum_transpose;
    bool slicing;          // forward, one-pass audition using frozen outer bounds
};
}
