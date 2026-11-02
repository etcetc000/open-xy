#pragma once
#include <opxy/types.hpp>

// Allocation-free model only: no firmware hooks, project storage or audio dispatch.
// Time units are opaque integer ticks; mapping stock sequencer time is adapter work.
namespace opxy { namespace arranger {

struct Clip {
    u8 track;
    u8 pattern;
    bool muted; // Per-instance silence; source, selection and geometry stay intact.
    u32 start;
    u32 length;
    u32 offset;
    u32 source_length;
    // Exact native source phase below one tick, in units of 1/1680 tick.
    u32 phase_remainder;
    constexpr Clip(u8 lane = 0, u8 source = 0, u32 at = 0, u32 span = 0,
                   u32 phase = 0, u32 period = 0, u32 remainder = 0, bool silent = false)
        : track(lane), pattern(source), muted(silent), start(at), length(span), offset(phase),
          source_length(period), phase_remainder(remainder) {}
};

class Model {
public:
    static const u32 capacity = 256;
    static const u32 max_time = 0xffffffffu;

    constexpr Model() : clips_{}, used_{}, position_(0), loop_start_(0), loop_end_(1), loop_enabled_(false) {}

    // Slot identity stays fixed until removal; a later insert may reuse a free slot.
    const Clip* clip(u32 slot) const { return slot < capacity && used_[slot] ? &clips_[slot] : nullptr; }

    bool insertAt(u32 slot, const Clip& value) {
        if (slot >= capacity || used_[slot] || !valid(value, capacity)) return false;
        clips_[slot] = value;
        used_[slot] = true;
        return true;
    }

    i32 insert(const Clip& value) {
        if (!valid(value, capacity)) return -1;
        for (u32 slot = 0; slot < capacity; ++slot) {
            if (!used_[slot]) {
                clips_[slot] = value;
                used_[slot] = true;
                return static_cast<i32>(slot);
            }
        }
        return -1;
    }

    bool replace(u32 slot, const Clip& value) {
        if (!clip(slot) || !valid(value, slot)) return false;
        clips_[slot] = value;
        return true;
    }

    // Split references the same source and preserves its phase on both sides.
    i32 split(u32 slot, u32 cursor) {
        const Clip* original = clip(slot);
        if (!original || cursor <= original->start || cursor - original->start >= original->length) return -1;
        u32 free = 0;
        while (free < capacity && used_[free]) ++free;
        if (free == capacity) return -1;
        Clip right = *original;
        const u32 left_length = cursor - right.start;
        right.start = cursor;
        right.length -= left_length;
        right.offset = phaseForward(right.offset, left_length, right.source_length);
        if (!valid(right, slot)) return -1;
        clips_[slot].length = left_length;
        clips_[free] = right;
        used_[free] = true;
        return static_cast<i32>(free);
    }

    // Keep the right edge fixed, including when revealing earlier source material.
    bool trimLeft(u32 slot, u32 start) {
        if (!clip(slot)) return false;
        Clip value = clips_[slot];
        const u32 end = value.start + value.length;
        if (start >= end) return false;
        value.offset = start >= value.start ? phaseForward(value.offset, start - value.start, value.source_length) :
            phaseBackward(value.offset, value.start - start, value.source_length);
        value.start = start;
        value.length = end - start;
        return replace(slot, value);
    }

    bool slip(u32 slot, i32 ticks) {
        if (!clip(slot)) return false;
        Clip& value = clips_[slot];
        value.offset = ticks >= 0 ? phaseForward(value.offset, static_cast<u32>(ticks), value.source_length) :
            phaseBackward(value.offset, 0u - static_cast<u32>(ticks), value.source_length);
        return true;
    }

    bool move(u32 slot, u8 track, u32 start) {
        if (!clip(slot)) return false;
        Clip value = clips_[slot];
        value.track = track;
        value.start = start;
        if (!valid(value, slot)) return false;
        clips_[slot] = value;
        return true;
    }

    bool resize(u32 slot, u32 length) {
        if (!clip(slot)) return false;
        Clip value = clips_[slot];
        value.length = length;
        if (!valid(value, slot)) return false;
        clips_[slot] = value;
        return true;
    }

    bool setMuted(u32 slot, bool value) {
        if (!clip(slot)) return false;
        clips_[slot].muted = value;
        return true;
    }

    bool remove(u32 slot) {
        if (!clip(slot)) return false;
        used_[slot] = false;
        return true;
    }

    // Copies a reference to the same pattern; source cloning belongs to the adapter.
    i32 duplicate(u32 slot, u32 start) {
        if (!clip(slot)) return -1;
        Clip value = clips_[slot];
        value.start = start;
        return insert(value);
    }

    const Clip* active(u8 track, u32 time, u32& source_position) const {
        source_position = 0;
        for (u32 slot = 0; slot < capacity; ++slot) {
            const Clip* value = clip(slot);
            if (!value || value->track != track || time < value->start || time - value->start >= value->length)
                continue;
            const u32 phase = (time - value->start) % value->source_length;
            const u32 remaining = value->source_length - value->offset;
            source_position = phase >= remaining ? phase - remaining : phase + value->offset;
            return value;
        }
        return nullptr;
    }

    void seek(u32 time) { position_ = time; }
    u32 position() const { return position_; }
    bool loopEnabled() const { return loop_enabled_; }
    u32 loopStart() const { return loop_start_; }
    u32 loopEnd() const { return loop_end_; }

    bool setLoop(bool enabled, u32 start, u32 end) {
        if (start >= end) return false;
        loop_start_ = start;
        loop_end_ = end;
        loop_enabled_ = enabled;
        return true;
    }

    // Half-open [A,B); retain overshoot without overflowing or using wide division.
    u32 advance(u32 delta) {
        if (!loop_enabled_) {
            position_ = delta > max_time - position_ ? max_time : position_ + delta;
        } else if (position_ < loop_end_) {
            const u32 distance = loop_end_ - position_;
            position_ = delta < distance ? position_ + delta :
                loop_start_ + (delta - distance) % (loop_end_ - loop_start_);
        } else {
            const u32 span = loop_end_ - loop_start_;
            const u32 phase = (position_ - loop_end_) % span;
            const u32 step = delta % span;
            position_ = loop_start_ + (step >= span - phase ? step - (span - phase) : phase + step);
        }
        return position_;
    }

private:
    static u32 phaseForward(u32 offset, u32 ticks, u32 period) {
        const u32 delta = ticks % period;
        return delta >= period - offset ? delta - (period - offset) : offset + delta;
    }
    static u32 phaseBackward(u32 offset, u32 ticks, u32 period) {
        const u32 delta = ticks % period;
        return delta > offset ? period - (delta - offset) : offset - delta;
    }

    bool valid(const Clip& value, u32 ignore) const {
        if (value.track >= 8 || value.pattern >= 16 || !value.length || value.length > max_time - value.start ||
            !value.source_length || value.offset >= value.source_length || value.phase_remainder >= 1680) return false;
        for (u32 slot = 0; slot < capacity; ++slot) {
            const Clip* other = clip(slot);
            if (slot != ignore && other && other->track == value.track &&
                value.start < other->start + other->length && other->start < value.start + value.length) return false;
        }
        return true;
    }

    Clip clips_[capacity];
    bool used_[capacity];
    u32 position_, loop_start_, loop_end_;
    bool loop_enabled_;
};

enum class Key : u8 { Sequencer = 1, Sample = 2 };

class HoldChord {
public:
    HoldChord() : held_(0), started_(0), armed_(false), fired_(false) {}
    void reset() { held_ = 0; started_ = 0; armed_ = fired_ = false; }

    // Semantic keys only. Caller supplies monotonic milliseconds, modulo u32.
    void set(Key key, bool pressed, u32 now) {
        const u8 bit = static_cast<u8>(key);
        if (bit != 1 && bit != 2) return;
        const u8 previous = held_;
        held_ = pressed ? held_ | bit : held_ & ~bit;
        if (!held_) fired_ = false;
        if (held_ != 3) armed_ = false;
        else if (previous != 3 && !fired_) { started_ = now; armed_ = true; }
    }

    bool poll(u32 now) {
        if (!armed_ || fired_ || now - started_ < 600) return false;
        fired_ = true;
        armed_ = false;
        return true;
    }

private:
    u8 held_;
    u32 started_;
    bool armed_, fired_;
};

}}
