#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <project_companion.cpp>
using namespace opxy;
using namespace opxy::platform;
namespace test {
void require(bool ok, const char* message) { if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); } }
std::map<std::string, std::string> files;
struct Handle { u16 flags; std::string path; u32 offset; bool writing; };
u32 generation = 1, pointer, pending, dirty_count, notifications;
unsigned char project[0x54076c];
std::string native_data, candidate_data;
bool reject_load, short_write, close_failure, rename_failure, companion_rename_failure, change_during_save;
extensions::ProjectState last;
std::vector<u8> received;
u32 get_generation() { return generation; }
void dirty() { ++dirty_count; }
void committed() { if (pending) { pointer = pending; pending = 0; native_data = candidate_data; ++generation; } }
void* open(const char* path, const char* mode) {
    if (mode[0] == 'r' && !files.count(path)) return nullptr;
    if (mode[0] == 'w') files[path].clear();
    return new Handle{0, path, 0, mode[0] == 'w'};
}
u32 read(void* output, u32 size, u32 count, void* raw) {
    auto* h = static_cast<Handle*>(raw);
    const auto& bytes = files[h->path];
    u32 n = size * count;
    if (n > bytes.size() - h->offset) { n = static_cast<u32>(bytes.size()) - h->offset; h->flags |= 0x100; }
    std::memcpy(output, bytes.data() + h->offset, n); h->offset += n;
    return n / size;
}
u32 write(const void* input, u32 size, u32 count, void* raw) {
    auto* h = static_cast<Handle*>(raw);
    if (short_write && count) --count;
    files[h->path].append(static_cast<const char*>(input), size * count);
    return count;
}
int close(void* raw) {
    auto* h = static_cast<Handle*>(raw); const bool fail = h->writing && close_failure;
    delete h; return fail ? -1 : 0;
}
u32 rename(const char* from, const char* to) {
    if (!files.count(from)) return 0;
    files.erase(to);
    if (rename_failure || (companion_rename_failure && std::string(from).find(".oxy") != std::string::npos)) return 0;
    files[to] = files[from]; files.erase(from); return 1;
}
u32 save(void*, const char* path) {
    files[path] = native_data;
    if (change_during_save) { pending = pointer; sdk_project_new_commit(); }
    return 1;
}
u32 deserialize(void*, const char* path) {
    if (!files.count(path)) return 0;
    candidate_data = files[path]; return 1;
}
u32 load(const char* path, u32 commit) {
    if (!sdk_project_load(project, path) || reject_load) return 0;
    pending = pointer;
    if (commit) committed();
    return 1;
}
void notify(const extensions::ProjectState& state) {
    ++notifications; last = state; received.clear();
    if (state.size) received.assign(state.data, state.data + state.size);
}
std::string document(const std::string& project_bytes, const std::string& sections, u32 version = 1) {
    char binding[160];
    std::snprintf(binding, sizeof(binding), "{\"format\":\"open-xy-project\",\"version\":%u,\"project\":{\"bytes\":%u,\"crc32\":\"%08x\"},\"sections\":{", version,
        static_cast<unsigned>(project_bytes.size()), crc32(0, reinterpret_cast<const unsigned char*>(project_bytes.data()), static_cast<u32>(project_bytes.size())));
    return std::string(binding) + sections + "}}";
}
}
const CompanionMachine sdk_companion_machines[] = {{nullptr, nullptr, nullptr, false}};
Address opxy::platform::host_stock_address(Address address) {
    using namespace test;
    switch (address) {
    case 0x840a0524: return reinterpret_cast<Address>(&pointer);
    case 0x840a0528: return reinterpret_cast<Address>(&pending);
    case 0x8d584224: return reinterpret_cast<Address>(&test::get_generation);
    case 0x8d585ee8: return reinterpret_cast<Address>(&test::dirty);
    case 0x8d585cdc: return reinterpret_cast<Address>(&test::committed);
    case 0x8d5890b0: return reinterpret_cast<Address>(&test::load);
    case 0x8d599b10: return reinterpret_cast<Address>(&test::deserialize);
    case 0x8d599074: return reinterpret_cast<Address>(&test::save);
    case 0x8fed0348: return reinterpret_cast<Address>(&test::open);
    case 0x8fed05bc: return reinterpret_cast<Address>(&test::read);
    case 0x8fed0780: return reinterpret_cast<Address>(&test::write);
    case 0x8fed00ae: return reinterpret_cast<Address>(&test::close);
    case 0x8c040c78: return reinterpret_cast<Address>(&test::rename);
    }
    std::fprintf(stderr, "unknown stock %lx\n", static_cast<unsigned long>(address)); std::abort();
}
int main() {
    using namespace test;
    pointer = static_cast<u32>(reinterpret_cast<Address>(project));
    const ProjectStateSpec own = {"demo.timeline", 1, 8192};
    const std::string unknown = "\"other.future\":{\"version\":2,\"nested\":[true,{\"keep\":\"yes\"}]}";
    files["/a.xy"] = "project A";
    files["/a.oxy"] = document(files["/a.xy"], "\"demo.timeline\":{\"version\":1,\"data\":\"0001ff\"}," + unknown);
    require(sdk_project_load_begin("/a.xy", 1) == 1 && notifications == 0, "load never invokes extension code");
    files["/reject.xy"] = "rejected candidate";
    reject_load = true;
    require(!sdk_project_load_begin("/reject.xy", 0), "second candidate rejected before first notification");
    reject_load = false;
    require(sdk_project_save(project, "/workspace.xy") == 1, "immediate workspace save after rejected second load, before callback");
    require(files["/workspace.oxy"].find("0001ff") != std::string::npos && files["/workspace.oxy"].find("other.future") != std::string::npos,
        "committed staged payload survives immediate save");
    project_state_observe(own, notify);
    require(last.status == extensions::ProjectStateStatus::ready && received == std::vector<u8>({0, 1, 255}), "restored payload");
    const auto token_a = last.token;
    const u8 replacement[] = {4, 5, 6};
    require(project_state_publish(own, token_a, replacement, 3) && dirty_count == 1, "publish dirties native project");
    project_state_observe(own, notify);
    require(received == std::vector<u8>({4, 5, 6}), "publish notifies revision");
    files["/b.xy"] = "project B";
    files["/b.oxy"] = document(files["/b.xy"], "\"wrong.owner\":{\"version\":1,\"secret\":true}");
    require(sdk_project_save(project, "/b.xy") == 1, "Save As A over B");
    require(files["/b.oxy"].find("wrong.owner") == std::string::npos && files["/b.oxy"].find("other.future") != std::string::npos,
        "Save As retains source A owners, never destination B");
    pending = pointer; sdk_project_new_commit(); project_state_observe(own, notify);
    require(last.status == extensions::ProjectStateStatus::empty && !project_state_publish(own, token_a, replacement, 3), "New same pointer invalidates token");
    require(sdk_project_save(project, "/new.xy") == 1 && files["/new.oxy"].find("other.future") == std::string::npos, "New drops previous namespaces");
    require(sdk_project_load_begin("/a.xy", 0) == 1, "deferred load stages");
    require(!project_state_publish(own, last.token, replacement, 3), "pending load blocks mutation");
    sdk_project_new_commit(); project_state_observe(own, notify);
    require(last.status == extensions::ProjectStateStatus::empty, "New cannot attach abandoned candidate at same next generation");
    require(sdk_project_load_begin("/a.xy", 0) == 1, "deferred reload"); committed();
    require(sdk_project_save(project, "/workspace.xy") == 1 && files["/workspace.oxy"].find("0001ff") != std::string::npos, "async commit save before sync");
    project_state_observe(own, notify);
    reject_load = true; require(!sdk_project_load_begin("/b.xy", 0), "native validation rejection"); reject_load = false;
    project_state_observe(own, notify); require(received == std::vector<u8>({0, 1, 255}), "failed load keeps active state");
    files["/future.xy"] = "future";
    files["/future.oxy"] = document("future", "\"demo.timeline\":{\"version\":2,\"future\":true}," + unknown);
    require(sdk_project_load_begin("/future.xy", 1) == 1, "future section load"); project_state_observe(own, notify);
    require(last.status == extensions::ProjectStateStatus::unsupported && !project_state_publish(own, last.token, replacement, 3), "unsupported owner cannot be overwritten");
    require(sdk_project_save(project, "/future-copy.xy") == 1 && files["/future-copy.oxy"].find("\"future\":true") != std::string::npos, "unsupported sections preserved");
    files["/bad.xy"] = "bad"; files["/bad.oxy"] = "{";
    require(sdk_project_load_begin("/bad.xy", 1) == 1, "stock load succeeds with corrupt companion"); project_state_observe(own, notify);
    require(last.status == extensions::ProjectStateStatus::unavailable && !sdk_project_save(project, "/bad.xy") && files["/bad.xy"] == "bad", "corrupt load protects before native save");
    require(sdk_project_load_begin("/a.xy", 1) == 1, "valid load clears quarantine"); project_state_observe(own, notify);
    short_write = true; require(sdk_project_save(project, "/failed.xy") == 1, "native result preserved on metadata failure"); short_write = false;
    project_state_observe(own, notify); require(last.status == extensions::ProjectStateStatus::save_failed, "failure notification");
    require(sdk_project_save(project, "/failed.xy") == 1, "retry"); project_state_observe(own, notify);
    require(last.status == extensions::ProjectStateStatus::ready, "retry clears failure");
    require(sdk_project_rename("/failed.xy", "/named.xy") == 1 && files.count("/named.oxy"), "named Save moves companion");
    pending = pointer; sdk_project_new_commit(); project_state_observe(own, notify);
    require(sdk_project_load_begin("/named.xy", 1) == 1, "named reload"); project_state_observe(own, notify);
    require(received == std::vector<u8>({0, 1, 255}), "named reload restores payload");
    require(!project_state_publish(own, last.token, replacement, 8193), "capacity checked before reading caller bytes");
    files["/large.xy"] = "large";
    files["/large.oxy"] = document("large", "\"absent.owner\":{\"version\":1,\"pad\":\"" + std::string(65300, 'x') + "\"}");
    require(sdk_project_load_begin("/large.xy", 1) == 1, "large absent section loads"); project_state_observe(own, notify);
    require(!sdk_project_save(project, "/large-copy.xy") && !files.count("/large-copy.xy"), "output budget fails before native writes");
    require(sdk_project_load_begin("/a.xy", 1) == 1, "load after budget failure"); project_state_observe(own, notify);
    require(sdk_project_save(project, "/rename-failure.xy") == 1, "save before rename failure");
    companion_rename_failure = true;
    require(sdk_project_rename("/rename-failure.xy", "/partial.xy") == 1, "preserve native rename result");
    companion_rename_failure = false; project_state_observe(own, notify);
    require(last.status == extensions::ProjectStateStatus::unavailable && !sdk_project_save(project, "/partial.xy"), "partial rename protects recovery data");
    require(files.count("/rename-failure.oxy") && project_state_status(), "original metadata retained and recovery status visible");
    // Stock or another editor legitimately invalidates the binding; it is not corruption.
    files["/stock.xy"] = "edited by stock";
    files["/stock.oxy"] = document("old bytes", "\"demo.timeline\":{\"version\":1,\"data\":\"abcd\"}");
    require(sdk_project_load_begin("/stock.xy", 1) == 1, "stock-edited load"); project_state_observe(own, notify);
    require(last.status == extensions::ProjectStateStatus::empty && sdk_project_save(project, "/stock.xy") == 1,
        "stale companion does not freeze native saves");
    short_write = true; sdk_project_save(project, "/retry.xy"); short_write = false;
    const u32 dirty_before_new = dirty_count;
    pending = pointer; sdk_project_new_commit(); project_state_observe(own, notify);
    require(dirty_count == dirty_before_new, "old project retry cannot dirty New");
    return 0;
}
