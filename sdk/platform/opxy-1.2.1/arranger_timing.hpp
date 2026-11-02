#pragma once
#include "arranger_phase.hpp"

namespace opxy { namespace platform {
// One native step is 480 source ticks. Every stock rate divides this numerator.
static inline u32 arranger_step_ticks(u32 scale) {
    const u32 rate = arranger_source_rate(scale);
    return rate ? (480u * 1680u) / rate : 0;
}

static inline u32 arranger_clip_limit(const arranger::Model& model, u32 slot, u32 scale, u32 source_steps = 64) {
    const arranger::Clip* value = model.clip(slot);
    const u32 step = arranger_step_ticks(scale);
    if (!value || !step || !source_steps || source_steps > 64) return 0;
    u32 limit = source_steps * step;
    if (limit > arranger::Model::max_time - value->start)
        limit = arranger::Model::max_time - value->start;
    for (u32 i = 0; i < arranger::Model::capacity; ++i) {
        const arranger::Clip* other = model.clip(i);
        if (i != slot && other && other->track == value->track && other->start > value->start &&
            other->start - value->start < limit) limit = other->start - value->start;
    }
    return limit;
}

// Saturating proportional conversion, without native 64-bit division helpers.
// Numerator/denominator are authenticated stock rates, at most 5040.
static inline u32 arranger_rescaled_ticks(u32 length, u32 old_scale, u32 new_scale, u32 cap) {
    const u32 numerator = arranger_source_rate(old_scale), denominator = arranger_source_rate(new_scale);
    if (!numerator || !denominator || !cap) return 0;
    const u32 whole = length / denominator, remainder = length % denominator;
    if (whole > cap / numerator) return cap;
    const u32 base = whole * numerator, tail = (remainder * numerator) / denominator;
    return tail > cap - base ? cap : base + tail;
}

static inline bool arranger_rescale_clip(arranger::Model& model, u32 slot, u32 old_scale, u32 new_scale, u32 source_steps = 64) {
    const arranger::Clip* value = model.clip(slot);
    if (!value || !arranger_source_rate(old_scale) || !arranger_source_rate(new_scale)) return false;
    const u32 cap = arranger_clip_limit(model, slot, new_scale, source_steps);
    if (!cap) return false;
    u32 length = arranger_rescaled_ticks(value->length, old_scale, new_scale, cap);
    // A neighbor can leave less than one step: keep its boundary, never move it.
    const u32 minimum = arranger_step_ticks(new_scale) < cap ? arranger_step_ticks(new_scale) : cap;
    if (length < minimum) length = minimum;
    return model.resize(slot, length);
}
}}
