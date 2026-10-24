#pragma once
#include <opxy/recording_policy.hpp>

namespace opxy { namespace extensions { namespace replace {
class Policy {
    struct Tag { u32 generation, lap; bool recorded; };
    Tag tags_[recording_capacity];
public:
    constexpr Policy() : tags_{} {}
    void reset() {
        for (u32 i = 0; i < recording_capacity; ++i) tags_[i] = {0, 0, false};
    }
    void recorded(const RecordedSlot& note) {
        if (note.slot < recording_capacity)
            tags_[note.slot] = {note.generation, note.intended_lap, true};
    }
    void select(const RecordingFrame& frame, RecordingSelection& out) const {
        for (u32 i = 0; i < recording_capacity; ++i) {
            const RecordingSlot& slot = frame.slots[i];
            const Tag& tag = tags_[i];
            const bool current = tag.recorded && tag.generation == slot.generation &&
                static_cast<i32>(frame.lap - tag.lap) <= 0;
            out.slots[i] = frame.length_raw && frame.from_raw < frame.to_raw &&
                frame.to_raw <= frame.length_raw && slot.used && !slot.active && !current &&
                slot.effective_raw >= frame.from_raw && slot.effective_raw < frame.to_raw;
        }
    }
};
}}}
