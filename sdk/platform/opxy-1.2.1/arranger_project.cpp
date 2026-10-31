// Matched OS 1.2.1 only. GUI owns edits; stock file workflow owns disk I/O.
// .xy remains musical source of truth; .xy.oxyl binds to exact saved bytes.
#include "arranger_project.hpp"
#include "arranger_state.hpp"
#include "stock.hpp"
#include "barrier.hpp"
#include "arranger_lock.hpp"
#include <opxy/arranger_storage.hpp>

extern "C" void sdk_arranger_project_mode_changed(bool) __asm__("sdk_arranger_project_mode_changed");
extern "C" void sdk_arranger_project_restored(bool, opxy::u32) __asm__("sdk_arranger_project_restored");
namespace {
using namespace opxy;
using namespace opxy::arranger;
using namespace opxy::arranger::storage;
using platform::stock;
using platform::barrier;
using platform::word;
const u32 profile = 0x00010201, units = 1;
const u32 path_capacity = 256, project_limit = 16 * 1024 * 1024;
State published, captured, staged, decode_scratch;
const State empty_state;
alignas(4) volatile u32 writer_intent, reader_intent, revision, retry, stage_ready, stage_cancelled, load_busy;
u32 published_revision, published_generation, gui_generation, captured_revision, captured_generation, staged_generation, staged_revision;
bool gui_bound, gui_active, publish_pending, published_bound, staged_recovered, staged_failed, captured_staged;
alignas(4) volatile u32 protected_generation, protection_active, io_status, rename_failed;
u32 observed_retry, staged_pointer, gui_pointer;
u8 gui_track;
Binding staged_binding;
// Stock uses one global serialization scratch. Nested/conflicting operations may
// still execute stock, but invalidate our capture instead of reusing it.
alignas(4) volatile u8 operation_busy;
alignas(4) volatile u32 operation_conflict;
u8 buffer[max_bytes + 1], chunk[4096];
char sidecar[path_capacity], temporary[path_capacity];
char rename_from[path_capacity], rename_to[path_capacity];

u32 generation() { return stock<u32()>(0x8d584224)(); }
void dirty() { stock<void()>(0x8d585ee8)(); }
void release_reader() { barrier(); reader_intent = 0; }
void release_writer() { barrier(); writer_intent = 0; }

// Try-once two-role exclusion. Both may decline under contention; neither waits.
// Only GUI writes published; the serialized file workflow is its sole reader.
bool publish_snapshot() {
    writer_intent = 1; barrier();
    if (reader_intent) { release_writer(); return false; }
    published.model = sdk_arranger_model();
    published.enabled = gui_active; published.track = gui_track;
    published_bound = gui_bound;
    published_generation = gui_generation; published_revision = revision;
    release_writer();
    return true;
}
bool capture_current() {
    if (captured_generation != generation()) return false;
    if (captured_revision == revision) return true;
    // Applying this same validated load advances revision once, without an edit.
    // Any subsequent GUI edit advances it again and invalidates the disk capture.
    return captured_staged && revision == captured_revision + 1 && gui_bound &&
        gui_generation == captured_generation && gui_pointer == word(0x840a0524);
}
bool capture_snapshot() {
    captured_staged = false;
    reader_intent = 1; barrier();
    if (writer_intent) { release_reader(); return false; }
    captured = published;
    captured_generation = published_generation; captured_revision = published_revision;
    const bool bound = published_bound;
    release_reader();
    if (bound && captured_generation == generation() && captured_revision == revision) return true;
    // Native598b04 commits a load, then598ae8 immediately saves workspace before
    // GUI poll can publish it. The file-operation lock keeps staged immutable;
    // use only the validated candidate for this exact committed buffer/generation.
    barrier();
    if (!stage_ready || stage_cancelled || staged_generation != generation() ||
        staged_pointer != word(0x840a0524)) return false;
    captured_staged = true;
    captured_revision = staged_revision;
    captured_generation = staged_generation;
    captured = staged;
    barrier();
    return !stage_cancelled && capture_current();
}

// OS libc FILE begins with flags: fread's negative-read branches 8fed06ae /
// 8fed1f1a set bit9; EOF branches 8fed06be / 8fed1f28 set bit8.
typedef void File;
File* open(const char* path, const char* mode) { return stock<File*(const char*, const char*)>(0x8fed0348)(path, mode); }
u32 read(void* data, u32 size, File* file) { return stock<u32(void*, u32, u32, File*)>(0x8fed05bc)(data, 1, size, file); }
int close(File* file) { return stock<int(File*)>(0x8fed00ae)(file); }
bool eof(File* file) { return (*static_cast<const u16*>(file) & 0x300u) == 0x100u; }
bool paths(const char* path) {
    if (!path) return false;
    u32 n = 0;
    while (n < path_capacity && path[n]) ++n;
    if (n < 4 || n + 10 >= path_capacity || path[n-3] != '.' || path[n-2] != 'x' || path[n-1] != 'y') return false;
    for (u32 i = 0; i < n; ++i) sidecar[i] = temporary[i] = path[i];
    const char suffix[] = ".oxyl.tmp";
    for (u32 i = 0; i < sizeof suffix; ++i) temporary[n+i] = suffix[i];
    for (u32 i = 0; i < 5; ++i) sidecar[n+i] = suffix[i];
    sidecar[n+5] = 0;
    return true;
}
bool measure(const char* path, Binding& binding) {
    File* file = open(path, "rb");
    if (!file) return false;
    binding = Binding{profile, units, 0, 0};
    bool valid = true;
    for (;;) {
        const u32 n = read(chunk, sizeof chunk, file);
        if (n > sizeof chunk || n > project_limit - binding.project_bytes) { valid = false; break; }
        binding.project_bytes += n;
        binding.project_crc32 = crc32(binding.project_crc32, chunk, n);
        if (n != sizeof chunk) { valid = eof(file); break; }
    }
    const int result = close(file);
    return valid && result == 0 && binding.project_bytes != 0;
}
bool restore_sidecar(const char* path, const Binding& binding) {
    File* file = open(path, "rb");
    // No verified errno API: null may mean absent or denied. Preserve stock-project
    // compatibility; only an opened but invalid companion can prove failed recovery.
    if (!file) return false;
    staged_failed = true; // An existing unreadable/invalid companion must survive recovery.
    const u32 n = read(buffer, sizeof buffer, file);
    const bool ended = eof(file);
    const int result = close(file);
    return ended && result == 0 && n <= max_bytes && decode(buffer, n, binding, staged, decode_scratch);
}
bool restore_file(const char* path) {
    staged_recovered = false; staged_failed = false;
    if (!paths(path) || !measure(path, staged_binding)) { staged_failed = true; return false; }
    if (restore_sidecar(sidecar, staged_binding)) return true;
    // A complete temp can survive interruption after the project save but before rename.
    staged_recovered = restore_sidecar(temporary, staged_binding);
    return staged_recovered;
}
bool write_file(const char* path) {
    if (!paths(path)) return false;
    Binding binding;
    if (!measure(path, binding)) return false;
    const u32 n = encode(captured, binding, buffer, max_bytes);
    if (!n) return false;
    File* file = open(temporary, "wb");
    if (!file) return false;
    const u32 written = stock<u32(const void*, u32, u32, File*)>(0x8fed0780)(buffer, 1, n, file);
    const int result = close(file);
    if (written != n || result != 0 || operation_conflict ||
        !capture_current()) return false;
    Binding after;
    if (!measure(path, after) || after.project_bytes != binding.project_bytes ||
        after.project_crc32 != binding.project_crc32 || operation_conflict ||
        !capture_current()) return false;
    // Same checked close -> rename sequence as stock RLE writer 8d59155a..59e.
    // Preserves previous sidecar on short write/close failure; not an atomic .xy pair.
    return stock<int(const char*, const char*)>(0x8fed0908)(temporary, sidecar) == 0;
}
void apply(const State& state, u32 current) {
    if (!gui_bound || gui_generation != current) io_status = 0;
    sdk_arranger_model() = state.model;
    gui_active = state.enabled; gui_track = state.track;
    gui_generation = current; gui_pointer = word(0x840a0524); gui_bound = true;
    sdk_arranger_bind_project(current);
    ++revision;
    publish_pending = true;
    sdk_arranger_project_restored(gui_active, gui_track);
    sdk_arranger_publish(gui_active, true);
}
}

bool sdk_arranger_project_changed(bool active, u32 track) {
    if (!sdk_arranger_project_edit_ready()) { sdk_arranger_project_poll(); return false; }
    gui_active = active; gui_track = static_cast<u8>(track < 8 ? track : 0);
    ++revision;
    publish_pending = true;
    dirty();
    if (publish_snapshot()) publish_pending = false;
    return true;
}

void sdk_arranger_project_poll() {
    const u32 current = generation();
    barrier();
    if (stage_ready && stage_cancelled) {
        stage_cancelled = 0; barrier(); stage_ready = 0;
    }
    if (stage_ready && staged_generation == current && staged_pointer == word(0x840a0524)) {
        apply(staged, current);
        barrier(); stage_ready = 0;
    } else if ((!gui_bound || gui_generation != current) && !load_busy) {
        apply(empty_state, current);
        // An older abandoned stage cannot attach to a reused project pointer.
        if (stage_ready && staged_generation != current + 1) { barrier(); stage_ready = 0; }
    }
    if (publish_pending && publish_snapshot()) publish_pending = false;
    const u32 requested_retry = retry;
    if (requested_retry != observed_retry && gui_bound && gui_generation == current) {
        observed_retry = requested_retry; dirty();
    }
}

u32 sdk_arranger_project_status() {
    barrier();
    if (protection_active && protected_generation == generation()) return rename_failed ? 3 : 1;
    return io_status;
}

u32 sdk_arranger_project_save(const char* path) {
    barrier();
    // Reject before stock changes .xy: keeping only the old sidecar would still
    // destroy its exact-byte binding. Protection belongs to this project generation.
    if (protection_active && protected_generation == generation()) return 0;
    if (!platform::arranger_try_lock(&operation_busy)) {
        operation_conflict = 1; ++retry;
        return stock<u32(const char*)>(0x8d585e98)(path);
    }
    operation_conflict = 0;
    const bool captured_ok = capture_snapshot();
    // 8d585e98 forwards serializer R0 unchanged after its call at 8d585eda.
    const u32 result = stock<u32(const char*)>(0x8d585e98)(path);
    if (!result || !captured_ok || operation_conflict || !capture_current() || !write_file(path)) { ++retry; io_status = 2; }
    else io_status = 0;
    barrier(); operation_busy = 0;
    return result;
}

u32 sdk_arranger_project_load(const char* path, u32 commit) {
    // Stock rejects an already pending load; leave its staged sidecar intact.
    if (word(0x840a0528))
        return stock<u32(const char*, u32)>(0x8d5890b0)(path, commit);
    if (stage_ready) {
        // A new accepted load must never inherit an abandoned/older candidate.
        // Keep its memory immutable until GUI acknowledges; reset the new project.
        const u32 before = generation();
        const u32 result = stock<u32(const char*, u32)>(0x8d5890b0)(path, commit);
        if (result) {
            protected_generation = before + 1; protection_active = 1;
            stage_cancelled = 1; barrier();
        }
        return result;
    }
    if (!platform::arranger_try_lock(&operation_busy)) {
        operation_conflict = 1; ++retry;
        const u32 before = generation();
        const u32 result = stock<u32(const char*, u32)>(0x8d5890b0)(path, commit);
        if (result) { protected_generation = before + 1; protection_active = 1; barrier(); }
        return result;
    }
    operation_conflict = 0; load_busy = 1;
    const u32 before = generation();
    const bool restored = restore_file(path);
    const u32 result = stock<u32(const char*, u32)>(0x8d5890b0)(path, commit);
    if (result) {
        Binding after;
        const bool same_file = restored && measure(path, after) &&
            after.project_bytes == staged_binding.project_bytes &&
            after.project_crc32 == staged_binding.project_crc32;
        if (!same_file || operation_conflict) staged = empty_state;
        else if (staged_recovered) ++retry;
        staged_generation = before + 1;
        staged_revision = revision;
        protected_generation = staged_generation;
        protection_active = (!same_file && staged_failed) || operation_conflict;
        rename_failed = 0;
        io_status = 0;
        staged_pointer = commit ? word(0x840a0524) : word(0x840a0528);
        stage_cancelled = 0; barrier(); stage_ready = 1;
    }
    barrier(); load_busy = 0; operation_busy = 0;
    return result;
}

// Native named-project worker saves the old name, then renames only .xy at
// 572966. The boot workspace does not use this path. Keep exact-byte companions
// alongside the renamed project; never reconstruct metadata from the GUI here.
u32 sdk_arranger_project_rename(const char* from, const char* to) {
    if (!platform::arranger_try_lock(&operation_busy)) { io_status = 2; return 0; }
    u32 from_size = 0, to_size = 0;
    if (from) while (from_size < path_capacity && from[from_size]) ++from_size;
    if (to) while (to_size < path_capacity && to[to_size]) ++to_size;
    const bool valid = from_size >= 3 && to_size >= 3 &&
        from_size + 10 < path_capacity && to_size + 10 < path_capacity &&
        from[from_size-3] == '.' && from[from_size-2] == 'x' && from[from_size-1] == 'y' &&
        to[to_size-3] == '.' && to[to_size-2] == 'x' && to[to_size-1] == 'y';
    const u32 result = stock<u32(const char*, const char*)>(0x8c040c78)(from, to);
    bool moved = valid;
    if (result && valid) {
        // .oxy stores SDK engine IDs; .xy.oxyl and its recovery temp store Linear.
        const char* suffixes[] = {".xy.oxyl", ".xy.oxyl.tmp", ".oxy"};
        for (u32 kind = 0; kind < 3; ++kind) {
            for (u32 i = 0; i < from_size - 3; ++i) rename_from[i] = from[i];
            for (u32 i = 0; i < to_size - 3; ++i) rename_to[i] = to[i];
            u32 i = 0;
            do { rename_from[from_size-3+i] = rename_to[to_size-3+i] = suffixes[kind][i]; }
            while (suffixes[kind][i++]);
            File* file = open(rename_from, "rb");
            if (!file) continue; // Same documented absent/open-denied limitation as load.
            if (close(file) != 0 || stock<int(const char*, const char*)>(0x8fed0908)(rename_from, rename_to) != 0)
                moved = false;
        }
    }
    if (result && !moved) {
        protected_generation = generation(); protection_active = 1; rename_failed = 1;
        io_status = 2;
    } else if (!result) io_status = 2;
    barrier(); operation_busy = 0;
    return result;
}

u32 sdk_arranger_mtp_match(const char* pattern, const void* path) {
    if (!path) return 0;
    const u32* native = static_cast<const u32*>(path);
    const u32 length = native[5];
    if (length < 8 || length >= path_capacity)
        return stock<u32(const char*, const void*)>(0x8d5c1bb0)(pattern, path);
    const char* text = native[6] >= 16 ? platform::at<const char>(native[1]) :
        reinterpret_cast<const char*>(native + 1);
    const char suffix[] = ".xy.oxyl";
    for (u32 i = 0; i < 8; ++i)
        if (text[length - 8 + i] != suffix[i])
            return stock<u32(const char*, const void*)>(0x8d5c1bb0)(pattern, path);
    char project[path_capacity];
    for (u32 i = 0; i < length - 5; ++i) project[i] = text[i];
    project[length - 5] = 0;
    u32 matched = 0;
    // Stock's tiny regex has no groups or alternation; reuse its exact project path rule.
    const i32 result = stock<i32(const char*, const char*, u32*)>(0x8c0740c8)(pattern, project, &matched);
    return result == 0 && matched == length - 5;
}

// Project > General retains its stock two settings and stock list renderer.
// Only the second push at 8c0972e2 is redirected here; stock owns the vector.
void sdk_arranger_general_rows(void* rows, const void* row) {
    stock<void(void*, const void*)>(0x8c087770)(rows, row);
    sdk_arranger_project_poll();
    u32 extra[14] = {}; // Two native 28-byte strings, constructor/destructor paired.
    stock<void(void*, const char*)>(0x8c041788)(extra, "arranger mode");
    stock<void(void*, const char*)>(0x8c041788)(extra + 7, sdk_arranger_project_linear() ? "Linear" : "Song");
    stock<void(void*, const void*)>(0x8c087770)(rows, extra);
    stock<void(void*)>(0x8c086d28)(extra);
}

// General's virtual encoder method: object+0x90 is selected row, event+4 delta.
// Raw encoder index 1 selects; indices 2/3 edit, exactly as stock General.
void sdk_arranger_general_encoder(void* object, u32 encoder, const void* event) {
    if (!object || !event) return;
    u32& selected = static_cast<u32*>(object)[0x90 / 4];
    const i32 delta = static_cast<const i32*>(event)[1];
    if (encoder == 1) {
        const i32 step = delta < -2 ? -2 : delta > 2 ? 2 : delta;
        const i32 next = static_cast<i32>(selected < 3 ? selected : 2) + step;
        selected = static_cast<u32>(next < 0 ? 0 : next > 2 ? 2 : next);
    } else if ((encoder == 2 || encoder == 3) && selected == 2) {
        if (delta) sdk_arranger_project_set_linear(delta > 0);
    } else {
        stock<void(void*, u32, const void*)>(0x8c0970d4)(object, encoder, event);
    }
}

bool sdk_arranger_project_edit_ready() {
    barrier();
    return gui_bound && !load_busy && !stage_ready && !word(0x840a0528) &&
        gui_generation == generation() && gui_pointer == word(0x840a0524);
}

bool sdk_arranger_project_linear() {
    return gui_bound && gui_generation == generation() && gui_active;
}

bool sdk_arranger_project_set_linear(bool linear) {
    sdk_arranger_project_poll();
    if (!sdk_arranger_project_edit_ready()) return false;
    if (gui_active == linear) return true;
    if (!sdk_arranger_project_changed(linear, gui_track)) return false;
    sdk_arranger_project_mode_changed(linear);
    sdk_arranger_publish(linear, false);
    return true;
}
