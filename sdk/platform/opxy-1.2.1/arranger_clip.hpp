#pragma once
#include <opxy/arranger.hpp>
#include "arranger_resolver.hpp"
#include "arranger_phase.hpp"
#include "arranger_timing.hpp"
#include "barrier.hpp"

namespace opxy { namespace platform {

// GUI only; caller guards project lifetime and publishes/marks dirty afterwards.
// Append without renumbering any existing scene or arranger source references.
inline i32 arranger_add_empty_clip(arranger::Model& model, u8* project, u32 track,
                                  u32 cursor, u32 source_pattern, void (*initialize)(u8*)) {
    if (!arranger_pattern_source(project, track, source_pattern, false)) return -1;
    const u8* source = arranger_pattern_source(project, track, source_pattern, true);
    if (!source || !initialize) return -1;
    u8* pool = project + 0xd7c + track * 0x53f44;
    u32* count = reinterpret_cast<u32*>(pool + 0x53f40);
    if (*count >= 16) return -1;
    const arranger::Clip value = {static_cast<u8>(track), static_cast<u8>(*count), cursor,
                                  16 * 480, 0, 64 * 480};
    const i32 slot = model.insert(value);
    if (slot < 0) return -1;
    u8* destination = pool + *count * 0x53f4;
    // Stock copies this inline pattern representation; reset only its sequencing data.
    for (u32 i = 0; i < 0x53f4; ++i) destination[i] = source[i];
    initialize(destination);
    destination[0] = 64;
    destination[8] = 3; // New clips start with normal native timing.
    barrier();
    ++*count;
    barrier();
    return slot;
}

// Add an instance of an existing source without changing notes or native timing.
inline i32 arranger_add_source_clip(arranger::Model& model, const u8* project, u32 track,
                                   u32 cursor, u32 pattern) {
    const u8* source = arranger_pattern_source(project, track, pattern, false);
    if (!source || !source[0] || source[0] > 64 || source[8] >= 16) return -1;
    const u32 steps = source[0] < 16 ? source[0] : 16;
    u32 length = steps * arranger_step_ticks(source[8]);
    if (length > arranger::Model::max_time - cursor) length = arranger::Model::max_time - cursor;
    for (u32 slot = 0; slot < arranger::Model::capacity; ++slot) {
        const arranger::Clip* other = model.clip(slot);
        if (other && other->track == track && other->start > cursor && other->start - cursor < length)
            length = other->start - cursor;
    }
    return model.insert({static_cast<u8>(track), static_cast<u8>(pattern), cursor, length, 0, u32(source[0]) * 480});
}

inline bool arranger_clip_track_locked(const u8* project, u32 source, u32 destination) {
    if (!project || source >= 8 || destination >= 8) return true;
    const u32 locks = project[12] | (u32(project[13]) << 8);
    return (locks & ((1u << source) | (1u << destination))) != 0;
}

// GUI only: caller holds transport/project edit guards and publishes after success.
// Pattern notes, lists and sample descriptors are inline. Stock pattern destructor
// 8d4dda1c only resets list counts; stock copies the full 0x53f4 representation.
// Reject instrument locks: copying a different locked instrument over sequencing
// requires separately authenticated field boundaries, not a guessed partial copy.
inline bool arranger_clone_clip_source(arranger::Model& model, u8* project, u32 slot, u32 track) {
    const arranger::Clip* selected = model.clip(slot);
    if (!project || !selected || track >= 8) return false;
    if (arranger_clip_track_locked(project, selected->track, track)) return false;
    const u8* source = arranger_pattern_source(project, selected->track, selected->pattern, false);
    if (!source || source != arranger_pattern_source(project, selected->track, selected->pattern, true)) return false;
    u8* pool = project + 0xd7c + track * 0x53f44;
    u32* count = reinterpret_cast<u32*>(pool + 0x53f40);
    if (*count >= 16) return false;
    arranger::Clip replacement = *selected;
    replacement.track = static_cast<u8>(track);
    replacement.pattern = static_cast<u8>(*count);
    // Validate and reserve the model edit before touching project storage. No
    // fallible work follows; old sources stay allocated for references and undo.
    if (!model.replace(slot, replacement)) return false;
    u8* destination = pool + *count * 0x53f4;
    for (u32 i = 0; i < 0x53f4; ++i) destination[i] = source[i];
    barrier();
    ++*count;
    barrier();
    return true;
}

inline bool arranger_make_unique(arranger::Model& model, u8* project, u32 slot) {
    const arranger::Clip* selected = model.clip(slot);
    return selected && arranger_clone_clip_source(model, project, slot, selected->track);
}

inline bool arranger_move_clip_track(arranger::Model& model, u8* project, u32 slot, u32 track) {
    const arranger::Clip* selected = model.clip(slot);
    if (!selected || !project || track >= 8) return false;
    if (selected->track == track) return true;
    return arranger_clone_clip_source(model, project, slot, track);
}

// Clipboard remains a reference to its original source. Same-lane pastes share
// it; cross-lane pastes append an independent source in the destination pool.
inline i32 arranger_paste_clip(arranger::Model& model, u8* project,
                              const arranger::Clip& clipboard, u32 track, u32 cursor) {
    const u8* source = arranger_pattern_source(project, clipboard.track, clipboard.pattern, false);
    if (!source || track >= 8) return -1;
    arranger::Clip value = clipboard;
    value.start = cursor;
    if (track == clipboard.track) return model.insert(value);
    if (arranger_clip_track_locked(project, clipboard.track, track) ||
        source != arranger_pattern_source(project, clipboard.track, clipboard.pattern, true)) return -1;
    u8* pool = project + 0xd7c + track * 0x53f44;
    u32* count = reinterpret_cast<u32*>(pool + 0x53f40);
    if (*count >= 16) return -1;
    value.track = static_cast<u8>(track);
    value.pattern = static_cast<u8>(*count);
    const i32 slot = model.insert(value);
    if (slot < 0) return -1;
    // Insert validates space, time and slot capacity before any native write.
    // Nothing after it can fail, so no partial project allocation needs rollback.
    u8* destination = pool + *count * 0x53f4;
    for (u32 i = 0; i < 0x53f4; ++i) destination[i] = source[i];
    barrier();
    ++*count;
    barrier();
    return slot;
}

// Native scale-aware edge edits use the live source period, which may have
// changed since the clip was first created. No fractional phase is discarded.
inline i32 arranger_split_clip(arranger::Model& model, const u8* project, u32 slot, u32 cursor) {
    const arranger::Clip* selected = model.clip(slot);
    if (!selected) return -1;
    const u8* source = arranger_pattern_source(project, selected->track, selected->pattern, false);
    if (!source || !source[0] || source[8] >= 16) return -1;
    const u32 phase = arranger_source_phase(*selected, source, cursor);
    const i32 right = model.split(slot, cursor);
    if (right < 0) return -1;
    arranger::Clip value = *model.clip(static_cast<u32>(right));
    value.offset = phase / 1680;
    value.phase_remainder = phase % 1680;
    value.source_length = u32(source[0]) * 480;
    model.replace(static_cast<u32>(right), value); // Same geometry; normalized phase is valid.
    return right;
}

inline bool arranger_trim_clip_left(arranger::Model& model, const u8* project, u32 slot, u32 start) {
    const arranger::Clip* selected = model.clip(slot);
    if (!selected) return false;
    const u8* source = arranger_pattern_source(project, selected->track, selected->pattern, false);
    if (!source || !source[0] || source[8] >= 16 || start >= selected->start + selected->length) return false;
    arranger::Clip value = *selected;
    const u32 phase = arranger_source_phase(value, source, start);
    value.length = value.start + value.length - start;
    value.start = start;
    value.offset = phase / 1680;
    value.phase_remainder = phase % 1680;
    value.source_length = u32(source[0]) * 480;
    return model.replace(slot, value);
}

}}
