// Authenticated OS 1.2.1 pattern records are value objects: stock's pattern
// clipboard (8d581a2c/8d581a74) copies all 0x53f4 bytes with memcpy. Sample
// paths are inline, not PCM pointers. Keep their resident cache entries alive
// until both the checkpoint and its recovery state release them.
#include "checkpoint.hpp"
#include "note_container.hpp"
#include "stock.hpp"
#include "stock_sample.hpp"
#include <extension_runtime.hpp>
#include <checkpoint_storage.hpp>

namespace opxy { namespace platform { namespace checkpoint {
namespace {
using extensions::CheckpointCommand;
using extensions::CheckpointResult;
const u32 kProject = 0x840a0524, kGeneration = 0x840a0538;
const u32 kController = 0x860c6adc, kRecorder = 0x860c072c;
const u32 kSampleManager = 0x84048894;
const u32 kPatternBytes = 0x53f4, kTrackBytes = 0x53f44, kPatternsOffset = 0xd7c;
const u32 kSceneOffset = 0x98, kSceneBytes = 33, kTracks = 16, kRecords = 64;
const u32 kScenes = (kPatternsOffset - kSceneOffset) / kSceneBytes;
const u32 kExtraBytes = 65536, kPins = kRecords * 24;

struct Record { PatternRef ref; u32 offset, words; };
struct Snapshot {
    u32 project, generation, scene, counts[kTracks], count, pin_count, pins[kPins], extra_bytes;
    u8 scene_data[kSceneBytes], links[18];
    alignas(4) u8 extra[kExtraBytes];
    Record records[kRecords];
    CheckpointStorage<96 * 1024> storage;
    bool valid;
};
// This unit and its storage are absent unless a selected extension uses it.
Snapshot snapshots[2];
Snapshot* saved = &snapshots[0];
Snapshot* recovery = &snapshots[1];
alignas(4) u8 extra_compare[kExtraBytes];

void copy(void* to, const void* from, u32 bytes) {
    auto* dst = static_cast<u8*>(to);
    const auto* src = static_cast<const u8*>(from);
    for (u32 i = 0; i < bytes; ++i) dst[i] = src[i];
}
bool equal(const void* a, const void* b, u32 bytes) {
    const auto* x = static_cast<const u8*>(a);
    const auto* y = static_cast<const u8*>(b);
    for (u32 i = 0; i < bytes; ++i) if (x[i] != y[i]) return false;
    return true;
}
u32 block(u32 project, u32 track, u32 index) {
    return project + kPatternsOffset + track * kTrackBytes + index * kPatternBytes;
}
u32 count(u32 project, u32 track) { return word(block(project, track, 0) + 16 * kPatternBytes); }
u32 scene_address(u32 project) { return project + kSceneOffset + byte(project + 6) * kSceneBytes; }

void release(Snapshot& s) {
    const u32 manager = word(kSampleManager);
    for (u32 i = 0; i < s.pin_count; ++i)
        stock<void(u32, u32)>(0x8d509df8)(manager, s.pins[i]);
    s.pin_count = 0;
    s.valid = false;
}
bool pin(Snapshot& s, const char* path) {
    if (!*path) return true;
    u32 length = 0;
    while (length < 96 && path[length]) ++length;
    if (length == 96 || !word(kSampleManager)) return false;
    const u32 key = stock<u32(const char*)>(0x8d4e813a)(path);
    for (u32 i = 0; i < s.pin_count; ++i) if (s.pins[i] == key) return true;
    if (s.pin_count == kPins) return false;
    const void* object = stock<const void*(u32, u32)>(0x8d509d70)(word(kSampleManager), key);
    if (!object) return false;
    const SampleView sample = stock_sample_view(object);
    if (!sample.data || !sample.frames) {
        stock<void(u32, u32)>(0x8d509df8)(word(kSampleManager), key);
        return false;
    }
    s.pins[s.pin_count++] = key;
    return true;
}
bool add(Snapshot& s, u32 track, u32 index) {
    for (u32 i = 0; i < s.count; ++i)
        if (s.records[i].ref.track == track && s.records[i].ref.index == index) return true;
    if (s.count == kRecords || index >= s.counts[track]) return false;
    Record& r = s.records[s.count++];
    r.ref = {track, index};
    const auto* data = at<const u8>(block(s.project, track, index));
    if (!notes::clearable(*reinterpret_cast<const notes::Container*>(data + 0x457c))) return false;
    if (!s.storage.append(reinterpret_cast<const u32*>(data), kPatternBytes / 4, r.offset, r.words)) return false;
    const u32 engine = *reinterpret_cast<const u32*>(data + 0x18);
    if (track < 8 && (stock<u32(u32)>(0x8d5b79c0)(engine) ||
                     sample_engine(engine)))
        for (u32 pad = 0; pad < 24; ++pad)
            if (!pin(s, reinterpret_cast<const char*>(data + 0x396c + pad * 128))) return false;
    return true;
}
bool available() {
    return word(kProject) && !word(kProject + 4) &&
        !byte(kRecorder) && !byte(kRecorder + 1) && !byte(kPlaying + 2) && !byte(kPlaying + 3);
}
bool capture(Snapshot& s, const Snapshot* affected = nullptr) {
    release(s);
    s.project = word(kProject);
    s.generation = word(kGeneration);
    s.scene = byte(s.project + 6);
    if (s.scene >= kScenes) return false;
    s.count = 0;
    s.storage.used = 0;
    copy(s.scene_data, at<const void>(scene_address(s.project)), kSceneBytes);
    copy(s.links, at<const void>(s.project + 0xc), sizeof s.links);
    const u32 linked = s.links[0] | static_cast<u32>(s.links[1]) << 8;
    for (u32 t = 0; t < kTracks; ++t) {
        s.counts[t] = count(s.project, t);
        if (!s.counts[t] || s.counts[t] > 16 || !add(s, t, s.scene_data[t]) ||
            ((linked & (1u << t)) && !add(s, t, s.links[2 + t]))) {
            release(s);
            return false;
        }
    }
    // Recovery must include the original targets as well as the current scene's
    // selections. Otherwise changing a scene assignment could lose edits to an
    // original pattern that recall overwrites but recovery no longer selected.
    if (affected)
        for (u32 i = 0; i < affected->count; ++i)
            if (!add(s, affected->records[i].ref.track, affected->records[i].ref.index)) {
                release(s);
                return false;
            }
    s.extra_bytes = 0;
    if (support().save) {
        PatternRef refs[kRecords];
        for (u32 i = 0; i < s.count; ++i) refs[i] = s.records[i].ref;
        if (!support().save(s.extra, s.extra_bytes, kExtraBytes, refs, s.count)) {
            release(s);
            return false;
        }
    }
    s.valid = true;
    return true;
}
CheckpointResult validate(const Snapshot& s) {
    if (!s.valid) return CheckpointResult::empty;
    const u32 project = word(kProject);
    if (project != s.project || word(kGeneration) != s.generation) return CheckpointResult::empty;
    if (byte(project + 6) != s.scene) return CheckpointResult::wrong_scene;
    for (u32 t = 0; t < kTracks; ++t)
        if (count(project, t) != s.counts[t]) return CheckpointResult::changed;
    if (s.count > kRecords || s.extra_bytes > kExtraBytes) return CheckpointResult::changed;
    if (support().validate && !support().validate(s.extra, s.extra_bytes)) return CheckpointResult::changed;
    // Validate every destination and every currently selected pattern before
    // opening the transaction. A still-recording note cannot be overwritten.
    for (u32 i = 0; i < s.count; ++i) {
        const PatternRef& p = s.records[i].ref;
        if (p.track >= kTracks || p.index >= s.counts[p.track] ||
            !s.storage.valid(s.records[i].offset, s.records[i].words, kPatternBytes / 4))
            return CheckpointResult::changed;
        if (!notes::clearable(*at<const notes::Container>(block(project, p.track, p.index) + 0x457c)))
            return CheckpointResult::busy;
    }
    return CheckpointResult::success;
}
bool matches_live(const Snapshot& s) {
    if (!equal(s.scene_data, at<const void>(scene_address(s.project)), kSceneBytes) ||
        !equal(s.links, at<const void>(s.project + 0xc), sizeof s.links)) return false;
    for (u32 i = 0; i < s.count; ++i) {
        const Record& r = s.records[i];
        const u32* live = at<const u32>(block(s.project, r.ref.track, r.ref.index));
        if (!s.storage.visit(r.offset, r.words, kPatternBytes / 4, [live](u32 k, u32 stored) {
            const u32 mask = k == 0x14 / 4 ? ~8u : ~0u;
            return (stored & mask) == (live[k] & mask);
        })) return false;
    }
    if (support().save) {
        PatternRef refs[kRecords];
        for (u32 i = 0; i < s.count; ++i) refs[i] = s.records[i].ref;
        u32 bytes = 0;
        if (!support().save(extra_compare, bytes, kExtraBytes, refs, s.count) || bytes != s.extra_bytes ||
            !equal(extra_compare, s.extra, bytes)) return false;
    }
    return true;
}
void apply(const Snapshot& s) {
    stock<void(u32)>(0x8d591948)(kController);  // discard stock's stale single-track undo identities
    for (u32 i = 0; i < s.count; ++i) {
        const Record& r = s.records[i];
        const u32 target = block(s.project, r.ref.track, r.ref.index);
        u32* destination = at<u32>(target);
        s.storage.visit(r.offset, r.words, kPatternBytes / 4, [destination](u32 index, u32 value) {
            destination[index] = value;
            return true;
        });
        *at<u8>(target + 0x14) &= static_cast<u8>(~8u);  // stock's edited-pattern flag
    }
    copy(at<void>(s.project + 0xc), s.links, sizeof s.links);
    // Stock's scene paste validates the assignments, marks the project dirty
    // and advances its edit revision, without touching transport or the clipboard.
    stock<void(const void*)>(0x8d585b90)(s.scene_data);
    if (support().load) support().load(s.extra, s.extra_bytes);
    for (u32 t = 0; t < kTracks; ++t) {
        stock<void(u32)>(0x8d587668)(t);
        // The same refresh stock uses after pasting a pattern: publish sound,
        // sample assignments and sequencer iterators at the current clock time.
        stock<void(u32, u32)>(0x8d597730)(kController, t);
    }
}
}

void sync() {
    for (auto& s : snapshots)
        if (s.valid && (s.project != word(kProject) || s.generation != word(kGeneration))) release(s);
}
extensions::CheckpointResult command(extensions::CheckpointCommand cmd) {
    sync();
    if (!available()) return CheckpointResult::busy;
    if (cmd == CheckpointCommand::store) {
        if (!capture(*recovery)) return CheckpointResult::unavailable;
        Snapshot* old = saved; saved = recovery; recovery = old;
        release(*recovery);
        return CheckpointResult::success;
    }
    Snapshot& target = cmd == CheckpointCommand::undo ? *recovery : *saved;
    const CheckpointResult result = validate(target);
    if (result != CheckpointResult::success) return result;
    if (cmd == CheckpointCommand::undo) {
        apply(target);
        release(target);
        return CheckpointResult::success;
    }
    // Preserve a useful recovery state when repeatedly recalling an unchanged
    // checkpoint. Capture uses the spare buffer and never alters the baseline.
    if (matches_live(*saved)) return CheckpointResult::success;
    if (!capture(*recovery, saved)) return CheckpointResult::unavailable;
    apply(*saved);
    return CheckpointResult::success;
}
}}}

extern "C" const opxy::platform::CheckpointBackend* sdk_checkpoint_backend() {
    static const opxy::platform::CheckpointBackend backend = {
        opxy::platform::checkpoint::sync, opxy::platform::checkpoint::command};
    return &backend;
}
