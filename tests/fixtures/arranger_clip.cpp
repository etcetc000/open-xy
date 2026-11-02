#include <opxy/arranger.hpp>
static void host_barrier() {}
#include "../../sdk/platform/opxy-1.2.1/arranger_clip.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace opxy;
using namespace opxy::platform;
static void require(bool ok, const char* message) { if (!ok) { std::puts(message); std::exit(1); } }
alignas(4) static u8 project[0xd7c + 8 * 0x53f44];
static u8* pool(u32 track) { return project + 0xd7c + track * 0x53f44; }
static u32& count(u32 track) { return *reinterpret_cast<u32*>(pool(track) + 0x53f40); }
static void initialize_empty(u8* source) { source[0] = 16; source[8] = 7; source[0x457c] = 0; }
int main() {
    count(0) = 1; pool(0)[0] = 12; pool(0)[8] = 1; pool(0)[100] = 79;
    arranger::Model empty;
    const i32 added = arranger_add_empty_clip(empty, project, 0, 0, 0, initialize_empty);
    require(added == 0 && count(0) == 2, "new source append");
    require(empty.clip(0)->length == 16*480 && empty.clip(0)->source_length == 64*480, "new visible bar and 64 step source");
    require(pool(0)[0x53f4] == 64 && pool(0)[0x53f4+8] == 3 && pool(0)[0x53f4+100] == 79, "native new source length rate and instrument");
    require(pool(0)[0] == 12 && pool(0)[8] == 1, "new source preserves original timing");
    require(arranger_add_empty_clip(empty, project, 0, 0, 0, initialize_empty) < 0 && count(0) == 2, "failed insertion does not append");
    arranger::Model source_instances;
    pool(0)[0] = 8; pool(0)[8] = 5;
    require(arranger_add_source_clip(source_instances, project, 0, 100, 0) == 0 &&
        source_instances.clip(0)->length == 8*960 && source_instances.clip(0)->source_length == 8*480,
        "existing source insertion respects shorter source and slower rate");
    require(pool(0)[0] == 8 && pool(0)[8] == 5 && count(0) == 2, "existing insertion does not change native source");
    require(source_instances.insert({0,0,9000,100,0,8*480}) == 1, "existing source collision setup");
    require(arranger_add_source_clip(source_instances, project, 0, 8000, 0) == 2 &&
        source_instances.clip(2)->length == 1000, "existing source caps at next clip");
    require(arranger_add_source_clip(source_instances, project, 0, 8100, 0) < 0, "existing source rejects inside occupied interval");
    pool(0)[0] = 65;
    require(arranger_add_source_clip(source_instances, project, 0, 10000, 0) < 0, "invalid native source length rejected");
    arranger::Model model;
    count(0) = 1;
    for (u32 i = 0; i < 0x53f4; ++i) pool(0)[i] = static_cast<u8>(i * 13 + 7);
    require(model.insert({0, 0, 100, 100, 17, 64}) == 0, "first instance");
    require(model.duplicate(0, 200) == 1, "second instance");
    require(model.setMuted(0,true), "mute selected instance before unique");
    require(arranger_make_unique(model, project, 0), "unique succeeds");
    require(model.clip(0)->muted && !model.clip(1)->muted, "unique preserves per-instance mute only");
    require(count(0) == 2 && model.clip(0)->pattern == 1 && model.clip(1)->pattern == 0, "only selected reference changes");
    require(!std::memcmp(pool(0), pool(0) + 0x53f4, 0x53f4), "complete source copied");
    pool(0)[0x53f4 + 0x457c] ^= 1;
    require(pool(0)[0x457c] != pool(0)[0x53f4 + 0x457c], "source data independent");
    require(model.insert({1, 0, 150, 100, 0, 64}) == 2, "overlap setup");
    require(!arranger_move_clip_track(model, project, 0, 1) && count(1) == 0 && model.clip(0)->track == 0, "cross lane overlap no append");
    require(model.remove(2), "remove blocker");
    project[12] = 2;
    require(!arranger_move_clip_track(model, project, 0, 1) && count(1) == 0, "destination lock rejected");
    project[12] = 1;
    require(!arranger_make_unique(model, project, 0), "source lock rejected");
    project[12] = 0;
    count(1) = 16;
    require(!arranger_move_clip_track(model, project, 0, 1) && model.clip(0)->track == 0, "full destination rollback");
    count(1) = 0;
    require(arranger_move_clip_track(model, project, 0, 1), "cross lane succeeds");
    require(count(1) == 1 && count(0) == 2 && model.clip(0)->track == 1 && model.clip(0)->pattern == 0 &&
        model.clip(0)->offset == 17 && model.clip(0)->start == 100 && model.clip(0)->muted, "cross lane preserves timing and old sources");
    require(!std::memcmp(pool(1), pool(0) + 0x53f4, 0x53f4), "cross lane source and instrument copied");
    require(!arranger_move_clip_track(model, project, 0, 8) && !arranger_make_unique(model, nullptr, 0), "bad inputs rejected");
    count(1) = 17;
    require(!arranger_make_unique(model, project, 0), "corrupt count rejected");
    count(1) = 1;
    require(model.setMuted(1,true), "mute clipboard instance");
    const arranger::Clip clipboard = *model.clip(1);
    project[12] = 1;
    const i32 shared = arranger_paste_clip(model, project, clipboard, 0, 300);
    require(shared >= 0 && model.clip(shared)->pattern == clipboard.pattern && model.clip(shared)->muted && count(0) == 2,
        "same lane paste shares even with instrument lock");
    require(arranger_paste_clip(model, project, clipboard, 2, 300) < 0 && count(2) == 0,
        "cross lane paste rejects source lock");
    project[12] = 4;
    require(arranger_paste_clip(model, project, clipboard, 2, 300) < 0, "paste destination lock");
    project[12] = 0;
    const i32 pasted = arranger_paste_clip(model, project, clipboard, 2, 300);
    require(pasted >= 0 && count(2) == 1 && model.clip(pasted)->track == 2 && model.clip(pasted)->pattern == 0 &&
        model.clip(pasted)->offset == clipboard.offset && model.clip(pasted)->muted && clipboard.track == 0 && clipboard.pattern == 0,
        "cross lane paste preserves clipboard and phase");
    require(!std::memcmp(pool(2), pool(0), 0x53f4), "paste copies source and instrument");
    require(arranger_paste_clip(model, project, clipboard, 2, 350) < 0 && count(2) == 1, "paste overlap no allocation");
    count(2) = 16;
    require(arranger_paste_clip(model, project, clipboard, 2, 500) < 0, "paste full destination");
    require(arranger_paste_clip(model, project, clipboard, 3, arranger::Model::max_time) < 0 && count(3) == 0,
        "paste overflow no allocation");
    require(arranger_paste_clip(model, nullptr, clipboard, 0, 500) < 0, "same lane validates project");
    arranger::Model full;
    for (u32 i = 0; i < arranger::Model::capacity; ++i)
        require(full.insert({0, 0, i, 1, 0, 1}) >= 0, "full paste setup");
    require(arranger_paste_clip(full, project, clipboard, 3, 500) < 0 && count(3) == 0,
        "full model paste leaves native pool unchanged");
    project[12] = 0; count(0) = 1;
    const u32 rates[16] = {5040,3360,2240,1680,1260,840,560,420,336,280,240,210,210,140,105,105};
    for (u32 scale = 0; scale < 16; ++scale) {
        pool(0)[0] = 4; pool(0)[8] = static_cast<u8>(scale);
        arranger::Model phases;
        const arranger::Clip original(0, 0, 100, 10000, 1919, 7680, 1679);
        require(phases.insert(original) == 0, "fractional source setup");
        const i32 right = arranger_split_clip(phases, project, 0, 101);
        require(right >= 0, "fine split at every scale");
        const arranger::Clip* r = phases.clip(right);
        const u32 period = 4 * 480 * 1680;
        const u32 expected = static_cast<u32>((1919ull * 1680 + 1679 + rates[scale]) % period);
        require(r->offset * 1680 + r->phase_remainder == expected && r->source_length == 1920,
            "exact split uses current native period and fractional rate");
        for (u32 time = 101; time < 10000; time += 317)
            require(arranger_source_phase(*r, pool(0), time) == arranger_source_phase(original, pool(0), time),
                "split playback continuity across wraps");
        require(arranger_trim_clip_left(phases, project, right, 104), "fractional trim");
        r = phases.clip(right);
        require(arranger_source_phase(*r, pool(0), 9999) == arranger_source_phase(original, pool(0), 9999),
            "trim playback continuity");
        require(arranger_trim_clip_left(phases, project, right, 101), "fractional reveal");
        require(r->offset * 1680 + r->phase_remainder == expected, "left reveal restores exact phase");
        require(!arranger_trim_clip_left(phases, project, right, 100), "fractional trim collision rejected");
        require(r->offset * 1680 + r->phase_remainder == expected, "fractional failed trim unchanged");
    }
    arranger::Model invalid_phase;
    require(invalid_phase.insert({0,0,0,1,0,1,1680}) < 0, "invalid remainder rejected");
}
