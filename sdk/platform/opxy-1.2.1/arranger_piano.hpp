#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
inline u32 piano_vertical_zoom(u32 rows, i32 delta) {
    static const u32 sizes[] = {6, 8, 12, 16, 24};
    u32 index = 0; while (index < 4 && sizes[index] < rows) ++index;
    if (delta > 0 && index) --index;
    if (delta < 0 && index < 4) ++index;
    return sizes[index];
}
inline u32 piano_pitch_low(u32 pitch, u32 rows) {
    const u32 low = pitch > rows / 2 ? pitch - rows / 2 : 0;
    return low > 128 - rows ? 128 - rows : low;
}
inline bool piano_black_key(u32 pitch) {
    const u32 key = pitch % 12;
    return key == 1 || key == 3 || key == 6 || key == 8 || key == 10;
}
inline u32 piano_row_y(u32 row, u32 rows) { return 28 + row * 168 / rows; }
struct PianoNote { i32 position; u32 duration; u8 pitch, velocity, flags, padding; };

// GUI-only, source-local native ticks (480 per step). Caller must stop transport,
// disarm REC and require the native pending-note count to be zero before editing.
// The insertion callback is stock 8d58f24c(container,note,-1): -1 disables dedupe.
// Unlink below transcribes stock 8d58f076..0ca / 8d5ab9b6..9e8.
class PianoEditor {
public:
    typedef i32 (*Insert)(void*, const PianoNote*);
    static const u32 capacity = 120, bytes = 0x788, historyCapacity = 8;
    explicit constexpr PianoEditor(Insert insert = 0) : source_(0), project_(0), generation_(0), insert_(insert),
        history_{}, scratch_{}, size_(0), cursor_(0), group_(0) {}
    void bind(void* source, u32 project, u32 generation) {
        if (source_ != source || project_ != project || generation_ != generation) reset();
        source_ = static_cast<u8*>(source); project_ = project; generation_ = generation;
    }
    void reset() { size_ = cursor_ = group_ = 0; }
    void breakGroup() { group_ = 0; }
    bool valid() const { return source_ && source_[0] && source_[8] < 16 && validate(data()); }
    u32 span() const { return source_ ? source_[0] * 480u : 0; }
    u32 scaleRaw() const {
        static const u32 scales[16] = {5040,3360,2240,1680,1260,840,560,420,336,280,240,210,210,140,105,105};
        return source_ && source_[8] < 16 ? scales[source_[8]] : 1680;
    }
    // Source-local conversion; clamp to one source period before multiplication.
    u32 timelineToNative(u32 ticks) const {
        const u32 raw = scaleRaw(), limit = span() * 1680u / raw;
        if (ticks >= limit) return span();
        return (ticks / 1680u) * raw + (ticks % 1680u) * raw / 1680u;
    }
    u32 nativeToTimeline(u32 ticks) const {
        if (ticks > span()) ticks = span();
        return ticks * 1680u / scaleRaw();
    }
    u32 count() const { return valid() ? word(data(), 0x786) : 0; }
    bool first(PianoNote& out) const {
        if (!valid() || !word(data(), 0x786)) return false;
        read(data(), static_cast<u32>(link(data(), 0x782)), out); return true;
    }
    bool note(u32 index, PianoNote& out) const {
        if (!valid() || !active(data(), index)) return false;
        read(data(), index, out); return true;
    }
    i32 hit(i32 time, u32 pitch) const {
        if (!valid()) return -1;
        for (i32 i = link(data(), 0x782); i != -1; i = link(data(), i * 16 + 2)) {
            PianoNote n; read(data(), i, n);
            if (n.pitch == pitch && time >= n.position && (static_cast<u32>(time) - static_cast<u32>(n.position)) < n.duration) return i;
        }
        return -1;
    }
    bool add(PianoNote n, i32& selected) {
        breakGroup();
        if (!editable() || !insert_ || count() == capacity || !acceptable(n)) return false;
        copy(scratch_.data(), data()); n.flags = 0; n.padding = 0;
        const i32 index = insert_(scratch_.data(), &n);
        if (index < 0 || index >= static_cast<i32>(capacity) || !validate(scratch_.data()) || !active(scratch_.data(), index) || word(scratch_.data(), 0x786) != count() + 1) return false;
        commit(0); selected = index; return true;
    }
    bool erase(u32 index) {
        breakGroup();
        if (!editable() || !active(data(), index)) return false;
        copy(scratch_.data(), data()); unlink(scratch_.data(), index);
        if (!validate(scratch_.data())) return false;
        commit(0); return true;
    }
    bool update(u32 index, PianoNote n, u32 group = 0) {
        if (!editable() || !active(data(), index)) return false;
        PianoNote old; read(data(), index, old);
        // Stock may retain early notes before zero or long/tied durations. A
        // transpose/velocity edit preserves those existing values unchanged.
        if (n.pitch >= 128 || !n.velocity || n.velocity >= 128 ||
            (n.position != old.position && (n.position < 0 || static_cast<u32>(n.position) >= span())) ||
            (n.duration != old.duration && (!n.duration || n.duration > span()))) return false;
        n.flags = old.flags; n.padding = old.padding;
        if (old.position == n.position && old.duration == n.duration && old.pitch == n.pitch && old.velocity == n.velocity) return false;
        copy(scratch_.data(), data());
        if (old.position != n.position) {
            if (!insert_) return false;
            unlink(scratch_.data(), index);
            if (insert_(scratch_.data(), &n) != static_cast<i32>(index)) return false;
        } else write(scratch_.data(), index, n);
        if (!validate(scratch_.data())) return false;
        commit(group); return true;
    }
    bool undo() { breakGroup(); if (!editable() || !cursor_) return false; exchange(--cursor_); return true; }
    bool redo() { breakGroup(); if (!editable() || cursor_ == size_) return false; exchange(cursor_++); return true; }
    bool canUndo() const { return cursor_ != 0; }
    bool canRedo() const { return cursor_ != size_; }
private:
    struct Container { u32 words[bytes / 4]; u8* data() { return reinterpret_cast<u8*>(words); } };
    u8* source_; u32 project_, generation_; Insert insert_;
    Container history_[historyCapacity], scratch_;
    u32 size_, cursor_, group_;
    u8* data() const { return source_ + 0x457c; }
    static u32 word(const u8* p, u32 o) { return p[o] | (static_cast<u32>(p[o+1]) << 8); }
    static i32 link(const u8* p, u32 o) { const u32 w = word(p,o); return w < 32768 ? static_cast<i32>(w) : static_cast<i32>(w) - 65536; }
    static u32 dword(const u8* p, u32 o) { return word(p,o) | (word(p,o+2) << 16); }
    static void put(u8* p, u32 o, u32 v) { p[o] = static_cast<u8>(v); p[o+1] = static_cast<u8>(v >> 8); }
    static void put32(u8* p, u32 o, u32 v) { put(p,o,v); put(p,o+2,v >> 16); }
    static void copy(u8* to, const u8* from) { for (u32 i=0;i<bytes;++i) to[i]=from[i]; }
    static bool active(const u8* p, u32 node) {
        if (node >= capacity) return false;
        for (i32 i=link(p,0x782); i!=-1; i=link(p,i*16+2)) if (static_cast<u32>(i)==node) return true;
        return false;
    }
    static bool validate(const u8* p) {
        bool seen[capacity] = {}; u32 n=0; i32 prev=-1, position=0;
        if (word(p,0x786)>capacity) return false;
        for (i32 i=link(p,0x782); i!=-1; i=link(p,i*16+2)) {
            if (i<0 || i>=static_cast<i32>(capacity) || seen[i] || link(p,i*16)!=prev) return false;
            const i32 nextPosition=static_cast<i32>(dword(p,i*16+4));
            if (n && nextPosition<position) return false;
            position=nextPosition; seen[i]=true; prev=i; ++n;
        }
        if (prev!=link(p,0x784) || n!=word(p,0x786)) return false;
        for (i32 i=link(p,0x780); i!=-1; i=link(p,i*16+2)) {
            if (i<0 || i>=static_cast<i32>(capacity) || seen[i]) return false;
            seen[i]=true; ++n;
        }
        return n==capacity;
    }
    bool editable() const {
        if (!valid()) return false;
        for (i32 i=link(data(),0x782);i!=-1;i=link(data(),i*16+2)) if (data()[i*16+14]&2) return false;
        return true;
    }
    bool acceptable(const PianoNote& n) const {
        return span() && n.position>=0 && static_cast<u32>(n.position)<span() && n.duration && n.duration<=span() && n.pitch<128 && n.velocity && n.velocity<128;
    }
    static void read(const u8* p, u32 i, PianoNote& n) {
        const u32 o=i*16+4; n.position=static_cast<i32>(dword(p,o)); n.duration=dword(p,o+4);
        n.pitch=p[o+8]; n.velocity=p[o+9]; n.flags=p[o+10]; n.padding=p[o+11];
    }
    static void write(u8* p, u32 i, const PianoNote& n) {
        const u32 o=i*16+4; put32(p,o,static_cast<u32>(n.position)); put32(p,o+4,n.duration);
        p[o+8]=n.pitch; p[o+9]=n.velocity; p[o+10]=n.flags;
    }
    static void unlink(u8* p, u32 i) {
        const i32 prev=link(p,i*16), next=link(p,i*16+2);
        put(p,prev==-1 ? 0x782 : prev*16+2,static_cast<u32>(next));
        put(p,next==-1 ? 0x784 : next*16,static_cast<u32>(prev));
        put(p,i*16+2,word(p,0x780)); put(p,0x780,i); put(p,0x786,word(p,0x786)-1);
    }
    void commit(u32 group) {
        if (!(group && group==group_ && cursor_==size_ && size_)) {
            size_=cursor_;
            if (size_==historyCapacity) { for(u32 i=1;i<size_;++i) history_[i-1]=history_[i]; --size_; }
            copy(history_[size_++].data(),data()); cursor_=size_;
        }
        group_=group; copy(data(),scratch_.data());
    }
    void exchange(u32 i) {
        copy(scratch_.data(),data()); copy(data(),history_[i].data()); copy(history_[i].data(),scratch_.data());
    }
};
} }
