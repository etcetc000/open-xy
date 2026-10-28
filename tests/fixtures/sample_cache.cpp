// Host lifecycle tests for the real cache with an opaque analysis producer.
// The SDK owns scheduling/publication/lifetime; an engine owns the payload.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>
extern "C" void host_barrier();
#include "../../sdk/platform/opxy-1.2.1/sample_cache.cpp"

static unsigned analysis_words(const opxy::SampleView& sample) {
    return 4 + ((sample.frames + 31) / 32) * sample.channels;
}
struct Preparation {
    opxy::SampleView sample;
    unsigned* storage;
    unsigned cursor, words;
};
static Preparation preparation;
static unsigned fake_words(const opxy::SampleView* sample) {
    return sample->data && sample->frames >= 2 ? analysis_words(*sample) : 0;
}
static unsigned fake_begin(const opxy::SampleView* sample, unsigned* storage, unsigned words) {
    preparation = Preparation{*sample, storage, 0, analysis_words(*sample)};
    return storage && words >= preparation.words;
}
static unsigned fake_step(unsigned frames, unsigned* cursor) {
    preparation.cursor += std::min(frames, preparation.sample.frames - preparation.cursor);
    *cursor = preparation.cursor;
    if (preparation.cursor < preparation.sample.frames) return opxy::prepare_running;
    for (unsigned i = 0; i < preparation.words; ++i) preparation.storage[i] = i ^ 0x51a7u;
    return opxy::prepare_done;
}
static unsigned fake_prepared(opxy::SampleAnalysis* out) {
    if (preparation.cursor != preparation.sample.frames) return 0;
    *out = opxy::SampleAnalysis{preparation.sample, preparation.storage, preparation.words, 0x51a7u};
    return 1;
}
// Two analysis machines: engine 35 with the full budget, and engine 46, only
// registered for the per-engine checks, with 64 KiB.
static bool second_registered;
static int first_ready() { return 1; }
static int second_ready() { return second_registered; }
// Engine IDs are absolute link-time symbols on the device; here, their values.
static const char* engine_symbol(unsigned id) { return reinterpret_cast<const char*>(static_cast<__UINTPTR_TYPE__>(id)); }
const opxy::platform::SampleMachine sdk_sample_machines[] = {
    {engine_symbol(35), opxy::platform::SamplePage::drums, first_ready, nullptr, nullptr, nullptr, nullptr, 2u * 1024u * 1024u,
     fake_words, fake_begin, fake_step, fake_prepared, 0},
    {engine_symbol(46), opxy::platform::SamplePage::drums, second_ready, nullptr, nullptr, nullptr, nullptr, 64u * 1024u,
     fake_words, fake_begin, fake_step, fake_prepared, 0},
};
const unsigned sdk_sample_machine_count = 2;
extern "C" const opxy::platform::SampleMachine* sdk_sample_machine(unsigned engine) {
    for (const auto& m : sdk_sample_machines)
        if (m.engine() == engine) return m.ready() == 1 ? &m : nullptr;
    return nullptr;
}

static unsigned checks, allocations, frees, overlay_calls;
static bool allocation_failure, observing;
static const void* observed;
static std::unordered_map<void*, unsigned> blocks;
static std::unordered_map<unsigned, const void*> samples;
static unsigned key_locks, key_unlocks, acquired, released, hashes;
static bool key_lock_held;
static void require(bool result, const char* text) {
    ++checks;
    if (!result) { std::cerr << text << '\n'; std::exit(1); }
}
struct alignas(void*) Object {
    unsigned char bytes[64]{};
    Object(const opxy::SampleView& sample) {
        const unsigned head = 1;
        std::memcpy(bytes, &head, 4);
        std::memcpy(bytes + 8, &sample.data, sizeof(sample.data));
        std::memcpy(bytes + 0x30, &sample.frames, 4);
        std::memcpy(bytes + 0x38, &sample.rate, 4);
        bytes[0x3c] = sample.channels == 2;
    }
};
extern "C" void* sdk_sample_cache_alloc(unsigned bytes) {
    require(!key_lock_held, "preparation held desired-list mutex");
    ++allocations;
    if (allocation_failure) return nullptr;
    auto* block = static_cast<unsigned*>(std::malloc(bytes + 8));
    require(block != nullptr, "host allocation failed");
    block[0] = 0xcafe1234;
    block[1 + bytes / 4] = 0xdead4321;
    blocks[block + 1] = bytes;
    return block + 1;
}
extern "C" void sdk_sample_cache_free(void* pointer) {
    ++frees;
    const auto found = blocks.find(pointer);
    require(found != blocks.end(), "double or foreign free");
    auto* block = static_cast<unsigned*>(pointer) - 1;
    require(block[0] == 0xcafe1234 && block[1 + found->second / 4] == 0xdead4321,
            "analysis wrote outside its allocation");
    blocks.erase(found);
    std::free(block);
}
extern "C" void host_barrier() {
    if (!observed || observing) return;
    observing = true;
    const auto* published = sdk_sample_cache_find(observed, 35);
    if (published) {
        require(published->words && published->word_count == analysis_words(published->sample) &&
                published->info == 0x51a7u, "partially prepared analysis became visible");
        for (unsigned i = 0; i < published->word_count; ++i)
            require(published->words[i] == (i ^ 0x51a7u), "published payload is incomplete");
    }
    observing = false;
}
extern "C" void sdk_sample_progress_update(unsigned state, unsigned percent, unsigned) {
    ++overlay_calls;
    require(state <= 4 && percent <= 100, "invalid GUI snapshot");
}
extern "C" unsigned sdk_sample_cache_stock_hash(const char* path) {
    ++hashes;
    unsigned hash = 2166136261u;
    for (unsigned i = 0; path[i]; ++i) {
        require(i < 96, "unbounded stock path hash input");
        hash = (hash ^ static_cast<unsigned char>(path[i])) * 16777619u;
    }
    return hash;
}
extern "C" void sdk_sample_cache_keys_lock() {
    require(!key_lock_held, "nested desired-list lock");
    key_lock_held = true;
    ++key_locks;
}
extern "C" void sdk_sample_cache_keys_unlock() {
    require(key_lock_held, "unbalanced desired-list unlock");
    key_lock_held = false;
    ++key_unlocks;
}
extern "C" const void* sdk_sample_cache_sample_acquire(unsigned key) {
    require(!key_lock_held, "sample acquisition held desired-list mutex");
    const auto found = samples.find(key);
    if (found == samples.end()) return nullptr;
    ++acquired;
    return found->second;
}
extern "C" void sdk_sample_cache_sample_release(unsigned) {
    require(!key_lock_held && acquired > released, "sample reference release imbalance");
    ++released;
}

int main() {
    std::vector<opxy::i16> pcm(0xfffff, 0);
    opxy::SampleView small{pcm.data(), 8193, 44100, 1};
    Object first(small), second(small);
    require(!sdk_sample_cache_find(nullptr, 35), "null cache lookup");
    observed = &first;
    sdk_sample_cache_prepare(&first);
    const auto* first_analysis = sdk_sample_cache_find(&first, 35);
    require(first_analysis != nullptr, "preparation did not publish");
    require(first_analysis->sample.data == small.data && first_analysis->sample.frames == small.frames &&
            first_analysis->sample.channels == small.channels, "published sample identity changed");
    const unsigned before = allocations;
    for (unsigned i = 0; i < 1000; ++i)
        require(sdk_sample_cache_find(&first, 35) == first_analysis, "cache lookup changed immutable entry");
    sdk_sample_cache_prepare(&first);
    require(allocations == before, "cache hit or audio lookup allocated");
    sdk_sample_cache_release(&second);
    require(sdk_sample_cache_find(&first, 35) == first_analysis, "foreign release evicted a sample");
    sdk_sample_cache_release(&first);
    require(!sdk_sample_cache_find(&first, 35) && blocks.empty(), "release left stale cache entry");
    sdk_sample_cache_release(&first);
    observed = nullptr;

    // A stereo result must publish both channels atomically within one guarded allocation.
    opxy::SampleView stereo{pcm.data(), 8193, 44100, 2};
    Object stereo_object(stereo);
    observed = &stereo_object;
    sdk_sample_cache_prepare(&stereo_object);
    const auto* stereo_analysis = sdk_sample_cache_find(&stereo_object, 35);
    require(stereo_analysis && stereo_analysis->sample.channels == 2 &&
            stereo_analysis->word_count == analysis_words(stereo), "stereo publication is incomplete");
    sdk_sample_cache_release(&stereo_object);
    require(blocks.empty(), "stereo release leaked allocation");
    observed = nullptr;

    // Each registered analysis machine prepares and owns its own result for a
    // shared sample, within its own budget.
    second_registered = true;
    sdk_sample_cache_prepare(&first);
    const auto* mine = sdk_sample_cache_find(&first, 35);
    const auto* theirs = sdk_sample_cache_find(&first, 46);
    require(mine && theirs && mine != theirs && mine->words != theirs->words &&
            !sdk_sample_cache_find(&first, 47), "analysis is not keyed by engine");
    require(used_bytes(46) == 4 * analysis_words(small), "budget not charged to its machine");
    opxy::SampleView longest{pcm.data(), 0xfffff, 44100, 1};
    Object big(longest);
    sdk_sample_cache_prepare(&big);
    require(sdk_sample_cache_find(&big, 35) && !sdk_sample_cache_failed(&big, 35) &&
            !sdk_sample_cache_find(&big, 46) && sdk_sample_cache_failed(&big, 46),
            "one machine's budget limited another");
    sdk_sample_cache_release(&big);
    sdk_sample_cache_release(&first);
    require(blocks.empty() && !sdk_sample_cache_failed(&big, 46), "release left another machine's entry");
    second_registered = false;

    allocation_failure = true;
    sdk_sample_cache_prepare(&first);
    require(!sdk_sample_cache_find(&first, 35) && blocks.empty(), "allocation failure exposed cache");
    allocation_failure = false;
    const unsigned failed_allocations = allocations;
    sdk_sample_cache_prepare(&first);
    require(allocations == failed_allocations && !sdk_sample_cache_find(&first, 35),
            "failed preparation retried on browser refresh");
    sdk_sample_cache_release(&first);
    sdk_sample_cache_prepare(&first);
    require(sdk_sample_cache_find(&first, 35) != nullptr, "failed sample could not retry");
    sdk_sample_cache_release(&first);

    opxy::SampleView tiny{pcm.data(), 2, 44100, 1};
    std::vector<Object> tiny_objects(257, Object(tiny));
    for (unsigned i = 0; i < 256; ++i) sdk_sample_cache_prepare(&tiny_objects[i]);
    const unsigned full_allocations = allocations;
    sdk_sample_cache_prepare(&tiny_objects[256]);
    require(!sdk_sample_cache_find(&tiny_objects[256], 35) && allocations == full_allocations,
            "metadata capacity overrun or unexpected allocation");
    const unsigned full_generation = progress.generation;
    sdk_sample_cache_prepare(&tiny_objects[256]);
    require(progress.generation == full_generation, "full cache announced again on the next worker pass");
    sdk_sample_cache_release(&tiny_objects[80]);
    sdk_sample_cache_prepare(&tiny_objects[256]);
    require(sdk_sample_cache_find(&tiny_objects[256], 35) != nullptr, "released slot not reusable");
    for (auto& object : tiny_objects) sdk_sample_cache_release(&object);
    require(blocks.empty(), "capacity test leaked storage");

    opxy::SampleView maximum{pcm.data(), 0xfffff, 44100, 1};
    const unsigned large_bytes = 4 * analysis_words(maximum);
    const unsigned fitting = (2u * 1024u * 1024u) / large_bytes;
    std::vector<Object> large_objects(fitting + 1, Object(maximum));
    for (unsigned i = 0; i < fitting; ++i) {
        sdk_sample_cache_prepare(&large_objects[i]);
        require(sdk_sample_cache_find(&large_objects[i], 35) != nullptr, "valid budget preparation failed");
    }
    const unsigned budget_allocations = allocations;
    sdk_sample_cache_prepare(&large_objects[fitting]);
    require(!sdk_sample_cache_find(&large_objects[fitting], 35) && allocations == budget_allocations,
            "analysis memory budget exceeded");
    // Releasing another sample frees budget; the failed sample retries without being evicted.
    sdk_sample_cache_release(&large_objects[0]);
    sdk_sample_cache_prepare(&large_objects[fitting]);
    require(sdk_sample_cache_find(&large_objects[fitting], 35) != nullptr, "failed sample did not retry once budget was freed");
    for (auto& object : large_objects) sdk_sample_cache_release(&object);

    for (unsigned frames : {0u, 1u, 0x100000u, 0xffffffffu}) {
        opxy::SampleView invalid{pcm.data(), frames, 44100, 1};
        Object object(invalid);
        const unsigned old_allocations = allocations;
        sdk_sample_cache_prepare(&object);
        require(!sdk_sample_cache_find(&object, 35) && allocations == old_allocations,
                "invalid source frames reached allocation or analysis");
        sdk_sample_cache_release(&object);
    }

    // Browser preview creates a stock PCM object, but no assignment marker.
    // It must neither allocate analysis nor acquire a sample reference.
    std::vector<unsigned char> track(0x456c), other_track(0x456c);
    const unsigned custom_id = 35, sampler_id = 2;
    std::memcpy(track.data() + 24, &custom_id, 4);
    std::memcpy(other_track.data() + 24, &sampler_id, 4);
    const char* path = "samples/loop.wav";
    std::strcpy(reinterpret_cast<char*>(track.data() + 0x396c), path);
    std::strcpy(reinterpret_cast<char*>(other_track.data() + 0x396c), "samples/stock.wav");
    const unsigned key = sdk_sample_cache_stock_hash(path);
    samples[key] = &first;
    const unsigned preview_allocations = allocations;
    sdk_sample_cache_reset_keys();
    require(sdk_sample_cache_accept_key(key, 0) == 0, "preview priority changed");
    sdk_sample_cache_prepare_keys();
    require(allocations == preview_allocations && acquired == 0,
            "browser preview prepared analysis");

    PendingTrack pending[] = {{track.data(), 0}, {other_track.data(), 99}};
    require(sdk_sample_cache_snapshot_keys(123, 123, pending, 2) != 123 &&
            sdk_sample_cache_take_force_send() == 1 && !sdk_sample_cache_take_force_send(),
            "preview-cache-hit assignment failed to force both stock refresh gates");
    require(sdk_sample_cache_snapshot_keys(123, 123, pending, 2) == 123 &&
            !sdk_sample_cache_take_force_send(), "unchanged assignment forced IPC resend");
    for (unsigned priority : {0u, 1u, 5u, 6u, 10u, 11u, 15u, 16u, 99u}) {
        const unsigned tagged = sdk_sample_cache_tag_priority(key, priority);
        require(tagged == (priority | 128u), "assigned priority missing marker");
        require(sdk_sample_cache_accept_key(key, tagged) == priority,
                "marker changed stock ordering priority");
    }
    require(wanted_count == 1, "duplicate wanted keys not deduplicated");
    sdk_sample_cache_prepare_keys();
    require(sdk_sample_cache_find(&first, 35) && acquired == 1 && released == 1 &&
            key_locks == key_unlocks, "assigned cache hit was not prepared under retained reference");
    unsigned prepared_allocations = allocations;
    sdk_sample_cache_prepare_keys();
    require(allocations == prepared_allocations && acquired == released,
            "completed assignment analyzed twice or leaked reference");
    require(sdk_sample_cache_tag_priority(key, 256) == 256 &&
            sdk_sample_cache_accept_key(key, 256) == 256 &&
            sdk_sample_cache_tag_priority(key + 1, 99) == 99,
            "unknown/unowned priority changed");

    // The native GUI wrapper supplies null/zero while the sample browser is
    // open. Stock auditions mutate this very region table before confirmation,
    // so merely recognizing an owned region must not request more analysis.
    require(sdk_sample_cache_snapshot_keys(123, 123, nullptr, 0) != 123 &&
            sdk_sample_cache_take_force_send(), "browser opening kept old analysis intent");
    const unsigned browsing_acquired = acquired;
    const unsigned browsing_generation = progress.generation;
    for (unsigned highlight = 0; highlight < 64; ++highlight) {
        const std::string name = "samples/audition" + std::to_string(highlight) + ".wav";
        std::strcpy(reinterpret_cast<char*>(track.data() + 0x396c), name.c_str());
        const unsigned preview_key = sdk_sample_cache_stock_hash(name.c_str());
        samples[preview_key] = &second; // Stock PCM preview is already decoded.
        require(sdk_sample_cache_snapshot_keys(123, 123, nullptr, 0) == 123 &&
                !sdk_sample_cache_take_force_send(), "browser highlight forced ownership resend");
        sdk_sample_cache_reset_keys();
        const unsigned priority = sdk_sample_cache_tag_priority(preview_key, 0);
        require(priority == 0 && sdk_sample_cache_accept_key(preview_key, priority) == 0,
                "transient browser region was marked for analysis");
        sdk_sample_cache_prepare_keys();
        require(allocations == prepared_allocations && acquired == browsing_acquired &&
                progress.generation == browsing_generation && !sdk_sample_cache_find(&second, 35),
                "browser highlight allocated, retained or announced analysis");
        require(sdk_sample_cache_find(&first, 35) != nullptr,
                "browser opening discarded a completed analysis");
        samples.erase(preview_key);
    }
    std::strcpy(reinterpret_cast<char*>(track.data() + 0x396c), path);
    require(sdk_sample_cache_snapshot_keys(123, 123, pending, 2) != 123 &&
            sdk_sample_cache_take_force_send(), "same-path browser exit did not restore intent");
    sdk_sample_cache_reset_keys();
    sdk_sample_cache_accept_key(key, sdk_sample_cache_tag_priority(key, 0));
    sdk_sample_cache_prepare_keys();
    require(allocations == prepared_allocations && acquired == released,
            "browser exit recomputed completed analysis or leaked reference");

    // Reopen, then evict/redecode stock PCM while browsing. On close the file
    // is a stock cache hit without analysis; it must prepare once, not remain
    // silently unprepared because both stock path checksums stayed identical.
    sdk_sample_cache_snapshot_keys(123, 123, nullptr, 0);
    require(sdk_sample_cache_take_force_send(), "browser reopening did not clear intent");
    sdk_sample_cache_reset_keys();
    sdk_sample_cache_release(&first);
    sdk_sample_cache_prepare_keys();
    require(!sdk_sample_cache_find(&first, 35) && allocations == prepared_allocations,
            "browser reopened preview eagerly prepared analysis");
    require(sdk_sample_cache_snapshot_keys(123, 123, pending, 2) != 123 &&
            sdk_sample_cache_take_force_send(), "cache-hit browser exit did not force refresh");
    sdk_sample_cache_accept_key(key, sdk_sample_cache_tag_priority(key, 0));
    sdk_sample_cache_prepare_keys();
    require(sdk_sample_cache_find(&first, 35) && allocations == prepared_allocations + 1 &&
            acquired == released, "confirmed cache hit did not prepare exactly once");
    prepared_allocations = allocations;
    sdk_sample_cache_prepare_keys();
    require(allocations == prepared_allocations, "confirmed sample prepared repeatedly");
    sdk_sample_cache_release(&first);

    // Removal/engine switch while the same file remains the browser preview
    // also forces refresh and drops its request, even with identical path sum.
    std::memcpy(track.data() + 24, &sampler_id, 4);
    require(sdk_sample_cache_snapshot_keys(123, 123, pending, 2) != 123 &&
            sdk_sample_cache_take_force_send(), "unassignment kept stale ownership");
    sdk_sample_cache_reset_keys();
    require(sdk_sample_cache_tag_priority(key, 99) == 99, "removed assignment remains marked");
    const unsigned old_acquired = acquired;
    sdk_sample_cache_prepare_keys();
    require(acquired == old_acquired && !sdk_sample_cache_find(&first, 35),
            "unassigned preview restarted analysis");

    // A maximum-size candidate list is bounded. No NUL within the stock path
    // slot, empty path, null track, and invalid list counts never reach hash.
    std::memcpy(track.data() + 24, &custom_id, 4);
    std::memset(track.data() + 0x396c, 'x', 96);
    const unsigned old_hashes = hashes;
    PendingTrack invalid[] = {{track.data(), 0}, {nullptr, 0}};
    sdk_sample_cache_snapshot_keys(123, 123, invalid, 2);
    sdk_sample_cache_snapshot_keys(123, 123, invalid, 129);
    sdk_sample_cache_snapshot_keys(123, 123, nullptr, 128);
    require(hashes == old_hashes, "invalid path/list reached hash");

    std::vector<std::vector<unsigned char>> maximum_tracks(128,
        std::vector<unsigned char>(0x456c));
    std::vector<PendingTrack> maximum_pending(128);
    for (unsigned t = 0; t < 128; ++t) {
        auto& bytes = maximum_tracks[t];
        std::memcpy(bytes.data() + 24, &custom_id, 4);
        maximum_pending[t] = PendingTrack{bytes.data(), 0};
        for (unsigned r = 0; r < 24; ++r) {
            const std::string name = "samples/t" + std::to_string(t) + "r" + std::to_string(r) + ".wav";
            std::strcpy(reinterpret_cast<char*>(bytes.data() + 0x396c + r * 128), name.c_str());
        }
    }
    const unsigned maximum_hashes = hashes;
    sdk_sample_cache_snapshot_keys(123, 123, maximum_pending.data(), 128);
    require(assigned_count == 3072 && hashes - maximum_hashes == 3072,
            "maximum assignment snapshot truncated or rescanned paths");
    const unsigned last_key = sdk_sample_cache_stock_hash("samples/t127r23.wav");
    require(sdk_sample_cache_tag_priority(last_key, 16) == 144,
            "last assignment fell outside bounded snapshot");
    sdk_sample_cache_take_force_send();
    std::reverse(maximum_pending.begin(), maximum_pending.end());
    require(sdk_sample_cache_snapshot_keys(123, 123, maximum_pending.data(), 128) == 123 &&
            !sdk_sample_cache_take_force_send(), "assignment order alone forced IPC resend");
    for (unsigned i = 0; i < assigned_count; ++i) {
        require(!i || assigned_keys[i - 1] < assigned_keys[i],
                "maximum snapshot was not sorted and unique");
        require(sdk_sample_cache_tag_priority(assigned_keys[i], 5) == 133,
                "binary lookup missed an assigned key");
    }
    // A repeated track can appear in multiple pending patterns. Duplicates
    // must not exhaust capacity or cause an ownership change on reordering.
    for (auto& item : maximum_pending) item.track = maximum_tracks[0].data();
    sdk_sample_cache_snapshot_keys(123, 123, maximum_pending.data(), 128);
    require(assigned_count == 24, "duplicate assignment keys survived compaction");
    sdk_sample_cache_take_force_send();
    require(sdk_sample_cache_snapshot_keys(123, 123, maximum_pending.data(), 1) == 123 &&
            !sdk_sample_cache_take_force_send(), "duplicate multiplicity forced IPC resend");

    // Compare the bounded in-place sorter to a separate library oracle across
    // empty, odd, even, reverse and heavily duplicated unsigned-key sequences.
    unsigned random = 0x14351u;
    for (unsigned count = 0; count <= 257; ++count) {
        std::vector<unsigned> input(count), expected;
        for (unsigned i = 0; i < count; ++i) {
            random = random * 1664525u + 1013904223u;
            input[i] = count % 3 == 0 ? random % 17 : count % 3 == 1 ? random : count - i;
        }
        expected = input;
        std::sort(expected.begin(), expected.end());
        expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
        const unsigned unique = sort_unique(input.data(), count);
        require(unique == expected.size() &&
                std::equal(expected.begin(), expected.end(), input.begin()),
                "bounded assignment sort disagrees with independent oracle");
    }
    sdk_sample_cache_reset_keys();
    for (unsigned i = 0; i < 257; ++i)
        require(sdk_sample_cache_accept_key(i, 128) == 0, "full wanted list corrupted priority");
    require(wanted_count == 256, "wanted metadata capacity overrun");
    samples.clear();
    sdk_sample_cache_prepare_keys(); // Missing stock cache entries never allocate.
    require(allocations == prepared_allocations && acquired == released,
            "missing cache entry allocated or released a foreign reference");
    sdk_sample_cache_reset_keys();
    sdk_sample_gui_tick();
    require(overlay_calls && blocks.empty() && used_bytes(35) == 0, "GUI delivery or final ownership failure");
    std::cout << "cache lifecycle: " << checks << " checks, " << allocations << " allocations, " << frees << " frees\n";
}
