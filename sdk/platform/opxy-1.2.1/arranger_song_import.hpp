#pragma once
#include <opxy/arranger.hpp>

namespace opxy { namespace platform {
enum class SongImportResult : u8 { Ok, Empty, Invalid, UnsupportedState, NoRoom, CurrentSceneMuted };

inline u32 arranger_song_word(const u8* p) {
    return p[0] | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

// OS 1.2.1: 586014 selects all sixteen tracks from these inline pools.
inline const u8* arranger_song_source(const u8* project, u32 track, u32 pattern) {
    if (!project || track >= 16 || pattern >= 16) return nullptr;
    const u8* pool = project + 0xd7c + track * 0x53f44;
    const u32 count = arranger_song_word(pool + 0x53f40);
    if (!count || count > 16 || pattern >= count) return nullptr;
    const u8* source = pool + pattern * 0x53f4;
    return source[0] && source[0] <= 64 && source[8] < 16 ? source : nullptr;
}

// Native scene duration 11a0a740 uses table 840a9ec0, not the playback
// phase-rate table. Includes AUX/send tracks; state 2 is the stored mute.
inline SongImportResult arranger_song_scene_ticks(const u8* project, const u8* scene, u32& ticks) {
    static const u8 numerator[16] = {3,2,4,1,3,1,1,1,1,1,1,1,1,1,1,1};
    static const u8 denominator[16] = {1,1,3,1,4,2,3,4,5,6,7,8,9,12,16,32};
    const u32 mode = project[8] & 7;
    if (mode > 2) return SongImportResult::Invalid;
    ticks = mode == 1 ? 0xffffffffu : 0;
    bool included = false;
    for (u32 track = 0; track < 16; ++track) {
        const u8* source = arranger_song_source(project, track, scene[track]);
        if (!source) return SongImportResult::Invalid;
        if (scene[16 + track] == 2) continue;
        if (scene[16 + track] != 0) return SongImportResult::UnsupportedState;
        // Linear's existing 1/1680-tick phase model cannot exactly represent
        // native 1/9 and 1/32 rates. AUX durations are still calculated exactly.
        if (track < 8 && (source[8] == 12 || source[8] == 15)) return SongImportResult::UnsupportedState;
        included = true;
        const u32 span = u32(source[0]) * 480 * denominator[source[8]] / numerator[source[8]];
        if (mode == 1 ? span < ticks : span > ticks) ticks = span;
    }
    // Native minimum-mode all-muted result is UINT_MAX: reject that malformed
    // duration instead of importing an effectively unbounded timeline.
    if (!included && mode == 1) return SongImportResult::UnsupportedState;
    if (!ticks) ticks = 16 * 480;
    if (mode == 2) {
        // 5aadd4: floor to a time-signature unit, minimum one unit.
        static const u8 steps[6] = {12,16,20,12,14,12};
        const u32 meter = project[31] & 15;
        const u32 unit = u32(steps[meter < 6 ? meter : 5]) * 480;
        ticks = (ticks / unit > 1 ? ticks / unit : 1) * unit;
    }
    return SongImportResult::Ok;
}

// GUI only, stopped, with stable project identity. Caller owns separate scratch
// (not a large callback stack temporary). No native project byte is ever written.
// 597e10 reads 96 scene IDs at project+5401bc+song*68, count at +60.
// 58586c permits songs 0..13; scene rows occupy 98..d7c, stride 33.
// Consecutive equal scenes do not call native 597ad0/reset: merge each run so
// unequal track periods continue uninterrupted. Different scenes restart phase.
// Eight musical lanes only: this is a source/placement import, not AUX automation.
inline SongImportResult arranger_import_song(arranger::Model& destination, const u8* project,
                                             u32 song, u32 start, arranger::Model& scratch) {
    if (!project || song >= 14 || &destination == &scratch) return SongImportResult::Invalid;
    const u8* order = project + 0x5401bc + song * 0x68;
    const u32 count = arranger_song_word(order + 0x60);
    if (!count) return SongImportResult::Empty;
    if (count > 96) return SongImportResult::Invalid;
    if (project[6] >= 100) return SongImportResult::Invalid;
    const u8* current = project + 0x98 + u32(project[6]) * 33;
    // Stock note admission still uses the current native scene's mute getter.
    // Do not silently import an audible lane into a globally muted track.
    for (u32 track = 0; track < 8; ++track)
        if (current[16 + track] != 0) return SongImportResult::CurrentSceneMuted;
    scratch = destination;
    u32 cursor = start, inserted = 0;
    for (u32 occurrence = 0; occurrence < count;) {
        const u32 scene_id = order[occurrence];
        if (scene_id >= 100) return SongImportResult::Invalid;
        const u8* scene = project + 0x98 + scene_id * 33;
        // Empty scene selection can inherit previous native scene content. Do
        // not emulate that mutation or invent source assignments during import.
        if (!scene[32]) return SongImportResult::UnsupportedState;
        u32 span = 0;
        const SongImportResult status = arranger_song_scene_ticks(project, scene, span);
        if (status != SongImportResult::Ok) return status;
        u32 repeats = 1;
        while (occurrence + repeats < count && order[occurrence + repeats] == scene_id) ++repeats;
        if (!span || repeats > (arranger::Model::max_time - cursor) / span) return SongImportResult::NoRoom;
        span *= repeats;
        for (u32 track = 0; track < 8; ++track) {
            if (scene[16 + track] == 2) continue;
            const u8* source = arranger_song_source(project, track, scene[track]);
            const arranger::Clip clip(static_cast<u8>(track), scene[track], cursor, span, 0, u32(source[0]) * 480);
            if (scratch.insert(clip) < 0) return SongImportResult::NoRoom;
            ++inserted;
        }
        cursor += span;
        occurrence += repeats;
    }
    if (!inserted) return SongImportResult::Empty;
    destination = scratch;
    return SongImportResult::Ok;
}
}}
