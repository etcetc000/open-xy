// The inert voice object a sample adapter below 256 voices returns for an
// index it refuses: every call stock makes through its method table is safe,
// render writes silence over the whole block, nothing changes the object, and
// each refusal is counted. Expanded twice (stereo and mono) by the test.
#include <opxy/types.hpp>
#include <cstdio>
#include <cstring>
namespace opxy { namespace platform { const __UINTPTR_TYPE__ kVoiceDefaultMethod = 0x8d5051c4u; } }
namespace wide {
#include "refusal_wide.inc"
}
namespace narrow {
#include "refusal_narrow.inc"
}
static int failures = 0;
static void require(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
typedef int (*Start)(void*, opxy::u32);
typedef int (*Plain)(void*);
typedef int (*Stereo)();
typedef void (*Noop)();
template <typename Refuse>
static void check(Refuse refuse, const opxy::u32& count, unsigned words, int stereo, const char* name) {
    const opxy::u32 before = count;
    void* object = refuse();
    require(object && object == refuse(), "one shared object");
    require(count == before + 2, "each refusal is counted");
    const void* const* table = *static_cast<const void* const* const*>(object);
    unsigned char snapshot[sizeof(void*)];
    std::memcpy(snapshot, object, sizeof snapshot);
    opxy::i32 output[129];
    for (opxy::i32& word : output) word = 0x5a5a5a5a;
    const opxy::i32 parameters[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    require(reinterpret_cast<Render>(table[2])(object, output, parameters) == 0, "render returns 0");
    for (unsigned i = 0; i < words; ++i) require(output[i] == 0, "render writes silence");
    require(output[words] == 0x5a5a5a5a, "render stays within the block");
    require(reinterpret_cast<Plain>(table[0])(object) == 0 && reinterpret_cast<Plain>(table[1])(object) == 0,
            "destroy entries do nothing");
    require(reinterpret_cast<Start>(table[4])(object, 60) == 0, "start does nothing");
    require(reinterpret_cast<Plain>(table[6])(object) == 0 && reinterpret_cast<Plain>(table[9])(object) == 0,
            "query entries answer 0");
    reinterpret_cast<Noop>(table[3])();
    reinterpret_cast<Noop>(table[5])();
    reinterpret_cast<Noop>(table[7])();
    require(reinterpret_cast<Stereo>(table[8])() == stereo, "stereo answers the machine's layout");
    require(reinterpret_cast<__UINTPTR_TYPE__>(table[10]) == 0x8d5051c4u, "stock default method last");
    require(std::memcmp(snapshot, object, sizeof snapshot) == 0, "no call changes the object");
    if (!failures) std::printf("PASS: %s refusal\n", name);
}
int main() {
    check(wide::refused, wide::wide_voice_refusals, 128, 1, "stereo");
    check(narrow::refused, narrow::narrow_voice_refusals, 64, 0, "mono");
    return failures ? 1 : 0;
}
