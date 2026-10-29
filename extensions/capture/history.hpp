#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace extensions { namespace capture {

// Internal ghost pattern, independent of firmware addresses and input routing.
// The adapter supplies stock recording positions and stock-scaled tick deltas.
// No quantization or conversion from wall-clock time belongs in this buffer.
class RecordingHistory {
public:
    // Stock's ordinary note container and active recording list limits.
    static const u32 capacity = 120;
    static const u32 held_capacity = 20;
    constexpr RecordingHistory() : context_{}, entries_{}, count_(0), held_(0),
                                   playing_(false) {}

    // length is in stock pattern ticks, not bars or elapsed milliseconds.
    // Stop freezes the partial pass; a subsequent start begins a new take.
    void sync(const RecordingContext& context, bool playing) {
        const bool valid = context.recordable && context.track < 8 &&
            context.length > 0 && context.length < (1u << 23);
        const bool running = valid && playing;
        if (!(context == context_) || (running && !playing_)) clear();
        if (!valid) clear();
        if (!running && playing_) {
            for (u32 i = 0; i < count_; ++i) entries_[i].held = false;
            held_ = 0;
        }
        context_ = context;
        playing_ = running;
    }

    // Same Q8-scaled delta that stock accumulates for held recorded notes.
    // Only held-note duration advances. Completed notes stay in the ghost
    // pattern across loop wraps and silence, just as with armed recording.
    void advance(u32 delta) {
        if (!playing_ || !delta) return;
        for (u32 i = 0; i < count_; ++i) {
            Entry& entry = entries_[i];
            if (entry.held) {
                const u32 maximum = context_.length << 8;
                entry.duration += delta < maximum - entry.duration ? delta : maximum - entry.duration;
                entry.note.length = entry.duration >> 8;
            }
        }
    }

    // Receives normalized live input only. The platform must exclude playback,
    // step entry and generated notes. A repeated key ends its previous hold.
    bool note_on(i32 position, u32 pitch, u32 velocity, u32 radius = 0) {
        if (!playing_ || pitch > 127 || velocity > 127) return false;
        note_off(pitch);
        // Stock rejects a full container before attempting collision pruning.
        if (!velocity || held_ == held_capacity || count_ == capacity) return false;
        u32 kept = 0;
        for (u32 i = 0; i < count_; ++i) {
            const Entry& old = entries_[i];
            const u32 distance = position >= old.note.position
                ? static_cast<u32>(position) - static_cast<u32>(old.note.position)
                : static_cast<u32>(old.note.position) - static_cast<u32>(position);
            if (old.note.pitch == pitch && distance < radius) {
                if (old.held) --held_;
            } else {
                entries_[kept++] = old;
            }
        }
        count_ = kept;
        Entry& entry = entries_[count_++];
        entry.note = {position, 0, static_cast<u8>(pitch), static_cast<u8>(velocity)};
        entry.duration = 0;
        entry.held = true;
        ++held_;
        return true;
    }

    void note_off(u32 pitch) {
        if (!playing_) return;
        for (u32 i = 0; i < count_; ++i) {
            Entry& entry = entries_[i];
            if (entry.held && entry.note.pitch == pitch) {
                entry.held = false;
                --held_;
            }
        }
    }

    u32 size() const { return count_; }

    // Keep input order for stock's collision/capacity policy, even when a
    // loop wrap makes recorded positions descend. A snapshot never consumes
    // history. The caller clears it only after a successful pattern commit.
    u32 snapshot(Note* notes, u32 available) const {
        if (!notes || available < count_) return 0;
        for (u32 i = 0; i < count_; ++i) notes[i] = entries_[i].note;
        return count_;
    }

    void clear() { count_ = held_ = 0; }

private:
    struct Entry {
        Note note;
        u32 duration;
        bool held;
    };
    RecordingContext context_;
    Entry entries_[capacity];
    u32 count_, held_;
    bool playing_;
};

}}}
