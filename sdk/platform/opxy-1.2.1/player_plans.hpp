// Bounded edits to the records consumed by the stock player scheduler.
#pragma once
#include "player_activity.hpp"
namespace opxy { namespace platform {
inline bool valid_player_plan(const player::Plan& plan) {
    if (plan.count > player::max_tones) return false;
    for (u32 i = 0; i < plan.count; ++i)
        if (plan.tones[i].pitch > 127 || plan.tones[i].velocity > 127) return false;
    return true;
}
inline void player_append(PlayerChord& record, const player::Plan& plan) {
    u32 count = record.count;
    for (u32 i = 0; i < plan.count && count < player::max_tones; ++i) {
        bool exists = false;
        for (u32 j = 0; j < count; ++j)
            if (record.tones[j].pitch == plan.tones[i].pitch) exists = true;
        if (exists) continue;
        PlayerTone& tone = record.tones[count++];
        tone.delay = plan.tones[i].delay;
        tone.pitch = plan.tones[i].pitch;
        tone.velocity = plan.tones[i].velocity;
        tone.unused[0] = tone.unused[1] = 0;
    }
    record.count = count;
}
// Release is the stock note-off boundary. Reuse cleared records immediately,
// including when all eight slots were occupied before a chord replacement.
template<class Release> void player_store(PlayerChord* records, volatile u32& held,
        u32 root, const player::Plan& plan, bool existing_only, bool add, Release release) {
    if (root > 127 || held > kPlayerChords || !valid_player_plan(plan)) return;
    u32 at = kPlayerChords, empty = kPlayerChords;
    for (u32 i = 0; i < held; ++i) {
        if (records[i].count > player::max_tones) return;
        if (records[i].count && records[i].root == root) at = i;
        if (!records[i].count) empty = i;
    }
    if (existing_only && at == kPlayerChords) return;
    if (at != kPlayerChords && !add) release(root);
    if (!plan.count) return;
    if (at == kPlayerChords) {
        at = empty != kPlayerChords ? empty : held;
        if (at >= kPlayerChords) return;
        records[at].count = 0;
    }
    records[at].root = root;
    player_append(records[at], plan);
    if (at == held) held = held + 1;
}
}}
