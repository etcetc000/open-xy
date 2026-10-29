#pragma once
#include <opxy/types.hpp>
extern "C" {
bool sdk_arranger_bounce_destination_ready(opxy::u32, opxy::u32, bool) __asm__("sdk_arranger_bounce_destination_ready");
bool sdk_arranger_bounce_destination_begin(opxy::u32, opxy::u32, bool, const char*, opxy::u32) __asm__("sdk_arranger_bounce_destination_begin");
opxy::u32 sdk_arranger_bounce_destination_status() __asm__("sdk_arranger_bounce_destination_status");
bool sdk_arranger_bounce_destination_busy() __asm__("sdk_arranger_bounce_destination_busy");
void sdk_arranger_bounce_destination_cancel() __asm__("sdk_arranger_bounce_destination_cancel");
void sdk_arranger_bounce_sort_regions(opxy::u32) __asm__("sdk_arranger_bounce_sort_regions");
void sdk_arranger_bounce_destination_apply(void*) __asm__("sdk_arranger_bounce_destination_apply");
const opxy::u8* sdk_arranger_bounce_destination_source(opxy::u32) __asm__("sdk_arranger_bounce_destination_source");
// Returns -1 when the pointer does not belong to this bounded GUI transaction.
opxy::i32 sdk_arranger_bounce_destination_index(const opxy::u8*) __asm__("sdk_arranger_bounce_destination_index");
}

#include <opxy/arranger.hpp>
namespace opxy { namespace platform {
template <class Commit, class SameProject>
inline bool arranger_bounce_publish(arranger::Model& current, const arranger::Model& next,
                                    arranger::Model& backup, Commit commit, SameProject same_project) {
    backup = current; current = next;
    if (commit()) return true;
    if (same_project()) current = backup;
    return false;
}
// Candidate only: failure never mutates the project or the caller's arrangement.
inline bool arranger_bounce_layout(const arranger::Model& model, u32 slot, u32 pattern, bool replace,
                                   arranger::Model& out, arranger::Clip& clip, u32& steps, u32& scale) {
    const arranger::Clip* source = model.clip(slot);
    if (!source || source->track >= 7 || pattern >= 16 || !source->length || source->length > 244800) return false;
    const u32 lane = source->track + 1;
    out = model;
    for (u32 i = 0; i < arranger::Model::capacity; ++i) {
        const arranger::Clip* other = out.clip(i);
        if (!other || other->track != lane || other->start >= source->start + source->length ||
            source->start >= other->start + other->length) continue;
        if (!replace) return false;
        out.remove(i);
    }
    const u32 multiplier = source->length <= 122400 ? 1 : 2;
    steps = (source->length + 480 * multiplier - 1) / (480 * multiplier);
    scale = multiplier == 1 ? 3 : 5;
    clip = arranger::Clip{static_cast<u8>(lane), static_cast<u8>(pattern), source->start,
        source->length, 0, steps * 480 * multiplier};
    for (u32 i = 0; i < arranger::Model::capacity; ++i)
        if (!out.clip(i)) return out.insertAt(i, clip);
    return false;
}
}}
