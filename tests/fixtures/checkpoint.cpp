#include <checkpoint.cpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace opxy;
using namespace opxy::platform;
namespace cp = opxy::platform::checkpoint;
using Command = extensions::CheckpointCommand;
using Result = extensions::CheckpointResult;
namespace {
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
const u32 project_address = 0x87000000;
alignas(4) u8 project[0xd7c + 16 * 0x53f44];
u32 project_id = project_address, generation = 1, loading, manager = 17;
u32 record_mode, record_alt, playing, counting, armed, held;
u32 refreshes, publications, resets, sample_refs[256], acquire_calls, release_calls;
u32 extra_state = 100, clock_position = 12345, clipboard = 777;
bool missing_sample, fail_extra, bad_extra, check_complete;
alignas(8) u8 resident_sample[64];
i16 pcm[2];
u8* pattern(u32 t, u32 p = 0) { return project + 0xd7c + t * 0x53f44 + p * 0x53f4; }
u32& value(u32 t, u32 offset, u32 p = 0) { return *reinterpret_cast<u32*>(pattern(t, p) + offset); }
u32& pattern_count(u32 t) { return *reinterpret_cast<u32*>(project + 0xd7c + t * 0x53f44 + 16 * 0x53f4); }
u8* scene(u32 n = 0) { return project + 0x98 + n * 33; }
u32 hash(const char* path) { u32 h = 0; while (*path) h = (h * 31 + static_cast<u8>(*path++)) & 255; return h; }
const void* acquire(u32 m, u32 key) {
    require(m == manager && key < 256, "sample acquire arguments"); ++acquire_calls;
    if (missing_sample) return nullptr;
    ++sample_refs[key]; return resident_sample;
}
void release(u32 m, u32 key) {
    require(m == manager && key < 256 && sample_refs[key], "sample references balanced");
    --sample_refs[key]; ++release_calls;
}
u32 file_engine(u32 engine) { return engine == 2; }
void reset_undo(u32 controller) { require(controller == 0x860c6adc, "undo controller"); ++resets; }
void paste_scene(const void* data) { std::memcpy(scene(project[6]), data, 33); }
void publish(u32 track) { require(track < 16, "publication track"); ++publications; }
void refresh(u32 controller, u32 track) {
    require(controller == 0x860c6adc && track < 16, "refresh arguments"); ++refreshes;
    if (check_complete) require(value(15, 0x258) == 115, "all patterns restored before first publication");
}
bool save_extra(u8* data, u32& size, u32 capacity, const cp::PatternRef*, u32 count) {
    require(capacity <= 65536 && count >= 16 && count <= 64, "bounded extra capture");
    if (fail_extra || capacity < 4) return false;
    std::memcpy(data, &extra_state, 4); size = 4; return true;
}
bool matches_extra(const u8* data, u32 bytes, const cp::PatternRef*, u32) {
    return bytes == 4 && std::memcmp(data, &extra_state, 4) == 0;
}
bool validate_extra(const u8*, u32 bytes) { return bytes == 4 && !bad_extra; }
void load_extra(const u8* data, u32 bytes) { require(bytes == 4, "extra restore size"); std::memcpy(&extra_state, data, 4); }
bool owned_sample(u32 engine) { return engine == 34; }
u32 references() { u32 n = 0; for (u32 ref : sample_refs) n += ref; return n; }
void initialize() {
    *reinterpret_cast<u32*>(resident_sample) = 1;
    *reinterpret_cast<const i16**>(resident_sample + 8) = pcm;
    *reinterpret_cast<u32*>(resident_sample + 0x30) = 2;
    *reinterpret_cast<u32*>(resident_sample + 0x38) = 44100;
    for (u32 t = 0; t < 16; ++t) {
        pattern_count(t) = 2;
        for (u32 p = 0; p < 2; ++p) {
            auto& notes = *reinterpret_cast<notes::Container*>(pattern(t, p) + 0x457c);
            for (u32 i = 0; i < notes::capacity; ++i) {
                notes.slots[i].previous = -1;
                notes.slots[i].next = i + 1 < notes::capacity ? static_cast<i16>(i + 1) : -1;
            }
            notes.head = notes.tail = -1; notes.free = 0; notes.count = 0;
            pattern(t, p)[0] = 16;
            value(t, 0x18, p) = 18;
            value(t, 0x258, p) = 100 + t;
        }
    }
    project[0xc] = 1; project[0xe] = 1;  // track 0 links its sound to pattern 1
    value(0, 0x18, 1) = 2;
    std::strcpy(reinterpret_cast<char*>(pattern(0, 1) + 0x396c), "user/kick.wav");
}
}
namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    if (address >= project_address && address < project_address + sizeof project)
        return reinterpret_cast<Address>(project + (address - project_address));
    switch (address) {
        case 0x840a0524: return reinterpret_cast<Address>(&project_id);
        case 0x840a0528: return reinterpret_cast<Address>(&loading);
        case 0x840a0538: return reinterpret_cast<Address>(&generation);
        case 0x84048894: return reinterpret_cast<Address>(&manager);
        case 0x860c072c: return reinterpret_cast<Address>(&record_mode);
        case 0x860c072d: return reinterpret_cast<Address>(&record_alt);
        case 0x860c9b39: return reinterpret_cast<Address>(&playing);
        case 0x860c9b3a: return reinterpret_cast<Address>(&counting);
        case 0x860c9b3b: return reinterpret_cast<Address>(&armed);
        case 0x860c9b3c: return reinterpret_cast<Address>(&held);
        case 0x8d4e813a: return reinterpret_cast<Address>(&hash);
        case 0x8d509d70: return reinterpret_cast<Address>(&acquire);
        case 0x8d509df8: return reinterpret_cast<Address>(&release);
        case 0x8d5b79c0: return reinterpret_cast<Address>(&file_engine);
        case 0x8d591948: return reinterpret_cast<Address>(&reset_undo);
        case 0x8d585b90: return reinterpret_cast<Address>(&paste_scene);
        case 0x8d587668: return reinterpret_cast<Address>(&publish);
        case 0x8d597730: return reinterpret_cast<Address>(&refresh);
    }
    require(false, "unexpected stock boundary"); return 0;
}
namespace checkpoint {
const Support& support() { static const Support s = {save_extra, matches_extra, validate_extra, load_extra}; return s; }
bool sample_engine(u32 engine) { return owned_sample(engine); }
}
}}
int main() {
    initialize();
    require(cp::command(Command::restore) == Result::empty, "empty recall");
    require(cp::command(Command::store) == Result::success && references() == 1, "store pins sample");
    for (u32 t = 0; t < 16; ++t) value(t, 0x258) = 500 + t;
    value(0, 0x3864, 1) = 333;
    std::strcpy(reinterpret_cast<char*>(pattern(0, 1) + 0x396c), "user/snare.wav");
    scene()[16] = 2; extra_state = 999; project[0] = 0x34; project[1] = 0x12;
    check_complete = true;
    require(cp::command(Command::restore) == Result::success, "recall complete scene");
    require(refreshes == 16 && publications == 16 && resets == 1, "one coherent publication");
    require(value(0, 0x3864, 1) == 0 && !scene()[16] && extra_state == 100, "linked sound, mute and SDK state");
    require(project[0] == 0x34 && project[1] == 0x12 && clock_position == 12345 && clipboard == 777, "unrelated state preserved");
    require(references() == 2, "baseline and recovery retain their resources");
    require(cp::recovery->storage.used < cp::saved->storage.used / 8,
            "small edits share most of the checkpoint's pattern data");
    require(cp::command(Command::restore) == Result::success && refreshes == 16, "redundant recall no-op");
    check_complete = false;
    value(15, 0x258) = 900;
    require(cp::command(Command::undo) == Result::success && value(15, 0x258) == 515 && extra_state == 999,
            "recovery survived redundant recall");
    require(cp::command(Command::undo) == Result::empty, "one recovery, no stale redo");
    require(!std::strcmp(reinterpret_cast<char*>(pattern(0, 1) + 0x396c), "user/snare.wav"), "sample recovery");
    // Recall can replace scene assignments and sound-link configuration. Its
    // recovery must also cover edited original targets that stopped playing.
    scene()[15] = 1; project[0xc] = 0;
    value(15, 0x258) = 615; value(15, 0x258, 1) = 715;
    require(cp::command(Command::restore) == Result::success && scene()[15] == 0 && value(15, 0x258) == 115,
            "recall original active pattern set");
    require(cp::command(Command::undo) == Result::success && scene()[15] == 1 && value(15, 0x258) == 615 &&
            value(15, 0x258, 1) == 715 && project[0xc] == 0, "recovery includes displaced original targets");
    scene()[15] = 0; project[0xc] = 1; value(15, 0x258) = 515;
    missing_sample = true;
    require(cp::command(Command::store) == Result::unavailable && references() == 1, "failed store preserves baseline");
    missing_sample = false; fail_extra = true;
    require(cp::command(Command::store) == Result::unavailable && references() == 1, "extra capture failure releases staged pins");
    fail_extra = false;
    project[6] = 1;
    require(cp::command(Command::restore) == Result::wrong_scene && value(15, 0x258) == 515, "wrong scene refused");
    project[6] = 0; ++pattern_count(0);
    require(cp::command(Command::restore) == Result::changed, "changed pattern topology refused");
    --pattern_count(0); armed = 1;
    require(cp::command(Command::restore) == Result::busy && value(15, 0x258) == 515, "recording refused without edits");
    armed = 0; bad_extra = true;
    require(cp::command(Command::restore) == Result::changed && value(15, 0x258) == 515, "extra validation precedes edits");
    bad_extra = false;
    require(cp::command(Command::restore) == Result::success && extra_state == 100, "baseline still usable after refusals");
    ++generation; cp::sync();
    require(!references() && cp::command(Command::restore) == Result::empty && cp::command(Command::undo) == Result::empty,
            "same-address project replacement clears both states and releases pins");
    require(acquire_calls > release_calls && references() == 0, "failed acquire did not create a reference");
    value(0, 0x18, 1) = 34;
    require(cp::command(Command::store) == Result::success && references() == 1, "owned sample-engine resources retained");
    ++generation; cp::sync();
    *reinterpret_cast<u32*>(resident_sample + 0x30) = 0;
    require(cp::command(Command::store) == Result::unavailable && !references(), "unready PCM refused and released");
    *reinterpret_cast<u32*>(resident_sample + 0x30) = 2;
    project[6] = 99;
    require(cp::command(Command::store) == Result::success, "last scene record supported");
    project[6] = 100;
    require(cp::command(Command::store) == Result::unavailable, "scene outside stored table refused");
    project[6] = 99;
    require(cp::command(Command::restore) == Result::success, "invalid scene store preserved last checkpoint");

    // Thirty-two incompressible active/source records exceed a snapshot's
    // capacity. Refusing either store or recovery capture must be atomic.
    project[0xc] = project[0xd] = 255;
    for (u32 t = 0; t < 16; ++t) {
        project[0xe + t] = 1;
        for (u32 p = 0; p < 2; ++p) {
            for (u32 offset = 0; offset < 0x396c; offset += 4)
                value(t, offset, p) = 1 + t * 0x10000 + p * 0x1000 + offset;
            value(t, 0x18, p) = 18;
        }
    }
    const u32 live_value = value(15, 0x258), prior_refs = references(), prior_refreshes = refreshes;
    require(cp::command(Command::store) == Result::unavailable && references() == prior_refs &&
            value(15, 0x258) == live_value && refreshes == prior_refreshes,
            "snapshot exhaustion preserves baseline and live state");
    require(cp::command(Command::restore) == Result::unavailable && references() == prior_refs &&
            value(15, 0x258) == live_value && refreshes == prior_refreshes,
            "recovery exhaustion refuses before applying baseline");
    std::memset(project, 0, sizeof project);
    initialize();
    project[6] = 99;
    require(cp::command(Command::restore) == Result::success && value(15, 0x258) == 115,
            "baseline remains recallable after capacity failures");

    // A dense checkpoint leaves less room for a second full store, while a
    // few changes can still use recovery deltas in the remaining space.
    ++generation;
    for (u32 t = 0; t < 16; ++t)
        for (u32 p = 0; p < 2; ++p) {
            for (u32 offset = 0; offset < 0x457c; offset += 4)
                value(t, offset, p) = 1 + t * 0x10000 + p * 0x1000 + offset;
            for (u32 offset = 0x4d04; offset < 0x53f4; offset += 4)
                value(t, offset, p) = 1 + offset;
            value(t, 0x18, p) = 18;
        }
    require(cp::command(Command::store) == Result::success && cp::extent(*cp::saved) > cp::kPoolWords / 2,
            "dense checkpoint fits on its own");
    const u32 dense_baseline = value(15, 0x258);
    value(15, 0x258) = 500;
    require(cp::command(Command::store) == Result::unavailable && value(15, 0x258) == 500,
            "shared pool exhaustion preserves live data and the previous checkpoint");
    require(cp::command(Command::restore) == Result::success && value(15, 0x258) == dense_baseline,
            "deltas fit alongside a dense checkpoint");
    require(cp::command(Command::undo) == Result::success && value(15, 0x258) == 500,
            "dense delta recovers the prior edits");

    std::memset(project, 0, sizeof project);
    initialize();
    project[6] = 99;
    require(cp::command(Command::store) == Result::success, "replace a dense checkpoint with a sparse one");
    value(15, 0x258) = 600;
    require(cp::command(Command::restore) == Result::success && value(15, 0x258) == 115,
            "replacement remains valid after moving it to the start of the pool");
    return 0;
}
