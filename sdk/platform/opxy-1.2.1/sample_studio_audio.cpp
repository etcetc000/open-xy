// Sample Studio's capability, audio board (opxy/sample_studio.hpp, docs/sample-studio.md): the
// master bus, the B3 receiver and the mixer's hooks handed to the extension's player, and the
// services it uses on this board, each a stock call or global of OS 1.2.1.
#include <opxy/sample_studio.hpp>
#include "stock.hpp"

using opxy::i32;
using opxy::u32;
using opxy::platform::stock;
using opxy::platform::word;
namespace studio = opxy::studio;

extern "C" {
void sdk_master_process(void* object, i32* left, i32* right) __asm__("sdk_master_process");
void sdk_master_receive(u32 base, u32 id, i32 value) __asm__("sdk_master_receive");
void sdk_studio_voice(i32* out, u32 track) __asm__("sdk_studio_voice");
void sdk_studio_gain(u32 track, i32 gain) __asm__("sdk_studio_gain");
void sdk_studio_level(u32 track, i32 level) __asm__("sdk_studio_level");
void sdk_studio_mixer_begin() __asm__("sdk_studio_mixer_begin");
// The extension's player (generated beside its object, tooling/extensions.py).
const opxy::studio::Player* sdk_sample_studio_player() __asm__("sdk_sample_studio_player");
}

namespace {
// The master EQ call at 8d511b80 runs once a block (64 stereo frames, Q31, separate left and right
//    arrays) after dry + FX I + FX II are summed and before the master compressor and master volume.
//    Stock's EQ first, then the player.
// B3 IDs 146..160 are the player's channels 0..14 (after stock's 0..3, the master processor's
// 128..137 and the device identity's 138..145); every other ID goes to stock's receiver.
const u32 kFirstId = 146, kChannels = 15;
// The send effects' input buses this block (FX I's C, FX II's I), interleaved stereo Q31.
i32* const kFxBuses[2] = {reinterpret_cast<i32*>(0x84caa140), reinterpret_cast<i32*>(0x84caa340)};
// The sample pool stock loads samples into (mutex, then the dlmalloc mspace).
const u32 kSamplePool = 0x08039edc;
// Stock's sample cache on this board: the manager (its lock at +4), the map and a node's fields.
const u32 kSampleManager = 0x84048894, kCacheMap = 0x08039f24;
const u32 kNodeObject = 0x14, kNodeCount = 0x18, kNodeLoaded = 0x1d;
const studio::Player* player() { return sdk_sample_studio_player(); }
}  // namespace

namespace opxy { namespace studio {
void* pool_allocate(u32 bytes) {
    const u32 mutex = word(kSamplePool), space = word(kSamplePool + 4);
    stock<void(u32, i32)>(0x8c03e6f4)(mutex, -1);
    void* p = stock<void*(u32, u32)>(0x8c048788)(space, bytes);
    stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(mutex, 0, 0, 0);
    return p;
}
void pool_free(void* block) {
    const u32 mutex = word(kSamplePool), space = word(kSamplePool + 4);
    stock<void(u32, i32)>(0x8c03e6f4)(mutex, -1);
    stock<void(u32, void*)>(0x8c048a26)(space, block);
    stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(mutex, 0, 0, 0);
}
u32 pool_in_use() { return stock<u32()>(0x8d509608)(); }
// A cached sample held while it plays, counted as stock counts a voice's hold.
const void* pin(u32 key) {
    const u32 lock = word(kSampleManager) + 4;
    stock<void(u32)>(0x8d5be032)(lock);
    u32 wanted = key;
    unsigned char* node = stock<unsigned char*(u32, u32*)>(0x8d509824)(kCacheMap, &wanted);
    const void* object = nullptr;
    if (reinterpret_cast<u32>(node) != word(kCacheMap + 4)) {
        const u32 o = *reinterpret_cast<volatile u32*>(node + kNodeObject);
        if (node[kNodeLoaded] && o && word(o) && word(o + 0x30)) {
            *reinterpret_cast<volatile u32*>(node + kNodeCount) += 1;
            object = reinterpret_cast<const void*>(o);
        }
    }
    stock<void(u32)>(0x8d5be060)(lock);
    return object;
}
void unpin(u32 key) { stock<void(u32, u32)>(0x8d509df8)(word(kSampleManager), key); }
i32* fx_bus(u32 send) { return send < 2 ? kFxBuses[send] : nullptr; }
}}  // namespace opxy::studio

void sdk_master_process(void* object, i32* left, i32* right) {
    stock<void(void*, i32*, i32*)>(0x11a02c10)(object, left, right);   // stock's master EQ, as before
    if (const studio::Player* p = player()) p->master(left, right);
}
void sdk_master_receive(u32 base, u32 id, i32 value) {
    const studio::Player* p = player();
    if (p && id >= kFirstId && id < kFirstId + kChannels) {
        p->receive(id - kFirstId, value);
        return;
    }
    stock<void(u32, u32, i32)>(0x8d50eb8c)(base, id, value);
}
void sdk_studio_mixer_begin() {
    if (const studio::Player* p = player()) p->mixer_begin();
}
void sdk_studio_voice(i32* out, u32 track) {
    if (const studio::Player* p = player()) p->voice(out, track);
}
void sdk_studio_gain(u32 track, i32 gain) {
    if (const studio::Player* p = player()) p->track_gain(track, gain);
}
void sdk_studio_level(u32 track, i32 level) {
    if (const studio::Player* p = player()) p->track_level(track, level);
}
