void host_barrier();
#include "../../sdk/platform/opxy-1.2.1/arranger_resolver.hpp"
#include "../../sdk/platform/opxy-1.2.1/arranger_clip.hpp"
extern "C" int puts(const char*);
extern "C" void exit(int);
using namespace opxy;
void require(bool ok, const char* message) { if (!ok) { puts(message); exit(1); } }
alignas(4) static u8 project[0xd7c + 8 * 0x53f44];
static u32 initialization_calls;
static u32 publication_barriers;
void host_barrier() {
    const u32 pool = 0xd7c + 7 * 0x53f44;
    const u32 expected = publication_barriers / 2 + 1;
    require(project[pool + 0x53f40] == expected + (publication_barriers % 2),
        "source count published only between barriers");
    require(project[pool + expected * 0x53f4 + 0x457c] == 0,
        "source initialized before count publication");
    ++publication_barriers;
}
void initialize_empty(u8* pattern) {
    ++initialization_calls;
    pattern[0] = 16; pattern[8] = 3;
    for (u32 i = 0x457c; i < 0x53f4; ++i) pattern[i] = 0;
}
int main() {
    using opxy::platform::arranger_pattern_source;
    const u32 pool = 0xd7c + 7 * 0x53f44;
    project[pool + 0x53f40] = 16;
    project[6] = 17;
    project[0x98 + 17 * 0x21 + 7] = 2;
    require(arranger_pattern_source(project, 7, 15, false) == project + pool + 15 * 0x53f4,
        "explicit clip selection ignores current scene");
    require(project[0x98 + 17 * 0x21 + 7] == 2, "source scene unchanged");
    project[12] = 128;
    project[14 + 7] = 4;
    require(arranger_pattern_source(project, 7, 15, true) == project + pool + 4 * 0x53f4,
        "instrument lock preserved");
    require(arranger_pattern_source(project, 7, 15, false) == project + pool + 15 * 0x53f4,
        "instrument lock does not change notes source");
    project[14 + 7] = 16;
    require(!arranger_pattern_source(project, 7, 15, true), "invalid locked pattern rejected");
    project[pool + 0x53f40] = 4;
    require(!arranger_pattern_source(project, 7, 4, false), "count boundary");
    project[pool + 0x53f41] = 1;
    require(!arranger_pattern_source(project, 7, 0, false), "corrupt count rejected");
    require(!arranger_pattern_source(nullptr, 0, 0, false), "null project");
    require(!arranger_pattern_source(project, 8, 0, false), "aux track excluded");
    require(!arranger_pattern_source(project, 7, 16, false), "pattern boundary");
    // Appending a source never alters existing notes, scene identities, or sound data.
    project[pool + 0x53f41] = 0;
    project[pool + 0x53f40] = 1;
    project[12] = 0;
    project[pool + 24] = 77;
    project[pool + 0x457c] = 99;
    arranger::Model clips;
    i32 added = platform::arranger_add_empty_clip(clips, project, 7, 0, 0, initialize_empty);
    require(added == 0 && initialization_calls == 1, "empty source appended");
    require(project[pool + 0x53f40] == 2 && clips.clip(0)->pattern == 1,
        "source count and clip refer to new slot");
    require(project[pool + 0x53f4 + 24] == 77 && project[pool + 0x53f4 + 0x457c] == 0 &&
        project[pool + 0x457c] == 99, "sound copied while original notes preserved");
    require(project[0x98 + 17 * 0x21 + 7] == 2, "append preserves scene references");
    require(platform::arranger_add_empty_clip(clips, project, 7, 0, 0, initialize_empty) == -1 &&
        project[pool + 0x53f40] == 2 && initialization_calls == 1, "collision creates no orphan source");
    require(platform::arranger_add_empty_clip(clips, project, 7, 0xffffffff, 0, initialize_empty) == -1,
        "overflow creates no source");
    require(platform::arranger_add_empty_clip(clips, project, 7, 7680, 4, initialize_empty) == -1,
        "invalid source rejected");
    project[12] = 128;
    project[14 + 7] = 1;
    project[pool + 0x53f4 + 24] = 88;
    require(platform::arranger_add_empty_clip(clips, project, 7, 7680, 0, initialize_empty) >= 0 &&
        project[pool + 2 * 0x53f4 + 24] == 88, "new clip copies audible locked instrument");
    project[pool + 0x53f40] = 16;
    require(platform::arranger_add_empty_clip(clips, project, 7, 15360, 0, initialize_empty) == -1 &&
        !clips.clip(2) && initialization_calls == 2, "full source pool preserves arrangement");
    const u32 times[] = {0, 1, 479, 480, 1920, 0x7fffffff, 0xffffffff};
    const u32 rates[] = {5040, 3360, 2240, 1680, 1260, 840, 560, 420, 336, 280, 240, 210, 140, 105};
    const u32 periods[] = {480 * 1680, 255 * 480 * 1680, 0xffffffff};
    for (u32 time : times) for (u32 rate : rates) for (u32 period : periods)
        require(opxy::platform::arranger_multiply_modulo(time, rate, period) ==
            static_cast<u32>((opxy::i64(time) * rate) % period), "fractional scale and long seek conversion");
    // Stock admits events <= floor(raw_end/1680). A clip's excluded endpoint
    // must remain excluded even at every supported fractional track scale.
    for (u32 rate : rates) {
        const u32 delta = 1680;
        const u32 raw_end = delta * rate;
        const u32 excluded_event = raw_end / 1680;
        require((raw_end - 1) / 1680 < excluded_event, "clip/loop end excluded at every scale");
        const i32 next_previous = -1;
        require(next_previous < 0 && 0 <= i32(rate / 1680), "next clip event zero admitted after seek");
    }
}
