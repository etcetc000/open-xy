#pragma once
#include <opxy/arranger.hpp>

namespace opxy { namespace platform {

// GUI-only timeline history. Native pattern contents are intentionally outside
// snapshots: append-only source identities keep undo/redo references valid.
// Undo does not reclaim appended patterns from the native 16-pattern lane pool.
class ArrangerHistory {
public:
    static const u32 capacity = 16;
    constexpr ArrangerHistory() : entries_{}, pending_{}, count_(0), cursor_(0),
        group_(0), pending_group_(0), project_(0), generation_(0), bound_(false), pending_valid_(false) {}

    void reset() {
        count_ = cursor_ = group_ = pending_group_ = project_ = generation_ = 0;
        bound_ = pending_valid_ = false;
    }
    // Nonzero group joins consecutive turns of the same encoder/clip. Break
    // the group on navigation, another action, or leaving the arranger view.
    void breakGroup() { group_ = 0; }
    void begin(const arranger::Model& model, u32 track, u32 cursor,
        u32 project, u32 generation, u32 group = 0) {
        bind(project, generation);
        if (group != group_) breakGroup();
        pending_.model = model; pending_.track = track; pending_.cursor = cursor;
        pending_group_ = group; pending_valid_ = true;
    }
    // Only an actual timeline edit truncates redo; failed edits preserve it.
    bool commit(const arranger::Model& model) {
        if (!pending_valid_) return false;
        pending_valid_ = false;
        if (equal(pending_.model, model)) return false;
        if (pending_group_ && pending_group_ == group_ && cursor_ == count_ && count_) return true;
        count_ = cursor_;
        if (count_ == capacity) {
            for (u32 i = 1; i < count_; ++i) entries_[i - 1] = entries_[i];
            --count_;
        }
        entries_[count_++] = pending_;
        cursor_ = count_; group_ = pending_group_;
        return true;
    }
    // Abandon capture without changing the caller's model.
    void cancel() { pending_valid_ = false; }
    bool canUndo() const { return cursor_ != 0; }
    bool canRedo() const { return cursor_ < count_; }
    bool undo(arranger::Model& model, u32& track, u32& cursor, u32 project, u32 generation) {
        bind(project, generation); breakGroup(); cancel();
        if (!canUndo()) return false;
        exchange(--cursor_, model, track, cursor);
        return true;
    }
    bool redo(arranger::Model& model, u32& track, u32& cursor, u32 project, u32 generation) {
        bind(project, generation); breakGroup(); cancel();
        if (!canRedo()) return false;
        exchange(cursor_++, model, track, cursor);
        return true;
    }

private:
    struct Snapshot { arranger::Model model; u32 track, cursor; };
    Snapshot entries_[capacity], pending_;
    u32 count_, cursor_, group_, pending_group_, project_, generation_;
    bool bound_, pending_valid_;

    void bind(u32 project, u32 generation) {
        if (bound_ && project_ == project && generation_ == generation) return;
        reset(); project_ = project; generation_ = generation; bound_ = true;
    }
    void exchange(u32 index, arranger::Model& model, u32& track, u32& cursor) {
        // Reuse member scratch: a Model temporary would consume a large GUI stack frame.
        pending_.model = model; pending_.track = track; pending_.cursor = cursor;
        model = entries_[index].model; track = entries_[index].track; cursor = entries_[index].cursor;
        entries_[index] = pending_;
    }
    static bool equal(const arranger::Model& a, const arranger::Model& b) {
        if (a.loopEnabled() != b.loopEnabled() || a.loopStart() != b.loopStart() || a.loopEnd() != b.loopEnd())
            return false;
        for (u32 i = 0; i < arranger::Model::capacity; ++i) {
            const arranger::Clip* x = a.clip(i); const arranger::Clip* y = b.clip(i);
            if (!x || !y) { if (x != y) return false; continue; }
            if (x->track != y->track || x->pattern != y->pattern || x->start != y->start ||
                x->length != y->length || x->offset != y->offset || x->source_length != y->source_length ||
                x->phase_remainder != y->phase_remainder || x->muted != y->muted)
                return false;
        }
        // Playback position and edit cursor navigation alone are not undoable edits.
        return true;
    }
};
}}
