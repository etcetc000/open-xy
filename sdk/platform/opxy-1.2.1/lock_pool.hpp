// SDK step-lock values kept by handle (lock_handles.cpp, docs/abi-1.2.1.md "Step-lock
// handles"). A step that holds SDK locks carries a 16-bit handle in one of stock's own
// lock slots; the values live here, in a sparse pool: up to 16 fields of 16 bits per
// handle, for every key the step plays (a step-wide entry) and, optionally, for up to 24
// keys (per-key entries, which win field by field).
//
// The pool is an arena of 16-bit words holding one block per handle:
//   handle, entry count, then each entry: field mask, key, one value per mask bit
//   (lowest field first).
// Entries are sorted by key, the step-wide key (kStepWide) last. A block that grows is
// rebuilt at the arena's end and the old one marked dead (its handle word 0); compaction
// slides the live blocks down. The saved form (the companion's open-xy.locks) is the
// live blocks in handle order, as little-endian words.
//
// Nothing here touches stock memory: the pool is plain data, tested on the host.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace step_locks {
const u32 kArenaWords = 12288;       // 24 KiB of blocks a pool
// The smallest block (handle, entry count, mask, key, one value) is 5 words, so the
// arena holds at most 2,457 blocks: handles 1..2457 (0 is "none") are enough.
const u32 kSmallestBlock = 5;
const u32 kHandles = kArenaWords / kSmallestBlock + 1;
const u32 kFields = 16;              // fields a handle may lock, 0..15
const u32 kStepWide = 0xffff;        // an entry's key for every key the step plays
const u32 kKeys = 128;               // a per-key entry's key: the played note, 0..127
const u32 kEntries = 25;             // entries a handle holds at most: step-wide and 24 keys
const u32 kBlockWords = 2 + kEntries * (2 + kFields);
// What a pool's saved form takes at most, before base64.
const u32 kSavedBytes = kArenaWords * 2;

inline u32 popcount16(u32 mask) {
    u32 n = 0;
    for (mask &= 0xffff; mask; mask &= mask - 1) ++n;
    return n;
}

class Pool {
public:
    void clear() {
        for (u32 h = 0; h < kHandles; ++h) head_[h] = 0;
        used_ = 0;
        held_ = 0;
        reading_ = false;
    }

    bool has(u32 h) const { return h && h < kHandles && head_[h]; }
    u32 handles() const { return held_; }
    // Words of the arena the live blocks take.
    u32 live_words() const {
        u32 n = 0;
        for (u32 h = 1; h < kHandles; ++h)
            if (head_[h]) n += length(head_[h] - 1);
        return n;
    }

    // The value of `field` for `key`: the key's own entry, else the step-wide one.
    bool get(u32 h, u32 key, u32 field, i32& value) const {
        if (!has(h) || field >= kFields) return false;
        const u16* own = entry(h, key);
        const u16* wide = key == kStepWide ? nullptr : entry(h, kStepWide);
        return pick(own, field, value) || pick(wide, field, value);
    }
    // Every field locked for `key`, as get() finds each: its mask, values by field.
    u32 fields(u32 h, u32 key, i32* values) const {
        u32 mask = 0;
        for (u32 f = 0; f < kFields; ++f)
            if (get(h, key, f, values[f])) mask |= 1u << f;
        return mask;
    }
    // Only the key's own entry (or the step-wide one for kStepWide).
    bool own(u32 h, u32 key, u32 field, i32& value) const {
        return has(h) && field < kFields && pick(entry(h, key), field, value);
    }

    // Locks `field` at `value` for `key`, allocating the handle's block when it has
    // none. False when the arena or the handle is full: nothing changes.
    bool set(u32 h, u32 key, u32 field, i32 value) {
        if (!h || h >= kHandles || field >= kFields || !(key == kStepWide || key < kKeys)) return false;
        if (u16* e = has(h) ? mutable_entry(h, key) : nullptr) {
            if (e[0] >> field & 1u) {
                e[2 + popcount16(e[0] & ((1u << field) - 1))] = static_cast<u16>(value);
                return true;
            }
        }
        return rebuild(h, key, field, true, value);
    }
    // Unlocks `field` for `key` (its own entry only). An entry left empty goes, and a
    // handle left with no entry is freed.
    void erase(u32 h, u32 key, u32 field) {
        i32 old;
        if (!own(h, key, field, old)) return;
        rebuild(h, key, field, false, 0);
    }
    void free(u32 h) {
        if (!has(h)) return;
        arena_[head_[h] - 1] = 0;
        head_[h] = 0;
        --held_;
    }

    // The lowest handle that this pool does not hold and `referenced` (a bitmap of
    // kHandles bits, or null) does not mark, or 0 when none is left.
    u32 unused(const u8* referenced) const {
        for (u32 h = 1; h < kHandles; ++h)
            if (!head_[h] && !(referenced && (referenced[h >> 3] >> (h & 7) & 1))) return h;
        return 0;
    }
    // Copies `from`'s block for `h` here under handle `to` (which must be free here).
    bool copy(u32 to, const Pool& from, u32 h) {
        if (!from.has(h) || !to || to >= kHandles || has(to)) return false;
        const u32 words = from.length(from.head_[h] - 1);
        if (!room(words)) return false;
        // After room(), which may compact `from` when it is this pool.
        const u16* block = from.arena_ + from.head_[h] - 1;
        const u32 at = used_;
        for (u32 i = 0; i < words; ++i) arena_[at + i] = block[i];
        arena_[at] = static_cast<u16>(to);
        used_ += words;
        head_[to] = static_cast<u16>(at + 1);
        ++held_;
        return true;
    }
    // Garbage collection: frees every handle `marked` (a bitmap) does not mark, then
    // compacts. Returns the handles freed.
    u32 keep_only(const u8* marked) {
        u32 freed = 0;
        for (u32 h = 1; h < kHandles; ++h)
            if (head_[h] && !(marked[h >> 3] >> (h & 7) & 1)) {
                free(h);
                ++freed;
            }
        compact();
        return freed;
    }
    void compact() {
        u32 to = 0;
        for (u32 at = 0; at < used_;) {
            const u32 words = length(at);
            const u32 h = arena_[at];
            if (h && h < kHandles && head_[h] == at + 1) {
                for (u32 i = 0; i < words; ++i) arena_[to + i] = arena_[at + i];
                head_[h] = static_cast<u16>(to + 1);
                to += words;
            }
            at += words;
        }
        used_ = to;
    }

    // The saved form: live blocks in handle order, each word little-endian, stopping
    // before the first block that would take it past `limit` bytes. Returns the bytes.
    template <class Sink> u32 write(Sink& sink, u32 limit) const {
        u32 bytes = 0;
        for (u32 h = 1; h < kHandles; ++h) {
            if (!head_[h]) continue;
            const u32 words = length(head_[h] - 1);
            if (bytes + 2 * words > limit) break;
            const u16* block = arena_ + head_[h] - 1;
            for (u32 i = 0; i < words; ++i) {
                sink.byte(static_cast<u8>(block[i]));
                sink.byte(static_cast<u8>(block[i] >> 8));
            }
            bytes += 2 * words;
        }
        return bytes;
    }
    // The bytes write() gives within `limit`.
    u32 saved_bytes(u32 limit) const {
        u32 bytes = 0;
        for (u32 h = 1; h < kHandles; ++h) {
            if (!head_[h]) continue;
            const u32 words = 2 * length(head_[h] - 1);
            if (bytes + words > limit) break;
            bytes += words;
        }
        return bytes;
    }

    // Reading a saved form: begin, each byte, then finish, which checks every block
    // (handles ascending in 1..kHandles - 1, 1..kEntries entries, keys ascending and valid,
    // masks non-zero) and indexes them. Anything invalid leaves the pool empty.
    void begin() {
        clear();
        reading_ = true;
        odd_ = false;
        over_ = false;
    }
    void byte(u8 b) {
        if (!reading_ || over_) return;
        if (!odd_) {
            if (used_ == kArenaWords) {
                over_ = true;
                return;
            }
            arena_[used_] = b;
            odd_ = true;
        } else {
            arena_[used_++] |= static_cast<u16>(b << 8);
            odd_ = false;
        }
    }
    bool finish() {
        const bool read = reading_ && !odd_ && !over_;
        reading_ = false;
        u32 last = 0;
        bool ok = read;
        for (u32 at = 0; ok && at < used_;) {
            const u32 words = checked_length(at, used_ - at);
            const u32 h = arena_[at];
            ok = words && h > last && h < kHandles;
            if (!ok) break;
            head_[h] = static_cast<u16>(at + 1);
            ++held_;
            last = h;
            at += words;
        }
        if (!ok) clear();
        return ok;
    }

private:
    u16 head_[kHandles];   // the arena offset + 1 of each handle's block, 0 when free
    u16 arena_[kArenaWords];
    u16 scratch_[kBlockWords + 2 + kFields];   // a block, and one entry more while rebuilt
    // No initializers: pools are zero-initialized statics (the stock loader runs no
    // constructors); clear() or begin() resets one.
    u32 used_, held_;
    bool reading_, odd_, over_;

    bool room(u32 words) {
        if (used_ + words <= kArenaWords) return true;
        compact();
        return used_ + words <= kArenaWords;
    }
    // A block's words, from its entries.
    u32 length(u32 at) const {
        u32 words = 2, count = arena_[at + 1];
        while (count--) words += 2 + popcount16(arena_[at + words]);
        return words;
    }
    // The same for a block being read: 0 when it is malformed or runs past `left`.
    u32 checked_length(u32 at, u32 left) const {
        if (left < 2) return 0;
        const u32 count = arena_[at + 1];
        if (!count || count > kEntries) return 0;
        u32 words = 2, last_key = 0;
        bool first = true;
        for (u32 e = 0; e < count; ++e) {
            if (words + 2 > left) return 0;
            const u32 mask = arena_[at + words], key = arena_[at + words + 1];
            if (!mask || !(key == kStepWide || key < kKeys) || (!first && key <= last_key)) return 0;
            first = false;
            last_key = key;
            words += 2 + popcount16(mask);
            if (words > left) return 0;
        }
        return words;
    }
    const u16* entry(u32 h, u32 key) const {
        if (!has(h)) return nullptr;
        const u16* block = arena_ + head_[h] - 1;
        u32 at = 2;
        for (u32 count = block[1]; count--; at += 2 + popcount16(block[at]))
            if (block[at + 1] == key) return block + at;
        return nullptr;
    }
    u16* mutable_entry(u32 h, u32 key) { return const_cast<u16*>(entry(h, key)); }
    static bool pick(const u16* e, u32 field, i32& value) {
        if (!e || !(e[0] >> field & 1u)) return false;
        value = static_cast<i16>(e[2 + popcount16(e[0] & ((1u << field) - 1))]);
        return true;
    }

    // Rebuilds h's block with `field` of `key`'s entry set (or removed), at the arena's end.
    bool rebuild(u32 h, u32 key, u32 field, bool present, i32 value) {
        u32 out = 2, entries = 0;
        bool placed = false;
        const u16* block = has(h) ? arena_ + head_[h] - 1 : nullptr;
        const u32 count = block ? block[1] : 0;
        u32 at = 2;
        auto put_entry = [&](const u16* e, u32 entry_key) {
            u32 mask = e ? e[0] : 0;
            const u32 old_mask = mask;
            if (entry_key == key) {
                if (present) mask |= 1u << field;
                else mask &= ~(1u << field);
            }
            if (!mask) return;
            scratch_[out] = static_cast<u16>(mask);
            scratch_[out + 1] = static_cast<u16>(entry_key);
            u32 v = out + 2, from = 2;
            for (u32 f = 0; f < kFields; ++f) {
                if (!(mask >> f & 1u)) {
                    if (old_mask >> f & 1u) ++from;
                    continue;
                }
                if (entry_key == key && f == field) scratch_[v++] = static_cast<u16>(value);
                else scratch_[v++] = e[from];
                if (old_mask >> f & 1u) ++from;
            }
            out = v;
            ++entries;
        };
        for (u32 n = 0; n < count; ++n) {
            const u16* e = block + at;
            const u32 entry_key = e[1];
            if (!placed && key < entry_key) {
                put_entry(nullptr, key);
                placed = true;
            }
            if (entry_key == key) placed = true;
            put_entry(e, entry_key);
            at += 2 + popcount16(e[0]);
        }
        if (!placed) put_entry(nullptr, key);
        if (entries > kEntries) return false;
        if (!entries) {
            free(h);
            return true;
        }
        scratch_[0] = static_cast<u16>(h);
        scratch_[1] = static_cast<u16>(entries);
        // Room first, while the old block still counts as live; then replace it.
        if (used_ + out > kArenaWords) {
            compact();
            if (used_ + out > kArenaWords) {
                // The old block's own words would make room once dead.
                const u32 old = has(h) ? length(head_[h] - 1) : 0;
                if (used_ - old + out > kArenaWords) return false;
                free(h);
                compact();
            }
        }
        if (has(h)) free(h);
        const u32 to = used_;
        for (u32 i = 0; i < out; ++i) arena_[to + i] = scratch_[i];
        used_ += out;
        head_[h] = static_cast<u16>(to + 1);
        ++held_;
        return true;
    }
};
}}}
