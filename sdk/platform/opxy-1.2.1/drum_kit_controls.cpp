// The drum-kit controls' GUI role (SDK 1.23, drum_kit_controls.hpp): the kit table's
// lookup, the Shift edits and readouts of padVolume and padRandom, an ampEnvelope
// kit's keys past their amp values, and M2's per-key amp sync. Linked only with the
// kit_controls service, which a kit declaring any of them selects (tooling/native.py),
// and compiled, as every SDK unit of that build, with OPXY_KIT_CONTROLS.
#include "drum_kit_controls.hpp"
#include "drum_values.hpp"

using namespace opxy;
using namespace opxy::platform;

const DrumKit* sdk_drum_kit(u32 engine) {
    for (u32 i = 0; i < sdk_drum_kit_count; ++i)
        if (sdk_drum_kits[i].engine() == engine) return &sdk_drum_kits[i];
    return nullptr;
}

static bool amp_kit(const DrumMachine& machine) {
    const DrumKit* kit = sdk_drum_kit(machine.engine());
    return kit && (kit->controls & kKitAmp);
}

bool sdk_drum_kit_unpack(const DrumMachine& machine, const void* region, u8* model, u8* controls) {
    if (!amp_kit(machine)) return false;
    if (drum_marked(static_cast<const unsigned char*>(region)[24]))
        kit_amp_unpack(static_cast<const i32*>(region) + 1, model, controls, drum_codec(machine.model_count, machine.count));
    return true;
}

bool sdk_drum_kit_keeps(const DrumMachine& machine, const void* region) {
    i32 tops[kKitAmpValues];
    return region && amp_kit(machine) && drum_marked(static_cast<const unsigned char*>(region)[24]) &&
           kit_amp_tops(static_cast<const i32*>(region) + 1, tops);
}

void sdk_drum_kit_setup(const DrumMachine& machine, void* region) {
    const DrumKit* kit = sdk_drum_kit(machine.engine());
    if (!kit || !region) return;
    unsigned char* record = static_cast<unsigned char*>(region);
    if (kit->controls & kKitVolume) record[kDrumLevel] = 0;
    if (kit->controls & kKitRandom) static_cast<i32*>(region)[kDrumPadWord / 4] = 0;
}

// The selected key's record on an owned kit's track with the controls, or null.
static const unsigned char* selected_record(u32 track, const DrumKit** kit) {
    if (track >= kTracks) return nullptr;
    const DrumMachine* machine = sdk_drum_machine(track_engine(track));
    *kit = machine ? sdk_drum_kit(machine->engine()) : nullptr;
    return *kit ? static_cast<const unsigned char*>(pad_region(track, drum_edit_key(track))) : nullptr;
}

// A key the kit has not marked plays at 0 dB and without random (drum_kit_voice.hpp),
// so it reads so. The volume as stock shows a pad's gain ("0dB", "+3dB", "-12dB"),
// the random in percent.
bool sdk_drum_kit_readout(u32 track, u32 position, u8* choice, const char** title, char* value) {
    const DrumKit* kit = nullptr;
    const unsigned char* record = selected_record(track, &kit);
    const u32 shift = kit ? kit->shift(position) : 0;
    if (!shift || !choice || !title || !value) return false;
    const bool keyed = record && drum_marked(record[24]);
    u32 n = 0;
    if (shift == kKitVolume) {
        const i32 db = keyed ? kit_volume(static_cast<signed char>(record[kDrumLevel])) : 0;
        *choice = static_cast<u8>(kit_volume_choice(db));
        const u32 amount = static_cast<u32>(db < 0 ? -db : db);
        if (db) value[n++] = db < 0 ? '-' : '+';
        if (amount >= 10) value[n++] = static_cast<char>('0' + amount / 10);
        value[n++] = static_cast<char>('0' + amount % 10);
        value[n++] = 'd';
        value[n++] = 'B';
        value[n] = 0;
        *title = "VOLUME";
        return true;
    }
    i32 word = 0;
    if (keyed) __builtin_memcpy(&word, record + kDrumPadWord, sizeof word);
    u32 amount = kit_random_choice(word);
    *choice = static_cast<u8>(amount);
    if (amount >= 100) { value[n++] = '1'; amount -= 100; }
    if (n || amount >= 10) value[n++] = static_cast<char>('0' + amount / 10);
    value[n++] = static_cast<char>('0' + amount % 10);
    value[n++] = '%';
    value[n] = 0;
    *title = "RANDOM";
    return true;
}

// The key's volume in stock's own steps and range, 1 dB a detent, -30..+20 (its
// per-key editor, 8d4d60d8); or its random in 1% steps, ending exactly at none and at
// full scale. A key the kit has not marked starts from 0 dB and none (its setup). The
// key is then stored and published as sdk_drum_edit stores any edit.
bool sdk_drum_kit_edit(const DrumMachine& machine, u32 track, u32 key, void* region, u32 position, i32 delta,
                       u8 model, const u8* controls) {
    const DrumKit* kit = sdk_drum_kit(machine.engine());
    const u32 shift = kit && !machine.pad_mix ? kit->shift(position) : 0;
    if (!shift) return false;
    (void)track;
    unsigned char* record = static_cast<unsigned char*>(region);
    if (!record) return true;
    const bool keyed = drum_marked(record[24]);
    i32* pad_word = static_cast<i32*>(region) + kDrumPadWord / 4;
    i32 level = keyed ? static_cast<signed char>(record[kDrumLevel]) : 0, pad = keyed ? *pad_word : 0;
    if (shift == kKitVolume) {
        const i32 now = kit_volume(level);
        const i32 next = kit_volume(now + delta);
        if (next == now && next == level) return true;
        level = next;
    } else {
        i32 value = pad < 0 ? 0 : pad;
        const i32 room = delta > 0 ? (0x7fffffff - value) / kKitRandomStep : value / kKitRandomStep;
        const i32 turns = delta > 0 ? (delta < room ? delta : room) : (-delta < room ? delta : -room);
        if (!turns) return true;
        value += turns * kKitRandomStep;
        if (value > 0x7fffffff - kKitRandomStep / 2) value = 0x7fffffff;
        if (value < kKitRandomStep / 2) value = 0;
        pad = value;
    }
    const DrumKitKept kept = drum_kit_keep(machine, region);
    if (!drum_pack(model, controls, static_cast<i32*>(region) + 1, drum_codec(machine.model_count, machine.count)))
        return true;
    drum_kit_restore(kept, region);
    if (!keyed) sdk_drum_kit_setup(machine, region);
    record[24] = drum_mark(record[24]);
    if (shift == kKitVolume) record[kDrumLevel] = static_cast<unsigned char>(static_cast<signed char>(level));
    else *pad_word = pad;
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
    return true;
}

// Once a GUI frame (the samples service's frame hook, 8d51ea08, through
// sdk_sample_gui_tick): M2's amp words 8..11 follow the edit key (stock.hpp
// drum_edit_key: held, else selected) of an owned kit with ampEnvelope. When the
// selected pad changes (another key, track or pattern, or a project load, which fills
// the other project buffer) or its stored values change under it (a paste), the key's
// values, or the kit's defaults for those it has not stored, go to words 8..11 through
// stock's set (8d5acf28, which stores and publishes them). When words 8..11 then differ
// from what was put there, M2 has edited them: all four go to the key, marked as the
// kit's if it was not, and the key is published as Shift's edits are (8d585ee8,
// 8d4d5f18). The audio side reads them from the note payload. Any other kit's words
// 8..11 stay stock's.
namespace {
const u32 kProject = 0x86bd027c;
const u32 kAmpWord = 8;
struct AmpSync {
    const void* region;          // the pad last loaded, or null
    u32 track;
    i32 seen[kKitAmpValues];     // its words 1..4 as last loaded or stored
    i32 written[kKitAmpValues];  // words 8..11 as last put there
};
AmpSync sync;
}

// Stores an unmarked key's controls as it plays them now (its defaults) and marks it
// the kit's. Out of line, so the tick stays short.
static __attribute__((noinline)) bool mark_key(const DrumMachine& machine, u32 track, u32 key, unsigned char* record,
                                               i32* words) {
    u8 model, controls[kDrumControlCount];
    if (!drum_region_values(machine, track, key, record, &model, controls) ||
        !drum_pack(model, controls, words, drum_codec(machine.model_count, machine.count))) return false;
    sdk_drum_kit_setup(machine, record);
    record[24] = drum_mark(record[24]);
    return true;
}

void sdk_drum_amp_tick() {
    const u32 track = byte(kSelectedTrack);
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    const DrumKit* kit = machine ? sdk_drum_kit(machine->engine()) : nullptr;
    if (!kit || !(kit->controls & kKitAmp)) { sync.region = nullptr; return; }
    const u32 key = drum_edit_key(track);
    i32* words = static_cast<i32*>(pad_region(track, key));
    // The track's current pattern (8d587708) holds its words from +0x3864.
    const Address pattern = stock<Address(u32)>(0x8d587708)(track);
    if (!words || !pattern) { sync.region = nullptr; return; }
    words += 1;
    const volatile i32* base = at<const volatile i32>(pattern + 0x3864 + 4 * kAmpWord);
    bool moved = words != sync.region || track != sync.track;
    for (u32 i = 0; i < kKitAmpValues && !moved; ++i) moved = words[i] != sync.seen[i];
    if (moved) {
        const bool keyed = drum_marked(reinterpret_cast<unsigned char*>(words - 1)[24]);
        for (u32 i = 0; i < kKitAmpValues; ++i) {
            const i32 value = keyed ? kit_amp(words[i], kit->amp_default(i)) : kit->amp_default(i);
            if (base[i] != value) set_parameter(kProject, track, kAmpWord + i, value);
            sync.written[i] = value;
            sync.seen[i] = words[i];
        }
        sync.region = words;
        sync.track = track;
        return;
    }
    bool edited = false;
    for (u32 i = 0; i < kKitAmpValues; ++i) edited = edited || base[i] != sync.written[i];
    if (!edited) return;
    unsigned char* record = reinterpret_cast<unsigned char*>(words - 1);
    if (!drum_marked(record[24]) && !mark_key(*machine, track, key, record, words)) return;
    for (u32 i = 0; i < kKitAmpValues; ++i) {
        sync.written[i] = base[i];
        words[i] = kit_amp_store(words[i], sync.written[i]);
        sync.seen[i] = words[i];
    }
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
}
