// Drives the intensity rule and the example META bank (high octave of the
// punch-in track's second bank) through their callbacks.
#include <cstdio>
#include <cstdlib>
#include <opxy/punch.hpp>

namespace {
void require(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "failed: %s\n", what); std::exit(1); }
}
const opxy::i32 kUnset = -1;
opxy::i32 set_value[8][58];
unsigned plays, play_length, play_delay;
opxy::u8 play_pitch, play_velocity;
void clear() {
    for (auto& track : set_value)
        for (auto& value : track) value = kUnset;
}
}

void sdk_punch_override(opxy::u32 track, opxy::u32 parameter, opxy::i32 value, void*) {
    set_value[track][parameter] = value;
}
opxy::i32 master_value[12];
void sdk_punch_master(opxy::u32 parameter, opxy::i32 value) { master_value[parameter] = value; }
opxy::i32 sdk_punch_master_project(opxy::u32) { return 0x40000000; }
opxy::i32 track_value[16][58];
void sdk_punch_aux(opxy::u32 parameter, opxy::i32 value) { track_value[12][parameter] = value; }
void sdk_punch_track_set(opxy::u32 track, opxy::u32 parameter, opxy::i32 value) { track_value[track][parameter] = value; }
opxy::i32 sdk_punch_track_own(opxy::u32, opxy::u32) { return 0x40000000; }
opxy::i32 sdk_punch_live(opxy::u32, opxy::u32) { return 0x60000000; }   // every track at 75 %
opxy::u32 filter_type[8];
void sdk_punch_filter(opxy::u32 track, opxy::u32 type) { filter_type[track] = type; }
opxy::u32 keys_down;
bool sdk_punch_key_held(opxy::u32 key) { return keys_down >> key & 1u; }
void sdk_punch_play(opxy::u32, opxy::u8 pitch, opxy::u8 velocity, opxy::u32 length, opxy::u32 delay) {
    ++plays, play_pitch = pitch, play_velocity = velocity, play_length = length, play_delay = delay;
}

namespace {
using namespace opxy;
using namespace opxy::punch;
const Effect& effect(u8 key) {
    for (u32 i = 0; i < sdk_punch_effect_count; ++i)
        if (sdk_punch_effects[i]->key == key) return *sdk_punch_effects[i];
    require(false, "effect registered");
    return *sdk_punch_effects[0];
}
const i32 kUp = 15000, kDown = -15000, kLoopRest = 0, kHalf = 9000, kRest = 0;

void intensity_rule() {
    const i32 rest[] = {0, 3000, -3000, 1500};
    for (i32 bend : rest) require(intensity(bend) == 200, "the minimum at rest and in the dead zone");
    require(intensity(kUp) == 1000 && intensity(49149) == 1000 && intensity(2 * 8191) == 1000, "100 % up");
    require(intensity(kDown) == 1000 && intensity(-49151) == 1000 && intensity(-2 * 8192) == 1000, "100 % down too");
    require(intensity(9000) == 600 && intensity(-9000) == 600, "halfway either way");
    u32 previous = 0;
    for (i32 bend = 0; bend <= 49149; bend += 7) {
        const u32 now = intensity(bend);
        require(now >= previous && now >= 200 && now <= 1000 && now == intensity(-bend), "monotonic in size, 200..1000");
        previous = now;
    }
    require(by_intensity(200, 10, 20, 40) == 10 && by_intensity(600, 10, 20, 40) == 20 &&
            by_intensity(1000, 10, 20, 40) == 40, "by_intensity hits its three points");
    require(by_intensity(400, 10, 20, 40) == 15 && by_intensity(800, 10, 20, 40) == 30 &&
            by_intensity(0, 10, 20, 40) == 10 && by_intensity(5000, 10, 20, 40) == 40, "lines between, clamped");
    require(by_intensity(800, 0, 0x40000000, 0x7fffffff) == 0x5fffffff, "Q31 without overflow");
}

void gates() {
    const Effect& gate = effect(14);
    for (u32 tick = 0; tick < 96; ++tick) {
        clear();
        Context context{1, false, tick == 0, tick, kRest, false};
        gate.tick(context, Overrides{1, nullptr});
        require(set_value[1][parameter_level] == (tick % 48 >= 24 ? 0 : kUnset), "rest: eighths, second half silent");
        clear();
        context.bend = kUp, gate.tick(context, Overrides{1, nullptr});
        require(set_value[1][parameter_level] == (tick % 12 >= 6 ? 0 : kUnset), "full: 32nds");
        clear();
        context.bend = kHalf, gate.tick(context, Overrides{1, nullptr});
        require(set_value[1][parameter_level] == (tick % 24 >= 12 ? 0 : kUnset), "halfway: sixteenths");
    }
}

void roll() {
    const Effect& snare = effect(2);
    require(snare.tick && snare.note, "snare roll on low G");
    // Melodic tracks are left alone.
    Context melodic{6, false, true, 0, kRest, false};
    plays = 0;
    for (u32 t = 0; t < 400; ++t) melodic.first = t == 0, melodic.ticks96 = t, snare.tick(melodic, Overrides{6, nullptr});
    require(plays == 0, "no roll on melodic tracks");
    // At rest quarters of the snare pad G from the press; a later A snare does
    // not take over; a kick changes nothing; velocity rises over two bars.
    Context drum{0, true, true, 0, kRest, false};
    plays = 0;
    unsigned previous_velocity = 0;
    for (u32 t = 0; t < 768; ++t) {
        drum.first = t == 0, drum.ticks96 = t;
        if (t == 50) { Note kick{53, 127, false}; require(snare.note(drum, kick) && kick.pitch == 53, "a kick passes"); }
        if (t == 150) { Note first{55, 100, false}; require(snare.note(drum, first), "the first snare hit plays"); }
        if (t == 300) { Note other{57, 100, false}; require(!snare.note(drum, other), "a later snare hit from the sequencer is dropped"); }
        if (t == 310) { Note same{55, 100, false}; require(!snare.note(drum, same), "even the roll's own pad"); }
        const unsigned before = plays;
        snare.tick(drum, Overrides{0, nullptr});
        if (plays == before) continue;
        require(t >= 150, "silent until the pattern plays a snare pad");
        require(t % 96 == 0 && play_length == 48 && play_pitch == 55, "quarters, sticking to the first snare, G");
        require(play_velocity >= previous_velocity && play_velocity <= 100, "rising velocity");
        previous_velocity = play_velocity;
    }
    require(plays == 6 && previous_velocity >= 90, "quarters from the first snare, nearly full after two bars");
    // The rate by the encoder, a doubling a notch (480 knob units each, from the dead zone), tilt ignored.
    const i32 knobs[] = {0, 3000 + 480, 3000 + 2 * 480, 3000 + 3 * 480, 3000 + 4 * 480, 3000 + 10 * 480, 3000 + 479};
    const u32 periods[] = {96, 48, 24, 12, 6, 6, 48};
    for (u32 b = 0; b < 7; ++b) {
        Context fast{1, true, true, 0, kUp, false};
        fast.knob = knobs[b];
        plays = 0;
        for (u32 t = 0; t < 384; ++t) {
            fast.first = t == 0, fast.ticks96 = t;
            if (t == 0) { Note pad{55, 100, false}; snare.tick(fast, Overrides{1, nullptr}), snare.note(fast, pad); continue; }
            snare.tick(fast, Overrides{1, nullptr});
        }
        require(plays == 384 / periods[b] - 1, "the rate follows the intensity");
    }
}

void kill() {
    const Effect& kill = effect(5);
    require(kill.note && kill.release == release_bar && release_bar == 384, "kill on low A#, released on the bar");
    Context drum{0, true, true, 0, kHalf, false};
    Note kick{53, 100, false}, kick2{54, 90, false}, snare{38, 100, false}, hat{60, 100, false};
    require(kill.note(drum, kick) && kill.note(drum, kick2), "kick pads kept");
    require(!kill.note(drum, snare) && !kill.note(drum, hat), "other pads dropped at rest");
    drum.bend = kRest;
    Note rest_snare{38, 100, false}, rest_kick{53, 100, false};
    require(!kill.note(drum, rest_snare) && kill.note(drum, rest_kick), "at rest already a full kill: kicks only");
    drum.bend = kUp;
    Note top{53, 100, false};
    require(!kill.note(drum, top), "100 %: even the kick goes");
    // Melodic: the lowest note starting on a tick.
    Context melodic{5, false, true, 96, kHalf, false};
    Note a{48, 100, false}, b{60, 100, false}, c{40, 100, false}, d{60, 100, false};
    require(kill.note(melodic, a), "first note of the tick kept");
    melodic.first = false;
    require(!kill.note(melodic, b), "a higher note dropped");
    require(kill.note(melodic, c), "a lower one kept");
    melodic.ticks96 = 120;
    require(kill.note(melodic, d), "the next tick starts afresh");
}

void filters() {
    {   // lowpass + hipass of one octave held: the dynamic filter, a narrow band in the middle,
        // moved down or up by the signed encoder (Context::turn), low and high cut together
        clear();
        keys_down = 1u << 1 | 1u << 3;
        Context drum{2, true, true, 0, kRest, false};
        effect(1).tick(drum, Overrides{2, nullptr});
        effect(3).tick(drum, Overrides{2, nullptr});
        require(filter_type[2] == filter_dynamic && set_value[2][16] == 0x48000000 && set_value[2][19] == 0x48000000,
                "both keys: dynamic filter, the band in the middle");
        drum.turn = 1000, effect(1).tick(drum, Overrides{2, nullptr});
        require(set_value[2][16] > 0x70000000 && set_value[2][19] == set_value[2][16], "turned right: both cuts up together");
        drum.turn = -1000, effect(1).tick(drum, Overrides{2, nullptr});
        require(set_value[2][16] < 0x20000000 && set_value[2][16] > 0 && set_value[2][19] == set_value[2][16], "turned left: both down together");
        keys_down = 0;
        filter_type[2] = 0;
    }
    clear();
    Context drum{2, true, true, 0, kRest, false}, synth{6, false, true, 0, kRest, false};
    effect(1).tick(drum, Overrides{2, nullptr});
    effect(1).tick(synth, Overrides{6, nullptr});
    require(filter_type[2] == filter_z_lowpass && filter_type[6] == 0 && set_value[6][parameter_cutoff] == kUnset,
            "low F#: z lowpass on drum tracks only");
    require(set_value[2][parameter_cutoff] == 0x0a000000 && set_value[2][parameter_resonance] == 0x44000000,
            "lowpass dark on the press, strong resonance");
    drum.bend = kUp, effect(1).tick(drum, Overrides{2, nullptr});
    require(set_value[2][parameter_cutoff] == 0x0a000000, "tilt or bend leave the lowpass dark");
    drum.bend = kRest, drum.knob = kHalf, effect(1).tick(drum, Overrides{2, nullptr});
    const i32 half_cutoff = set_value[2][parameter_cutoff];
    drum.knob = kUp, effect(1).tick(drum, Overrides{2, nullptr});
    require(set_value[2][parameter_cutoff] == 0x60000000, "lowpass open at the encoder's full");
    {   // eased by the square: the encoder's first half does 3/4 of the cutoff's travel (1/4 left)
        const i32 expected = 0x60000000 - (0x60000000 - 0x0a000000) / 4;
        require(half_cutoff > expected - 0x1000 && half_cutoff < expected + 0x1000, "halfway on the encoder: three quarters open");
    }
    require(set_value[2][19] == 0, "key tracking held at 0");
    {   // the screen: one still frame per encoder position, moving only with it
        const Animation* view = effect(1).animation;
        Context still{2, true, false, 0, kRest, false};
        effect(1).tick(still, Overrides{2, nullptr});
        const u8* const* at_press = view->frames;
        still.ticks96 = 200, still.bend = kUp, effect(1).tick(still, Overrides{2, nullptr});
        require(view->count == 1 && view->frames == at_press, "time and tilt leave the screen still");
        still.knob = kUp, effect(1).tick(still, Overrides{2, nullptr});
        require(view->frames == at_press + 16, "the encoder's full: the open frame");
    }
    effect(15).tick(synth, Overrides{6, nullptr});
    require(filter_type[6] == filter_z_hipass && set_value[6][parameter_cutoff] == 0x66000000, "high G#: z hipass on melodic, thin on the press");
    synth.bend = kUp, effect(15).tick(synth, Overrides{6, nullptr});
    require(set_value[6][parameter_cutoff] == 0x66000000, "tilt leaves the hipass thin");
    synth.knob = kUp, effect(15).tick(synth, Overrides{6, nullptr});
    require(set_value[6][parameter_cutoff] == 0x02000000, "hipass nearly open at the encoder's full");
}

void wah() {
    // Wah: every track, z lowpass, the cutoff swinging once a bar at rest.
    const Effect& wah = effect(18);
    clear();
    Context mel{5, false, true, 0, kRest, false};
    i32 lowest = 0x7fffffff, highest = 0;
    for (u32 t = 0; t < 384; ++t) {
        mel.first = t == 0, mel.ticks96 = t;
        wah.tick(mel, Overrides{5, nullptr});
        const i32 cutoff = set_value[5][parameter_cutoff];
        lowest = cutoff < lowest ? cutoff : lowest, highest = cutoff > highest ? cutoff : highest;
    }
    require(filter_type[5] == filter_z_lowpass && set_value[5][parameter_resonance] == 0x5c000000, "resonant z lowpass");
    require(lowest <= 0x14100000 && highest >= 0x5b000000, "the cutoff swings over its whole range in a bar");
    clear();
    Context kit{1, true, true, 0, kRest, false};
    wah.tick(kit, Overrides{1, nullptr});
    require(filter_type[1] == filter_z_lowpass && set_value[1][parameter_cutoff] != kUnset, "drum tracks too");
}

void low_octave() {
    {   // volume: silent at rest, back to the own level (75 %) at the top; drums on low F, melodic on high F
        const Effect& drums = effect(0);
        const Effect& melodic = effect(12);
        clear();
        Context drum{1, true, true, 0, kRest, false};
        Context synth{5, false, true, 0, kRest, false};
        drums.tick(drum, Overrides{1, nullptr});
        drums.tick(synth, Overrides{5, nullptr});
        require(set_value[1][parameter_level] == 0 && set_value[5][parameter_level] == kUnset, "low F: drum tracks only, silent at rest");
        drum.first = false, drum.bend = kHalf, drums.tick(drum, Overrides{1, nullptr});
        require(set_value[1][parameter_level] == 0x30000000, "halfway: half their own level");
        melodic.tick(synth, Overrides{5, nullptr});
        synth.first = false, synth.bend = kUp, melodic.tick(synth, Overrides{5, nullptr});
        require(set_value[5][parameter_level] == 0x60000000, "high F: melodic tracks, their own level at the top");
    }
    {   // ratchet: two at rest, 24 per step at full
        const Effect& ratchet = effect(7);
        Context context{4, false, true, 0, kLoopRest, false};
        Note note{60, 100, false};
        plays = 0, ratchet.note(context, note);
        require(plays == 1, "ratchet: twice at rest");
        context.bend = kUp, plays = 0, ratchet.note(context, note);
        require(plays == 7 && play_delay == 7 * (ticks_per_step / 8), "ratchet: eight per step at full");
    }
    {   // send boost: 60 % at rest, full plus the dry level gone at 100 %
        const Effect& boost = effect(19);
        clear();
        Context context{2, false, true, 0, kHalf, false};
        boost.tick(context, Overrides{2, nullptr});
        require(set_value[2][parameter_send_fx1] == 0x60000000 + (0x7fffffff - 0x60000000) / 2 &&
                set_value[2][parameter_level] == kUnset, "boost halfway");
        context.bend = kUp, boost.tick(context, Overrides{2, nullptr});
        require(set_value[2][parameter_send_fx1] == 0x7fffffff && set_value[2][parameter_level] == kUnset,
                "boost: full send at the top, the dry sound kept");
        clear(), context.bend = kRest, boost.tick(context, Overrides{2, nullptr});
        require(set_value[2][parameter_send_fx1] == 0x60000000, "boost: 75 % at rest");
        // From the lead track the send effects grow too: FX I's third
        // parameter up to 30 % of its room, FX II's first up to 60 %.
        Context lead{2, false, true, 0, kUp, true};
        boost.tick(lead, Overrides{2, nullptr});
        effect(20).tick(lead, Overrides{2, nullptr});
        require(track_value[14][2] == 0x40000000 + (0x7fffffff - 0x40000000) / 10 * 3, "FX I: feedback gently up");
        require(track_value[15][0] == 0x40000000 + (0x7fffffff - 0x40000000) / 10 * 6, "FX II: reverb length up");
        require(track_value[12][parameter_send_fx1] == 0x7fffffff, "Aux 5 follows");
    }
    {   // pump: silent on the beat, back to 75 % half a beat later
        const Effect& pump = effect(4);
        clear();
        Context context{6, false, true, 96, kRest, false};
        pump.tick(context, Overrides{6, nullptr});
        require(set_value[6][parameter_level] == 0, "pump: silent on the beat");
        clear(), context.ticks96 = 96 + 24, pump.tick(context, Overrides{6, nullptr});
        require(set_value[6][parameter_level] > 0 && set_value[6][parameter_level] < 0x20000000,
                "pump at rest: still deep a quarter beat in");
        clear(), context.ticks96 = 96 + 48, pump.tick(context, Overrides{6, nullptr});
        require(set_value[6][parameter_level] == kUnset, "pump at rest: own level after half a beat");
    }
    {   // squash: compressor 80 %, level the project's 50 % plus 30 %
        const Effect& squash = effect(8);
        Context context{0, true, true, 0, kHalf, true};
        squash.tick(context, Overrides{0, nullptr});
        require(master_value[static_cast<u32>(Master::compressor)] == 0x66666666 &&
                master_value[static_cast<u32>(Master::level)] == 0x40000000 + 0x26666666, "squash lifts the level");
    }
}

void accent_and_octave() {
    const Effect& accent = effect(6);
    require(accent.note && !accent.tick && accent.animation, "accent on low B");
    Context context{0, true, true, 0, kRest, false};
    Note beat{38, 100, false}, off{38, 100, false}, generated{38, 100, true};
    context.ticks96 = 96, require(accent.note(context, beat) && beat.velocity == 127, "a beat is boosted");
    Note soft_beat{38, 50, false};
    require(accent.note(context, soft_beat) && soft_beat.velocity == 90, "a beat gains 40");
    context.ticks96 = 48, require(!accent.note(context, off), "an off-beat note is dropped, even at rest");
    {   // sparser with the intensity: quarters, halves, bars, two bars
        const i32 bends[] = {kRest, 3000 + 12000 * 30 / 100, 3000 + 12000 * 55 / 100, kUp};
        const u32 periods[] = {96, 192, 384, 768};
        for (u32 z = 0; z < 4; ++z) {
            context.bend = bends[z];
            u32 played = 0;
            for (u32 t = 0; t < 1536; t += 96) {
                context.ticks96 = t;
                Note n{38, 100, false};
                played += accent.note(context, n);
                require(accent.note(context, n) == (t % periods[z] == 0), "only the zone's beats play");
            }
            require(played == 1536 / periods[z], "the zone's number of beats");
        }
    }
    context.bend = kRest;
    accent.note(context, generated);
    require(generated.velocity == 100, "generated notes are left alone");
    context.bend = kRest;
    const Effect& octave = effect(17);
    require(octave.note && octave.animation, "octave on high A#");
    Context synth{5, false, true, 0, kRest, false}, drum{1, true, true, 0, kRest, false};
    Note n{60, 100, false}, d{36, 100, false}, high{120, 100, false};
    octave.note(synth, n), octave.note(drum, d), octave.note(synth, high);
    require(n.pitch == 72 && d.pitch == 36 && high.pitch == 120, "an octave up on melodic tracks, drums and the top notes stay");
    synth.bend = kUp;
    Note top{40, 100, false};
    octave.note(synth, top);
    require(top.pitch == 64, "two octaves at full");
}

void clip_and_gain() {
    const struct { u32 key; Master knob; i32 low, mix_low; const char* name; } keys[] = {
        {10, Master::dist_clip, 0x3fffffff, 0x19999999, "low D#: clip"},
        {11, Master::dist_gain, 0x60000000, 0x2cccccc8, "low E: gain"}};
    for (const auto& k : keys) {
        const Effect& e = effect(k.key);
        require(e.tick && !e.note && !e.step && e.animation, k.name);
        Context follower{3, false, true, 0, kUp, false};
        clear(), master_value[static_cast<u32>(k.knob)] = kUnset, e.tick(follower, Overrides{3, nullptr});
        require(master_value[static_cast<u32>(k.knob)] == kUnset, "only the lead track sets the master");
        Context lead{0, true, true, 0, kRest, true};
        clear(), e.tick(lead, Overrides{0, nullptr});
        const i32 knob = master_value[static_cast<u32>(k.knob)], mix = master_value[static_cast<u32>(Master::dist_mix)];
        require(knob >= k.low - 16 && knob <= k.low + 16 && mix >= k.mix_low - 16 && mix <= k.mix_low + 16, "the base at rest");
        lead.bend = kUp, e.tick(lead, Overrides{0, nullptr});
        require(master_value[static_cast<u32>(k.knob)] == 0x7fffffff && master_value[static_cast<u32>(Master::dist_mix)] == 0x40000000,
                "full and a 50 % mix at the top");
        lead.bend = kHalf, e.tick(lead, Overrides{0, nullptr});
        const i32 halfway = master_value[static_cast<u32>(k.knob)], expect = k.low + (0x7fffffff - k.low) / 2;
        require(halfway > expect - 16 && halfway < expect + 16, "halfway between");
    }
}
}

int main() {
    intensity_rule();
    filters();
    wah();
    low_octave();
    gates();
    roll();
    kill();
    clip_and_gain();
    accent_and_octave();
    return 0;
}
