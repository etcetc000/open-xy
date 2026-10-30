#include "sample_cache.hpp"
#include "sample_machines.hpp"
#include "barrier.hpp"
#include "stock_sample.hpp"

extern "C" {
void* sdk_sample_cache_alloc(opxy::u32) __asm__("sdk_sample_cache_alloc");
void sdk_sample_cache_free(void*) __asm__("sdk_sample_cache_free");
void sdk_sample_progress_update(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_sample_progress_update");
opxy::u32 sdk_sample_cache_stock_hash(const char*) __asm__("sdk_sample_cache_stock_hash");
void sdk_sample_cache_keys_lock() __asm__("sdk_sample_cache_keys_lock");
void sdk_sample_cache_keys_unlock() __asm__("sdk_sample_cache_keys_unlock");
const void* sdk_sample_cache_sample_acquire(opxy::u32) __asm__("sdk_sample_cache_sample_acquire");
void sdk_sample_cache_sample_release(opxy::u32) __asm__("sdk_sample_cache_sample_release");
}

namespace {
using opxy::u32;
using opxy::platform::barrier;
using opxy::platform::SampleMachine;
constexpr u32 kEntries = 256;
constexpr u32 kChunkFrames = 4096;
// One machine's analysis of one retained sample.
struct Entry {
    // The owner is published last and cleared before storage is released.
    const void* volatile owner;
    u32 engine;
    opxy::SampleAnalysis analysis;
    bool ready;
    u32 failure;  // progress state of an unsuccessful preparation, else 0
    u32* storage;
    u32 bytes;
};
static Entry entries[kEntries];
struct Progress {
    volatile u32 sequence;
    volatile u32 state;
    volatile u32 percent;
    volatile u32 generation;
};
static Progress progress;

// The stock pending track list contains at most 128 track/pattern records,
// each with 24 assignments. Stock browser auditions temporarily replace region
// paths, so the GUI wrapper supplies an empty list while that screen is open.
// Snapshotting runs on the existing GUI file worker while stock's busy flag
// prevents the GUI from rebuilding its pending list. Browser exit requests the
// next stock refresh, which publishes the final assignments normally.
constexpr u32 kAssignedKeys = 128u * 24u;
struct PendingTrack { const unsigned char* track; u32 priority; };
static u32 assigned_keys[kAssignedKeys], next_assigned_keys[kAssignedKeys];
static u32 assigned_count, force_send;

// The stock desired cache list is bounded to 200 entries. Leave spare capacity
// while keeping malformed/full inputs bounded. Its existing manager+0 mutex
// protects reset/add transactions and the worker's private snapshot copy.
static u32 wanted_keys[kEntries], worker_keys[kEntries];
static u32 wanted_count;
// Set when the wanted keys, a sample or the budget changed, or a wanted sample
// was not loaded yet; an unchanged worker pass does no reference or entry work.
static bool keys_dirty = true;
// "Cache full" is announced once until a release frees an entry or budget.
static bool full_reported;

bool contains(const u32* keys, u32 count, u32 key) {
    for (u32 i = 0; i < count; ++i) if (keys[i] == key) return true;
    return false;
}

// Canonicalize a bounded assignment snapshot without allocation or recursion.
// Sorting once also makes every IPC priority lookup logarithmic, and avoids
// needless refreshes when stock enumerates the same assignments in a new order.
void sift_down(u32* keys, u32 root, u32 count) {
    while (root < count / 2) {
        u32 child = root * 2 + 1;
        if (child + 1 < count && keys[child] < keys[child + 1]) ++child;
        if (keys[root] >= keys[child]) return;
        const u32 value = keys[root];
        keys[root] = keys[child];
        keys[child] = value;
        root = child;
    }
}

u32 sort_unique(u32* keys, u32 count) {
    for (u32 i = count / 2; i; --i) sift_down(keys, i - 1, count);
    for (u32 end = count; end > 1; --end) {
        const u32 value = keys[0];
        keys[0] = keys[end - 1];
        keys[end - 1] = value;
        sift_down(keys, 0, end - 1);
    }
    u32 unique = 0;
    for (u32 i = 0; i < count; ++i)
        if (!unique || keys[i] != keys[unique - 1]) keys[unique++] = keys[i];
    return unique;
}

bool assigned(u32 key) {
    u32 first = 0, last = assigned_count;
    while (first < last) {
        const u32 middle = first + (last - first) / 2;
        if (assigned_keys[middle] < key) first = middle + 1;
        else if (assigned_keys[middle] > key) last = middle;
        else return true;
    }
    return false;
}

void status(u32 state, u32 percent, bool next = false) {
    ++progress.sequence;
    barrier();
    progress.state = state;
    progress.percent = percent;
    if (next) ++progress.generation;
    barrier();
    ++progress.sequence;
}

void failed(Entry* entry, const void* object, u32 engine, u32 state) {
    if (entry) {
        entry->engine = engine;
        entry->analysis = opxy::SampleAnalysis{};
        entry->ready = false;
        entry->failure = state;
        entry->storage = nullptr;
        entry->bytes = 0;
        barrier();
        entry->owner = object;
    }
    // Remember unsuccessful preparation until stock evicts the sample. A
    // browser refresh must not repeatedly allocate or announce the same error.
    status(state, 0);
}

// Retained PCM the cache accepts: 2..2^20-1 frames, mono or stereo.
bool valid_source(const opxy::SampleView& sample) {
    return sample.data && sample.frames >= 2 && sample.frames <= 0x000fffffu &&
           (sample.channels == 1 || sample.channels == 2);
}
opxy::SampleView sample_view(const void* object) {
    const opxy::SampleView sample = opxy::platform::stock_sample_view(object);
    return valid_source(sample) ? sample : opxy::SampleView{nullptr, 0, 0, 1};
}
// Heap bytes one machine's published analyses hold.
u32 used_bytes(u32 engine) {
    u32 total = 0;
    for (u32 i = 0; i < kEntries; ++i)
        if (entries[i].owner && entries[i].engine == engine) total += entries[i].bytes;
    return total;
}

// Runs synchronously on the existing file worker, with a retained stock sample
// reference, after assigned keys have loaded. Stock PCM is never modified.
// Budgets are extension limits, not assumptions about stock cache size or
// available heap memory.
void prepare_for(const void* object, const SampleMachine& m) {
    Entry* free_entry = nullptr;
    for (u32 i = 0; i < kEntries; ++i) {
        if (entries[i].owner == object && entries[i].engine == m.engine()) return;
        if (!entries[i].owner && !free_entry) free_entry = &entries[i];
    }
    if (!free_entry) {
        if (!full_reported) {
            full_reported = true;
            status(1, 0, true);
            failed(nullptr, object, m.engine(), 4);
        }
        return;
    }
    const opxy::SampleView sample = sample_view(object);
    status(1, 0, true);
    // The machine sizes its storage; bound it before converting to bytes.
    const u32 words = sample.data ? m.analysis_words(&sample) : 0;
    if (!words) {
        failed(free_entry, object, m.engine(), 3);
        return;
    }
    const u32 budget = m.analysis_budget, used = used_bytes(m.engine());
    if (used > budget || words > budget / sizeof(u32) || words * sizeof(u32) > budget - used) {
        failed(free_entry, object, m.engine(), 4);
        return;
    }
    const u32 bytes = words * sizeof(u32);
    u32* storage = static_cast<u32*>(sdk_sample_cache_alloc(bytes));
    if (!storage) {
        failed(free_entry, object, m.engine(), 3);
        return;
    }
    opxy::SampleAnalysis result{};
    u32 step = opxy::prepare_failed;
    if (m.prepare_begin(&sample, storage, words)) {
        u32 cursor = 0;
        // frames < 2^20, so this product cannot overflow u32.
        while ((step = m.prepare_step(kChunkFrames, &cursor)) == opxy::prepare_running)
            status(1, (cursor < sample.frames ? cursor : sample.frames) * 100u / sample.frames);
    }
    if (step != opxy::prepare_done || !m.prepared(&result) || result.words != storage || result.word_count > words) {
        sdk_sample_cache_free(storage);
        failed(free_entry, object, m.engine(), 3);
        return;
    }
    free_entry->engine = m.engine();
    free_entry->analysis = result;
    free_entry->ready = true;
    free_entry->failure = 0;
    free_entry->storage = storage;
    free_entry->bytes = bytes;
    barrier();
    free_entry->owner = object;
    status(2, 100);
}
}

extern "C" u32 sdk_sample_cache_snapshot_keys(u32 new_sum, u32 old_sum,
                                               const void* pending, u32 count) {
    u32 next_count = 0;
    if (pending && count <= 128) {
        const PendingTrack* tracks = static_cast<const PendingTrack*>(pending);
        for (u32 t = 0; t < count; ++t) {
            const unsigned char* track = tracks[t].track;
            if (!track || !opxy::platform::sample_analysis(*reinterpret_cast<const u32*>(track + 24))) continue;
            for (u32 r = 0; r < 24; ++r) {
                const char* path = reinterpret_cast<const char*>(track + 0x396c + r * 128);
                u32 length = 0;
                while (length < 96 && path[length]) ++length;
                if (!length || length == 96) continue;
                next_assigned_keys[next_count++] = sdk_sample_cache_stock_hash(path);
            }
        }
    }
    next_count = sort_unique(next_assigned_keys, next_count);
    bool changed = next_count != assigned_count;
    if (!changed) {
        for (u32 i = 0; i < next_count; ++i)
            if (next_assigned_keys[i] != assigned_keys[i]) { changed = true; break; }
    }
    if (!changed) return old_sum;
    for (u32 i = 0; i < next_count; ++i) assigned_keys[i] = next_assigned_keys[i];
    assigned_count = next_count;
    force_send = 1;
    // Stock has two path-key-only change detectors. Force its outer refresh
    // when assignment ownership changes, including preview -> assigned hits.
    // The wrapper around its inner detector also sends the existing IPC list.
    return new_sum ^ 1u;
}

extern "C" u32 sdk_sample_cache_tag_priority(u32 key, u32 priority) {
    // Authenticated stock priorities are 0/1/5/6/10/11/15/16/99. The marker
    // travels in the existing priority byte and is removed before stock uses
    // it for cache ordering. Unknown priorities are forwarded unchanged.
    return priority < 128 && assigned(key)
        ? priority | 128u : priority;
}

extern "C" u32 sdk_sample_cache_accept_key(u32 key, u32 priority) {
    if (priority < 128 || priority > 255) return priority;
    if (wanted_count < kEntries && !contains(wanted_keys, wanted_count, key)) {
        wanted_keys[wanted_count++] = key;
        keys_dirty = true;
    }
    return priority & 127u;
}

extern "C" void sdk_sample_cache_reset_keys() {
    wanted_count = 0;
    keys_dirty = true;
}

extern "C" u32 sdk_sample_cache_take_force_send() {
    const u32 result = force_send;
    force_send = 0;
    return result;
}

extern "C" void sdk_sample_cache_prepare_keys() {
    // The maintenance callback has returned, releasing both stock mutexes.
    // Copy a complete committed desired-list transaction while holding +0,
    // then release it before reference acquisition (+4) and any analysis.
    sdk_sample_cache_keys_lock();
    if (!keys_dirty) {
        sdk_sample_cache_keys_unlock();
        return;
    }
    keys_dirty = false;
    const u32 count = wanted_count;
    for (u32 i = 0; i < count; ++i) worker_keys[i] = wanted_keys[i];
    sdk_sample_cache_keys_unlock();
    bool pending = false;
    for (u32 i = 0; i < count; ++i) {
        const u32 key = worker_keys[i];
        const void* object = sdk_sample_cache_sample_acquire(key);
        if (!object) { pending = true; continue; }
        sdk_sample_cache_prepare(object);
        sdk_sample_cache_sample_release(key);
    }
    if (pending) keys_dirty = true;
}

extern "C" void sdk_sample_cache_prepare(const void* object) {
    // Samples decoded only for browser preview are not requested. Every
    // registered analysis machine prepares its own result: the request names
    // the sample, not the engine it was assigned to.
    if (!object) return;
    for (u32 i = 0; i < sdk_sample_machine_count; ++i) {
        const SampleMachine& m = sdk_sample_machines[i];
        if (m.analysis_budget && m.ready() == 1) prepare_for(object, m);
    }
}

extern "C" void sdk_sample_cache_release(const void* object) {
    // The stock worker reaches this hook only after the cache reference count
    // permits destruction. Thus no audio voice can still borrow this entry.
    // It executes before stock releases PCM or the sample object's address.
    if (!object) return;
    keys_dirty = true;
    full_reported = false;
    bool freed = false;
    for (u32 i = 0; i < kEntries; ++i) {
        Entry& entry = entries[i];
        if (entry.owner != object) continue;
        entry.owner = nullptr;
        barrier();
        u32* storage = entry.storage;
        entry.analysis = opxy::SampleAnalysis{};
        entry.ready = false;
        entry.storage = nullptr;
        entry.bytes = 0;
        if (storage) {
            sdk_sample_cache_free(storage);
            freed = true;
        }
    }
    // Budget is free again: samples that failed for lack of it may retry.
    if (freed)
        for (u32 j = 0; j < kEntries; ++j)
            if (entries[j].owner && entries[j].failure == 4) entries[j].owner = nullptr;
}

extern "C" const opxy::SampleAnalysis* sdk_sample_cache_find(const void* object, u32 engine) {
    if (!object) return nullptr;
    // The factory's stock reference protects the matching owner's lifetime;
    // this function must never be used with an unowned raw sample pointer.
    // Entries for unrelated samples may change, but fixed metadata is never
    // freed and no analysis field is read until the exact owner matches.
    for (u32 i = 0; i < kEntries; ++i) {
        if (entries[i].owner == object) {
            barrier();
            if (entries[i].engine == engine) return entries[i].ready ? &entries[i].analysis : nullptr;
        }
    }
    return nullptr;
}

// Whether this sample's preparation has failed; a voice then checks rarely.
extern "C" u32 sdk_sample_cache_failed(const void* object, u32 engine) {
    if (!object) return 0;
    for (u32 i = 0; i < kEntries; ++i)
        if (entries[i].owner == object && entries[i].engine == engine) return entries[i].failure != 0;
    return 0;
}

extern "C" void sdk_sample_gui_tick() {
    // Read the worker on this firmware role, not unproven cross-role globals.
    // A preempted writer simply delays the overlay until the next GUI frame.
    const u32 sequence = progress.sequence;
    if (sequence & 1u) return;
    barrier();
    const u32 state = progress.state;
    const u32 percent = progress.percent;
    const u32 generation = progress.generation;
    barrier();
    if (progress.sequence == sequence) sdk_sample_progress_update(state, percent, generation);
}
