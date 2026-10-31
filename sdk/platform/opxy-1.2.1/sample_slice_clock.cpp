// GUI-side snapshot and clock. Audio receives this snapshot in the source
// note's stock transport packet; no GUI global is read by the audio role.
#include "sample_slice_clock.hpp"
#include "sample_machines.hpp"
#include "stock_sample.hpp"
#include "stock.hpp"

using opxy::u32;
using opxy::i32;
using namespace opxy::platform;

void sdk_sample_slice_config(SamplePreviewRange* source, u32 engine, u32 track, const void* raw) {
    const SampleMachine* machine = sdk_sample_machine(engine);
    if (!source || !raw || track >= kTracks || !machine || !machine->slice_clock) return;
    const opxy::SampleRegion region = sample_gui_region(raw);
    const SavedSettings saved = saved_settings(raw);
    source->processed = 1;
    source->bpm_x10 = project_bpm_x10();
    if (!source->bpm_x10) source->bpm_x10 = 1200;
    source->settings = saved.saved ? saved.value : machine->defaults();
    source->level = region.level;
    source->pitch_cents = region.pitch_cents;
    const auto parameter = stock<i32(u32, u32, u32, u32)>(0x8d5ac9dc);
    for (u32 i = 0; i < 8; ++i) source->parameters[i] = parameter(0x86bd027c, track, i, 1);
    source->track_transpose = sample_track_transpose(parameter(0x86bd027c, track, 52, 1));
}

u32 sdk_sample_slice_clock_rate(u32 engine, const SamplePreviewRange* source, u32 file_bits) {
    const SampleMachine* machine = sdk_sample_machine(engine);
    const u32 rate = slice_file_rate(file_bits);
    if (!source || !source->processed || !machine || !machine->slice_clock || !rate) return file_bits;
    const opxy::SampleSlice input{source->frames, rate, source->bpm_x10,
        opxy::SampleRegion{source->begin, source->end, source->level, false, source->pitch_cents},
        source->settings, source->parameters, source->track_transpose};
    opxy::SampleSliceRate clock{};
    machine->slice_clock(&input, &clock);
    if (!clock.denominator || !clock.tuning) return file_bits;
    const auto multiply = stock<u32(u32, u32)>(0x04021ac0);
    const u32 frames_per_second = multiply(slice_ratio_bits(clock.numerator, clock.denominator),
                                          slice_ratio_bits(44100, 1));
    return multiply(frames_per_second, slice_ratio_bits(clock.tuning, 4096));
}
