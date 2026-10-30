// Test fixture: plays its pad's region at the Volume control and implements
// every optional sample service (pad settings, drum controls and analysis)
// with the smallest behavior that honors each contract.
#include <opxy/sample.hpp>
#include <opxy/sample_analysis.hpp>
#include <opxy/sample_engine.hpp>
#include <opxy/q31.hpp>

namespace probesample {
struct Voice { opxy::SampleView sample; opxy::u32 position; bool once, finished; };
// Pad settings: bit 0 plays the pad once instead of looping.
const opxy::PadSettings kOnce = 1;

void initialize(Voice& voice, const opxy::SampleView& sample) {
    voice.sample = sample;
    voice.position = 0;
    voice.once = voice.finished = false;
}
void start(Voice& voice) { voice.position = 0; voice.finished = false; }

void render_sample(Voice& voice, const opxy::SampleBlock& block) {
    const opxy::u32 channels = block.output_channels == 2 ? 2 : 1;
    const opxy::u32 begin = block.region.begin, end = block.region.end;
    voice.once = (block.settings & kOnce) != 0 || block.parameters[1] > 0x40000000;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        if (voice.position < begin || voice.position >= end) {
            voice.finished = voice.once && voice.position >= end;
            voice.position = begin;
        }
        const bool silent = voice.finished || begin >= end;
        for (opxy::u32 c = 0; c < channels; ++c) {
            const opxy::i32 value = silent ? 0 : opxy::i32(opxy::sample_at(voice.sample, voice.position, c)) * 0x8000;
            block.output[i * channels + c] = opxy::q31::multiply(value, block.parameters[2]);
        }
        if (!silent) ++voice.position;
    }
}

opxy::PadSettings sample_defaults() { return 0; }
opxy::PadSettings sample_assign(const opxy::SampleAssignment& input) {
    return input.replacing ? input.previous : sample_defaults();
}
// Pressing the first encoder toggles play-once for the pad.
void drum_control(opxy::PadSettings& value, const opxy::DrumControl& event) {
    if (event.knob == 0 && event.pressed) value = static_cast<opxy::PadSettings>(value ^ kOnce);
}
void drum_panel(opxy::PadSettings value, const opxy::SampleRegion&, const opxy::i32*, opxy::DrumPanel& panel) {
    panel.repeat = (value & kOnce) == 0;
}

// Analysis: the peak magnitude of the first channel, in one word.
struct Preparation { opxy::SampleView sample; opxy::u32* storage; opxy::u32 peak; };
opxy::u32 analysis_words(const opxy::SampleView& sample) { return sample.frames ? 1 : 0; }
bool prepare_begin(Preparation& job, const opxy::SampleView& sample, opxy::u32* storage, opxy::u32 words) {
    if (!storage || words < 1) return false;
    job.sample = sample;
    job.storage = storage;
    job.peak = 0;
    return true;
}
opxy::PrepareStatus prepare_step(Preparation& job, opxy::u32 frames, opxy::u32& cursor) {
    const opxy::u32 last = job.sample.frames - cursor < frames ? job.sample.frames : cursor + frames;
    for (; cursor < last; ++cursor) {
        const opxy::i32 value = opxy::sample_at(job.sample, cursor);
        const opxy::u32 magnitude = static_cast<opxy::u32>(value < 0 ? -value : value);
        if (magnitude > job.peak) job.peak = magnitude;
    }
    return cursor < job.sample.frames ? opxy::prepare_running : opxy::prepare_done;
}
bool prepared(const Preparation& job, opxy::SampleAnalysis& out) {
    job.storage[0] = job.peak;
    out.sample = job.sample;
    out.words = job.storage;
    out.word_count = 1;
    out.info = 0;
    return true;
}
}
