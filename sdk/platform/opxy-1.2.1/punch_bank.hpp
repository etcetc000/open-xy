// The punch-in track's second bank (shift + key): which presses it takes, the
// keys it holds, and how held keys run on the instrument tracks. No stock
// calls, so the host tests run it as is.
#pragma once
#include <opxy/punch.hpp>

namespace opxy { namespace punch_bank {
const u32 kKeys = 24, kTracks = 8;
// Stock's punch-in notes: the drum octave's keys from F at 101..112, the
// melodic octave's at 113..124. In the second bank they are keys 0..11 (low
// octave) and 12..23 (high octave), each with its own effect.
const u8 kFirstPitch = 101, kLastPitch = 124;

inline u32 effect_index(const punch::Effect* const* effects, u32 count, u32 key) {
    u32 i = 0;
    while (i < count && effects[i]->key != key) ++i;
    return i;
}

// The key (0..23) a press on the punch-in track plays in the second bank, or -1 to leave it to stock: page 2 is on (`shift`) and an effect plays that key.
// `pitch` is the press as stock's remap maps it onto the punch-in octaves.
inline i32 take(bool shift, u8 pitch, const punch::Effect* const* effects, u32 count) {
    if (!shift || pitch < kFirstPitch || pitch > kLastPitch) return -1;
    const u32 key = pitch - kFirstPitch;
    return effect_index(effects, count, key) < count ? static_cast<i32>(key) : -1;
}

// The second-bank keys held, each latched at its press until the release of
// the same keyboard pitch, whatever shift does in between. An effect with a
// release quantum stays latched after that release until the sequencer
// position reaches its next multiple. Oldest first.
struct Bank {
    static const u32 kLatches = 24;   // every keyboard key
    struct Latch { u8 pitch, key, releasing; u32 since, until; };
    Latch latches[kLatches];
    u32 count;

    bool press(u8 pitch, u8 key) {
        drop(pitch);
        if (count == kLatches) return false;
        latches[count++] = Latch{pitch, key, 0, 0, 0};
        return true;
    }
    // True when the pitch was latched: its release belongs to the bank. A key
    // whose effect has a release quantum runs on until the next multiple of it
    // after `now` (ticks96).
    bool release(u8 pitch, u32 now, const punch::Effect* const* effects, u32 count_effects) {
        for (u32 i = 0; i < count; ++i) {
            Latch& latch = latches[i];
            if (latch.pitch != pitch || latch.releasing) continue;
            const u32 index = effect_index(effects, count_effects, latch.key);
            const u32 quantum = index < count_effects ? effects[index]->release : 0;
            if (!quantum) return remove(i), true;
            latch.releasing = 1, latch.since = now, latch.until = (now / quantum + 1) * quantum;
            return true;
        }
        return false;
    }
    // Releases every key still held, as if each were let go at `now`.
    void release_all(u32 now, const punch::Effect* const* effects, u32 count_effects) {
        for (u32 i = 0; i < count;) {
            if (latches[i].releasing) { ++i; continue; }
            const u32 before = count;
            release(latches[i].pitch, now, effects, count_effects);
            if (count == before) ++i;
        }
    }
    // Ends the released keys whose time has come, or whose position went back
    // (the transport restarted or jumped).
    void expire(u32 now) {
        for (u32 i = 0; i < count;) {
            const Latch& latch = latches[i];
            if (latch.releasing && (now >= latch.until || now < latch.since)) remove(i);
            else ++i;
        }
    }
    u32 keys() const {
        u32 held = 0;
        for (u32 i = 0; i < count; ++i) held |= 1u << latches[i].key;
        return held;
    }
    i32 latest() const { return count ? latches[count - 1].key : -1; }
    // Whether the pitch is latched and not yet released.
    bool holds(u8 pitch) const {
        for (u32 i = 0; i < count; ++i)
            if (latches[i].pitch == pitch && !latches[i].releasing) return true;
        return false;
    }
    // Keys still pressed (not running on after their release).
    u32 pressed() const {
        u32 n = 0;
        for (u32 i = 0; i < count; ++i) n += latches[i].releasing ? 0 : 1;
        return n;
    }

  private:
    void remove(u32 i) {
        for (--count; i < count; ++i) latches[i] = latches[i + 1];
    }
    // A new press of a pitch replaces any latch of it, released or not.
    void drop(u8 pitch) {
        for (u32 i = 0; i < count;) {
            if (latches[i].pitch == pitch) remove(i);
            else ++i;
        }
    }
};

// Runs `call(effect, context)` for every held key's effect on `track`, lowest
// key first. `engaged` holds the keys that ran on this track last time, for
// context.first. With `led`, a bit per effect index, the first track an effect
// runs on since `led` was cleared gets context.lead. Returns whether any call
// returned true.
template <class Call>
bool run(u32 held, const punch::Effect* const* effects, u32 count, u32& engaged, u32 track, bool drum,
         u32 ticks96, i32 bend, u32* led, Call call, i32 knob = 0, i32 turn = 0) {
    const u32 before = engaged;
    engaged = held;
    bool any = false;
    for (u32 key = 0; key < kKeys; ++key) {
        const u32 bit = 1u << key;
        if (!(held & bit)) continue;
        const u32 index = effect_index(effects, count, key);
        if (index >= count) continue;
        const u32 mine = index < 32 ? 1u << index : 0;
        const bool lead = led && mine && !(*led & mine);
        if (lead) *led |= mine;
        const punch::Context context{track, drum, !(before & bit), ticks96, bend, lead, knob, turn};
        any |= call(*effects[index], context);
    }
    return any;
}
}}
