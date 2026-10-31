#include <map>
#include <string>
#include <vector>
#include <regex>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "stock.hpp"
#include "arranger_state.hpp"
#include "arranger_project.hpp"
void host_barrier();
void* fake_address(opxy::u32);
const char* fake_text(opxy::u32);
namespace opxy { namespace platform {
template<class F> F* fake_stock(u32 address) { return reinterpret_cast<F*>(fake_address(address)); }
template<class T> T* fake_at(Address address) { return reinterpret_cast<T*>(fake_text(static_cast<u32>(address))); }
u32 fake_word(u32 address);
}}
#define OPXY_ARRANGER_LOCK_HPP
namespace opxy { namespace platform {
inline bool arranger_try_lock(volatile u8* flag) {
    const bool acquired = *flag == 0; *flag |= 0x80; host_barrier(); return acquired;
}
}}
#define stock fake_stock
#define word fake_word
#define at fake_at
#include <arranger_project.cpp>
#undef stock
#undef word
#undef at

using opxy::u32;
using opxy::u8;
using opxy::u16;
namespace test {
std::map<std::string, std::vector<u8> > files;
struct File { u16 flags; std::string path; u32 position; };
u32 generation = 1, pointer = 0x1000, pending = 0, dirty_count = 0, restored_count = 0;
u32 bound = 0, selected = 0;
u32 encoder_calls = 0, row_destructors = 0;
u8 save_payload = 77;
bool fail_companion_rename = false;
bool enabled = false, fail_read = false, fail_write = false, fail_close = false, fail_rename = false, fail_load = false;
opxy::arranger::Model model;
void (*during_save)() = nullptr;
void (*during_temp_close)() = nullptr;
void (*barrier_hook)() = nullptr;
bool in_barrier = false;
void require(bool value, const char* label) { if (!value) { std::puts(label); std::exit(1); } }
u32 get_generation() { return generation; }
void mark_dirty() { ++dirty_count; }
void string_construct(void* destination, const char* text) {
    require(std::strlen(text) < 28, "bounded test string");
    std::memset(destination, 0, 28); std::strcpy(static_cast<char*>(destination), text);
}
void row_push(void* rows, const void* row) {
    const u8* bytes = static_cast<const u8*>(row);
    static_cast<std::vector<std::vector<u8> >*>(rows)->push_back(std::vector<u8>(bytes, bytes+56));
}
void row_destroy(void*) { ++row_destructors; }
void general_encoder(void*, u32, const void*) { ++encoder_calls; }
void* open(const char* path, const char* mode) {
    if (*mode == 'r' && !files.count(path)) return nullptr;
    if (*mode == 'w') files[path].clear();
    return new File{0, path, 0};
}
u32 read(void* out, u32 size, u32 count, void* raw) {
    File& file = *static_cast<File*>(raw);
    if (fail_read) { file.flags |= 0x200; return 0; }
    const std::vector<u8>& content = files[file.path];
    u32 n = size * count;
    if (n > content.size() - file.position) { n = content.size() - file.position; file.flags |= 0x100; }
    if (n) std::memcpy(out, content.data() + file.position, n);
    file.position += n; return n / size;
}
u32 write(const void* data, u32 size, u32 count, void* raw) {
    File& file = *static_cast<File*>(raw);
    const u8* bytes = static_cast<const u8*>(data);
    const u32 n = fail_write ? size * count / 2 : size * count;
    files[file.path].assign(bytes, bytes + n); return n / size;
}
int close(void* raw) {
    File* file = static_cast<File*>(raw);
    if (during_temp_close && file->path == "/song.xy.oxyl.tmp") during_temp_close();
    delete file; return fail_close ? -1 : 0;
}
int rename(const char* from, const char* to) {
    if (fail_rename || (fail_companion_rename && std::string(from).find(".oxyl") != std::string::npos)) return -1;
    if (!files.count(from)) return -1;
    files[to] = files[from]; files.erase(from); return 0;
}
u32 native_rename(const char* from, const char* to) { return rename(from,to) == 0; }
u32 save(const char* path) {
    files[path] = std::vector<u8>(100, save_payload);
    if (during_save) during_save();
    return 1;
}
u32 load(const char*, u32 commit) {
    if (pending || fail_load) return 0;
    pending = pointer == 0x1000 ? 0x2000 : 0x1000;
    if (commit) { pointer = pending; pending = 0; ++generation; }
    return 1;
}
void nested_save() { during_save = nullptr; sdk_arranger_project_save("/song.xy"); }
void change_during_save() { model.seek(222); sdk_arranger_project_changed(true, 3); }
void apply_then_edit() { sdk_arranger_project_poll(); change_during_save(); }
void read_during_publish() { require(!capture_snapshot(), "reader declined writer intent"); }
void write_during_capture() { require(!publish_snapshot(), "writer declined reader intent"); }
void replace_project_during_write() { files["/song.xy"][0] ^= 1; }
std::string mtp_path;
u32 mtp_stock_calls;
opxy::i32 regex_match(const char* pattern, const char* text, u32* matched) {
    std::cmatch match;
    if (!std::regex_search(text, match, std::regex(pattern))) { *matched = 0; return -1; }
    *matched = static_cast<u32>(match.length()); return static_cast<opxy::i32>(match.position());
}
u32 mtp_stock(const char* pattern, const void*) {
    ++mtp_stock_calls;
    u32 matched = 0;
    return regex_match(pattern, mtp_path.c_str(), &matched) == 0 && matched == mtp_path.size();
}
bool mtp_allowed(const char* path) {
    mtp_path = path;
    u32 native[7] = {}; native[1] = 0x1234;
    native[5] = static_cast<u32>(mtp_path.size()); native[6] = native[5] + 16;
    const char* patterns[] = {
        R"(^/projects/workspace\.o?xy$)",
        R"(^/projects/[\w\!\+\-\@\[\] #-)]+/[\w\d\!\+\-\@\[\] #-)]+\.o?xy$)",
        R"(^/projects/[\w\!\+\-\@\[\] #-)]+/[\w\d\!\+\-\@\[\] #-)]+/[\w\d\!\+\-\@\[\] #-)]+\.o?xy$)"
    };
    for (const char* pattern : patterns) if (sdk_arranger_mtp_match(pattern, native)) return true;
    return false;
}
}
const char* fake_text(opxy::u32 address) {
    test::require(address == 0x1234, "native MTP string address"); return test::mtp_path.c_str();
}
void host_barrier() {
    if (test::barrier_hook && !test::in_barrier) {
        test::in_barrier = true; test::barrier_hook(); test::in_barrier = false;
    }
}
opxy::u32 opxy::platform::fake_word(u32 address) {
    if (address == 0x840a0524) return test::pointer;
    if (address == 0x840a0528) return test::pending;
    test::require(false, "unexpected stock word"); return 0;
}
void* fake_address(u32 address) {
    switch(address) {
    case 0x8c041788: return reinterpret_cast<void*>(test::string_construct);
    case 0x8c087770: return reinterpret_cast<void*>(test::row_push);
    case 0x8c086d28: return reinterpret_cast<void*>(test::row_destroy);
    case 0x8c0970d4: return reinterpret_cast<void*>(test::general_encoder);
    case 0x8d584224: return reinterpret_cast<void*>(test::get_generation);
    case 0x8d585ee8: return reinterpret_cast<void*>(test::mark_dirty);
    case 0x8fed0348: return reinterpret_cast<void*>(test::open);
    case 0x8fed05bc: return reinterpret_cast<void*>(test::read);
    case 0x8fed0780: return reinterpret_cast<void*>(test::write);
    case 0x8fed00ae: return reinterpret_cast<void*>(test::close);
    case 0x8c040c78: return reinterpret_cast<void*>(test::native_rename);
    case 0x8fed0908: return reinterpret_cast<void*>(test::rename);
    case 0x8d585e98: return reinterpret_cast<void*>(test::save);
    case 0x8d5890b0: return reinterpret_cast<void*>(test::load);
    case 0x8d5c1bb0: return reinterpret_cast<void*>(test::mtp_stock);
    case 0x8c0740c8: return reinterpret_cast<void*>(test::regex_match);
    }
    test::require(false, "unexpected stock call"); return nullptr;
}
opxy::arranger::Model& sdk_arranger_model() { return test::model; }
void sdk_arranger_bind_project(u32 value) { test::bound = value; }
void sdk_arranger_publish(bool, bool) {}
extern "C" void sdk_arranger_project_mode_changed(bool linear) { test::enabled = linear; }
extern "C" void sdk_arranger_project_restored(bool active, u32 track) {
    test::enabled = active; test::selected = track; ++test::restored_count;
}

int main() {
    using test::require;
    sdk_arranger_project_poll();
    require(test::bound == 1 && !test::enabled, "initial project bound disabled");
    const opxy::arranger::Clip clip = {2, 4, 100, 400, 12, 480};
    require(test::model.insertAt(19, clip), "insert stable slot");
    test::model.seek(150);
    sdk_arranger_project_changed(true, 2);
    require(test::dirty_count == 1, "mode/model edits mark dirty");
    test::barrier_hook = test::read_during_publish;
    require(publish_snapshot(), "GUI publication"); test::barrier_hook = nullptr;
    test::barrier_hook = test::write_during_capture;
    require(capture_snapshot(), "worker capture"); test::barrier_hook = nullptr;
    require(sdk_arranger_project_save("/song.xy") == 1, "stock save result");
    require(test::files.count("/song.xy.oxyl") && !test::files.count("/song.xy.oxyl.tmp"), "completed sidecar rename");
    const std::vector<u8> original = test::files["/song.xy.oxyl"];
    test::model.seek(200); sdk_arranger_project_changed(false, 1);
    test::fail_write = true; sdk_arranger_project_save("/song.xy"); test::fail_write = false;
    require(test::files["/song.xy.oxyl"] == original, "partial write preserves sidecar");
    u32 count = test::dirty_count; sdk_arranger_project_poll();
    require(test::dirty_count == count + 1, "write failure retries GUI dirty");
    sdk_arranger_project_poll(); require(test::dirty_count == count + 1, "retry acknowledged once");
    test::fail_close = true; sdk_arranger_project_save("/song.xy"); test::fail_close = false;
    require(test::files["/song.xy.oxyl"] == original, "failed close preserves sidecar");
    test::fail_rename = true; sdk_arranger_project_save("/song.xy"); test::fail_rename = false;
    require(test::files["/song.xy.oxyl"] == original, "failed rename preserves sidecar");
    test::during_save = test::change_during_save; sdk_arranger_project_save("/song.xy"); test::during_save = nullptr;
    require(test::files["/song.xy.oxyl"] == original, "concurrent revision never writes mixed snapshot");
    test::during_save = test::nested_save; sdk_arranger_project_save("/song.xy");
    require(test::files["/song.xy.oxyl"] == original, "nested same-path stock save cannot reuse metadata capture");
    test::fail_read = true; sdk_arranger_project_save("/song.xy"); test::fail_read = false;
    require(test::files["/song.xy.oxyl"] == original, "read error not mistaken for EOF");
    test::during_temp_close = test::replace_project_during_write;
    sdk_arranger_project_save("/song.xy"); test::during_temp_close = nullptr;
    require(test::files["/song.xy.oxyl"] == original, "project changed during sidecar write cannot commit mixed pair");
    test::files["/song.xy"][0] ^= 1;

    require(sdk_arranger_project_load("/song.xy", 0) == 1, "deferred load staged");
    const u32 before = test::restored_count;
    sdk_arranger_project_poll(); require(test::restored_count == before, "no restore before commit");
    require(sdk_arranger_project_load("/song.xy", 0) == 0, "pending load refused by stock");
    test::pointer = test::pending; test::pending = 0; ++test::generation;
    sdk_arranger_project_poll();
    require(test::enabled && test::selected == 2 && test::model.position() == 150 && test::model.clip(19), "staged restore bound to commit");
    require(test::bound == test::generation, "new audio publication generation");
    test::fail_load = true;
    require(sdk_arranger_project_load("/song.xy", 1) == 0, "failed stock load"); test::fail_load = false;
    sdk_arranger_project_poll(); require(test::model.clip(19), "failed stock load preserves current model");
    test::files["/song.xy"][0] ^= 1;
    require(sdk_arranger_project_load("/song.xy", 1) == 1, "stock load with stale metadata");
    sdk_arranger_project_poll(); require(!test::enabled && !test::model.clip(19), "stale binding resets model");
    test::model.insertAt(19, clip); sdk_arranger_project_changed(true, 0);
    ++test::generation; // Default/new project; same address is deliberately reused.
    sdk_arranger_project_poll(); require(!test::model.clip(19) && !test::enabled, "same-pointer new generation resets");
    test::files["/song.xy"] = std::vector<u8>(100, 77);
    require(sdk_arranger_project_load("/song.xy", 0) == 1, "candidate before abandoned pending load");
    test::pending = 0; // Model a cancelled/replaced stock pending operation.
    test::files["/other.xy"] = std::vector<u8>(100, 12);
    require(sdk_arranger_project_load("/other.xy", 1) == 1, "replacement stock load");
    sdk_arranger_project_poll();
    require(!test::enabled && !test::model.clip(19), "abandoned candidate cannot attach to next load same buffer/generation");
    const auto replacement_bytes = test::files["/other.xy"];
    require(sdk_arranger_project_status() == 1 && sdk_arranger_project_save("/other.xy") == 0,
        "unvalidated accepted replacement blocks save");
    require(test::files["/other.xy"] == replacement_bytes, "replacement project remains unchanged on disk");
    ++test::generation;
    require(!sdk_arranger_project_changed(true, 7), "post-edit generation mismatch rejected");
    require(!test::enabled, "rejected edit resets new project state");
    require(sdk_arranger_project_edit_ready(), "current project edit ready");
    test::model.insertAt(19, clip);
    require(test::model.setLoop(true, 100, 500), "mode test loop");
    require(sdk_arranger_project_set_linear(true) && sdk_arranger_project_linear(), "enable persisted Linear mode");
    require(test::enabled && test::model.clip(19) && test::model.loopEnabled(), "mode switch preserves arrangement");
    count = test::dirty_count;
    require(sdk_arranger_project_set_linear(true) && test::dirty_count == count, "unchanged mode remains clean");
    require(sdk_arranger_project_save("/workspace.xy") == 1, "save Linear workspace");
    require(sdk_arranger_project_set_linear(false), "switch to Song");
    require(!test::enabled && test::model.clip(19) && test::model.loopEnabled(), "Song retains arrangement");
    require(sdk_arranger_project_save("/song-mode.xy") == 1, "save independent Song project");
    gui_bound = false; test::model = opxy::arranger::Model(); // Simulated fresh GUI startup.
    require(sdk_arranger_project_load("/workspace.xy", 1) == 1, "workspace reload after restart");
    sdk_arranger_project_poll();
    require(sdk_arranger_project_linear() && test::enabled && test::model.clip(19) && test::model.loopEnabled(), "restart restores Linear and clips/loop");
    require(sdk_arranger_project_load("/song-mode.xy", 1) == 1, "load separate Song project");
    sdk_arranger_project_poll();
    require(!sdk_arranger_project_linear() && !test::enabled && test::model.clip(19), "project modes isolated without deleting clips");

    test::files["/projects/imported/copy.xy"] = test::files["/workspace.xy"];
    test::files["/projects/imported/copy.xy.oxyl"] = test::files["/workspace.xy.oxyl"];
    require(sdk_arranger_project_load("/projects/imported/copy.xy", 1) == 1, "load exported pair under another path");
    sdk_arranger_project_poll();
    require(test::enabled && test::model.clip(19) && test::model.loopEnabled(), "paired backup restores arrangement and mode");
    {
        using namespace opxy::arranger::storage;
        const std::vector<u8> current = test::files["/projects/imported/copy.xy.oxyl"];
        const u32 clips = read32(current.data() + 52), old_length = header_bytes + clips * 20;
        std::vector<u8> legacy(old_length);
        std::memcpy(legacy.data(), current.data(), header_bytes);
        for (u32 i = 0; i < clips; ++i)
            std::memcpy(legacy.data() + header_bytes + i * 20, current.data() + header_bytes + i * clip_bytes, 20);
        write32(legacy.data() + 8, 1); write32(legacy.data() + 12, old_length);
        write32(legacy.data() + 60, checksum(legacy.data(), old_length));
        test::files["/projects/imported/copy.xy.oxyl"] = legacy;
        require(sdk_arranger_project_load("/projects/imported/copy.xy", 1) == 1, "load previous firmware project pair");
        sdk_arranger_project_poll();
        require(test::enabled && test::model.clip(19) && !test::model.clip(19)->phase_remainder &&
            test::model.loopEnabled(), "legacy companion lifecycle retains arrangement and zero fractional phase");
    }
    test::files["/projects/imported/copy.xy.oxyl.tmp"] = test::files["/projects/imported/copy.xy.oxyl"];
    test::files["/projects/imported/copy.xy.oxyl"][60] ^= 1;
    count = test::dirty_count;
    require(sdk_arranger_project_load("/projects/imported/copy.xy", 1) == 1, "recover complete bound temp after interrupted rename");
    sdk_arranger_project_poll();
    require(test::enabled && test::model.clip(19) && test::dirty_count > count, "recovered sidecar restored and queued for ordinary resave");
    test::files["/projects/imported/copy.xy"][0] ^= 1;
    require(sdk_arranger_project_load("/projects/imported/copy.xy", 1) == 1, "load mismatched imported pair");
    sdk_arranger_project_poll();
    require(!test::enabled && !test::model.clip(19), "neither mismatched companion nor temp can attach");
    require(sdk_arranger_project_load("/song-mode.xy", 1) == 1, "restore Song for General test");
    sdk_arranger_project_poll();

    for (const char* name : {"/projects/workspace", "/projects/a b/song", "/projects/x/y 2/song"}) {
        for (const char* suffix : {".xy", ".oxy", ".xy.oxyl"})
            require(test::mtp_allowed((std::string(name) + suffix).c_str()), "MTP exposes project and both companions");
        for (const char* suffix : {".xy.oxyl.tmp", ".txt", ".xy.oxyl/child", ".oxy.oxyl"})
            require(!test::mtp_allowed((std::string(name) + suffix).c_str()), "MTP rejects unapproved companion suffix");
    }
    for (const char* path : {"/projects/random.xy.oxyl", "/projects/a/b/c/song.xy.oxyl",
        "/projects/../song.xy.oxyl", "/samples/user/song.xy.oxyl"})
        require(!test::mtp_allowed(path), "companion wrapper preserves stock project path restrictions");
    require(test::mtp_stock_calls != 0, "ordinary MTP names delegated to stock");

    std::vector<std::vector<u8> > rows(1, std::vector<u8>(56));
    u32 old_row[14] = {};
    test::string_construct(old_row, "scene length"); test::string_construct(old_row+7, "longest");
    sdk_arranger_general_rows(&rows, old_row);
    require(rows.size() == 3 && test::row_destructors == 1, "append exactly one native General row and destroy temporary");
    require(std::strcmp(reinterpret_cast<const char*>(rows[2].data()), "arranger mode") == 0 &&
        std::strcmp(reinterpret_cast<const char*>(rows[2].data()+28), "Song") == 0, "General displays persisted Song mode");
    u32 general[40] = {}; general[0x90/4] = 1;
    opxy::i32 event[4] = {0, 1, 0, 0};
    sdk_arranger_general_encoder(general, 1, event);
    require(general[0x90/4] == 2, "third row selectable using stock E1 convention");
    sdk_arranger_general_encoder(general, 2, event);
    require(sdk_arranger_project_linear(), "E2 switches General to Linear");
    event[1] = -1; sdk_arranger_general_encoder(general, 3, event);
    require(!sdk_arranger_project_linear(), "E3 switches General to Song");
    event[1] = 0x7fffffff; sdk_arranger_general_encoder(general, 1, event);
    require(general[0x90/4] == 2, "extreme positive selector clamps");
    event[1] = static_cast<opxy::i32>(0x80000000u); sdk_arranger_general_encoder(general, 1, event);
    require(general[0x90/4] == 0, "extreme negative selector clamps");
    sdk_arranger_general_encoder(general, 2, event);
    require(test::encoder_calls == 1, "existing General setting delegates unchanged");
    test::pending = 0x2000;
    require(!sdk_arranger_project_edit_ready() && !sdk_arranger_project_set_linear(true), "pending project blocks mode/source edits");
    test::pending = 0;
    test::files["/workspace.xy.oxyl"][8] ^= 1;
    require(sdk_arranger_project_load("/workspace.xy", 1) == 1, "stock loads with malformed sidecar");
    sdk_arranger_project_poll();
    require(!sdk_arranger_project_linear() && !test::model.clip(19), "malformed mode metadata fails closed to Song");
    require(sdk_arranger_project_status() == 1, "failed restore is visible and quarantined");
    const auto damaged_project = test::files["/workspace.xy"];
    const auto damaged_sidecar = test::files["/workspace.xy.oxyl"];
    test::save_payload = 99;
    require(sdk_arranger_project_save("/workspace.xy") == 0, "quarantine rejects before native project rewrite");
    require(test::files["/workspace.xy"] == damaged_project && test::files["/workspace.xy.oxyl"] == damaged_sidecar,
        "failed restore preserves both original files for recovery");
    require(sdk_arranger_project_load("/song.xy", 1) == 1, "valid candidate before second rejected load");
    test::fail_load = true;
    require(sdk_arranger_project_load("/missing.xy", 1) == 0, "second load rejected before GUI stage apply");
    test::fail_load = false;
    sdk_arranger_project_poll();
    require(test::model.clip(19), "rejected second load must retain first valid stage");
    require(!sdk_arranger_project_status(), "valid load releases quarantine without reusing failed model");
    for (u32 pass = 0; pass < 2; ++pass) {
        test::save_payload = static_cast<u8>(80 + pass);
        test::model.seek(300 + pass);
        require(sdk_arranger_project_changed(true, 1), "new revision publishes snapshot");
        require(sdk_arranger_project_save("/revised.xy") == 1, "different serialized bytes save successfully");
        require(sdk_arranger_project_load("/revised.xy", 1) == 1, "changed-byte project roundtrip");
        sdk_arranger_project_poll();
        require(test::model.clip(19) && test::model.position() == 300 + pass && !sdk_arranger_project_status(),
            "each revision binds its own serialized bytes and clips");
    }
    // Native598b04 loads/commits and synchronously saves workspace via598ae8,
    // before GUI poll applies the validated staged arrangement.
    require(sdk_arranger_project_load("/revised.xy", 1) == 1, "native load before immediate workspace save");
    test::save_payload = 101;
    test::during_save = sdk_arranger_project_poll; // GUI accepts this same stage while stock writes
    require(sdk_arranger_project_save("/fat32/projects/workspace.xy") == 1, "native immediate workspace save");
    test::during_save = nullptr;
    require(!sdk_arranger_project_status() && test::files.count("/fat32/projects/workspace.xy.oxyl"),
        "workspace autosave includes committed staged Linear state before GUI poll");
    sdk_arranger_project_poll();
    require(sdk_arranger_project_load("/fat32/projects/workspace.xy", 1) == 1, "reload immediate workspace pair");
    sdk_arranger_project_poll();
    require(test::enabled && test::model.clip(19) && test::model.position() == 301,
        "workspace reopen retains Linear mode and timeline");
    require(sdk_arranger_project_load("/revised.xy", 1) == 1, "stage before no-GUI workspace save");
    require(sdk_arranger_project_save("/no-gui.xy") == 1 && test::files.count("/no-gui.xy.oxyl"),
        "stage persists with no GUI application at all");
    sdk_arranger_project_poll();
    require(sdk_arranger_project_load("/revised.xy", 1) == 1, "stage before apply plus real edit");
    test::during_save = test::apply_then_edit;
    sdk_arranger_project_save("/concurrent-edit.xy");
    test::during_save = nullptr;
    require(sdk_arranger_project_status() == 2 && !test::files.count("/concurrent-edit.xy.oxyl"),
        "stage apply handoff must not admit subsequent GUI edit");
    // Named-project UI uses commit0, unlike the successful boot workspace path.
    require(sdk_arranger_project_load("/revised.xy", 1) == 1, "restore named Linear source");
    sdk_arranger_project_poll();
    require(sdk_arranger_project_save("/fat32/projects/user/named.xy") == 1, "save named Linear project");
    ++test::generation; test::pointer = test::pointer == 0x1000 ? 0x2000 : 0x1000;
    sdk_arranger_project_poll();
    require(!test::model.clip(19), "new project clears old arrangement");
    require(sdk_arranger_project_load("/fat32/projects/user/named.xy", 0) == 1, "named asynchronous load");
    sdk_arranger_project_poll();
    test::pointer = test::pending; test::pending = 0; ++test::generation;
    sdk_arranger_project_poll();
    require(test::enabled && test::model.clip(19) && !sdk_arranger_project_status(),
        "named save new-project and async reload retains Linear clips");
    require(sdk_arranger_project_save("/fat32/projects/user/untitled.xy") == 1, "native save before rename");
    test::files["/fat32/projects/user/untitled.oxy"] = {1,2,3};
    test::files["/fat32/projects/user/untitled.xy.oxyl.tmp"] = test::files["/fat32/projects/user/untitled.xy.oxyl"];
    require(sdk_arranger_project_rename("/fat32/projects/user/untitled.xy", "/fat32/projects/user/my-song.xy") == 1,
        "native named save renames project and companions");
    require(test::files.count("/fat32/projects/user/my-song.oxy") &&
        test::files.count("/fat32/projects/user/my-song.xy.oxyl.tmp") &&
        !test::files.count("/fat32/projects/user/untitled.xy.oxyl"), "all exact companion names follow rename");
    ++test::generation; sdk_arranger_project_poll();
    require(!test::model.clip(19), "new project after named save");
    require(sdk_arranger_project_load("/fat32/projects/user/my-song.xy",0)==1, "reload renamed saved project");
    test::pointer=test::pending; test::pending=0; ++test::generation; sdk_arranger_project_poll();
    require(test::enabled && test::model.clip(19), "renamed project reload retains Linear mode and timeline");
    const auto before_failed_rename = test::files;
    test::fail_rename=true;
    require(!sdk_arranger_project_rename("/fat32/projects/user/my-song.xy", "/failed.xy"), "native rename failure returned");
    test::fail_rename=false;
    require(test::files==before_failed_rename, "native failure leaves companions untouched");
    test::fail_companion_rename=true;
    require(sdk_arranger_project_rename("/fat32/projects/user/my-song.xy", "/partial.xy"), "partial move retains native result");
    test::fail_companion_rename=false;
    require(sdk_arranger_project_status()==3 && test::files.count("/fat32/projects/user/my-song.xy.oxyl"),
        "failed companion move preserves original and reports recovery failure");
    const auto partial_project=test::files["/partial.xy"];
    require(!sdk_arranger_project_save("/partial.xy") && test::files["/partial.xy"]==partial_project,
        "partial companion rename blocks autosave before project changes");
    return 0;
}
