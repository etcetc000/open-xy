// Module files, diagnostic build. Once per processor role, at startup, every
// <name>.opx in /projects/openxy is loaded into stock heap memory, in name
// order, and its init is called (module_loader.hpp has the format). A module
// that fails any check is left out and startup goes on. Each role then writes
// gui-module.oxc or audio-module.oxc beside the modules: one entry per module
// with its status ("loaded" or "m-<code>"), base, notes and init result.
#include "module_loader.hpp"
#include "stock.hpp"
#include <opxy/module.hpp>
using namespace opxy;
using namespace opxy::platform;
extern "C" u32 sdk_module_sync_code(u32, u32) __asm__("sdk_module_sync_code");
namespace {
// The SDK heap of the 1.2.1 profile: stock's arena ends where the module
// reservation (profile module_base) begins; 8 MiB stay free for stock.
const u32 arena_base = 0x8c0d0380, protected_base = 0x8d2d0380, reserve_bytes = 0x800000;
const u32 file_limit = 0x40000;
const char folder[] = "/fat32/projects/openxy";
const u32 modules_max = 16, name_max = 15;  // name without ".opx"

bool attempted;
char report[2048];
u32 used;
u32 notes;

void put(const char* s) { while (*s && used < sizeof(report) - 48) report[used++] = *s++; }
void put_hex(u32 v) {
    put("0x");
    for (int shift = 28; shift >= 0; shift -= 4) { char c[2] = {"0123456789abcdef"[(v >> shift) & 15], 0}; put(c); }
}
void put_dec(u32 v) {
    char digits[11]; int n = 0;
    do digits[n++] = static_cast<char>('0' + v % 10); while (v /= 10);
    char c[2] = {0, 0};
    while (n) { c[0] = digits[--n]; put(c); }
}

// One short line from the module being initialized; quotes and control
// characters are dropped.
void note(const char* text) {
    if (!text || notes >= 8) return;
    put(notes++ ? ",\"" : "\"");
    for (u32 i = 0; text[i] && i < 48; ++i) {
        const char c = text[i];
        if (c >= 32 && c < 127 && c != '"' && c != '\\') { char s[2] = {c, 0}; put(s); }
    }
    put("\"");
}

bool in_arena(const unsigned char* p, u32 bytes) {
    const u32 a = static_cast<u32>(reinterpret_cast<Address>(p));
    return p && !(a & 3) && a >= arena_base && bytes <= protected_base - a;
}

// Stock's nonfatal heap entry under its usual mutex, admitted only while the
// top chunk keeps reserve_bytes free (the checks of #339's collection_io.cpp).
unsigned char* allocate(void*, u32 bytes) {
    if (!bytes || bytes > file_limit + 64) return nullptr;
    const u32 mutex = word(0x08036aa4), arena = word(0x08036aa8);
    if (!mutex || (arena & 3) || arena < arena_base || arena > protected_base - 28 ||
        !stock<u32(u32, u32)>(0x8c03e6f4)(mutex, 0xffffffffu)) return nullptr;
    const u32 top_bytes = word(arena + 0x0c), top = word(arena + 0x18);
    const u32 rounded = bytes < 27 ? 32 : (bytes + 35) & ~31u;
    const bool admitted = top >= arena_base && top < protected_base && !(top & 7) && !(top_bytes & 7) &&
        top_bytes <= protected_base - top && top_bytes >= rounded + reserve_bytes;
    unsigned char* result = admitted ? stock<unsigned char*(u32, u32)>(0x8c048788)(arena, bytes) : nullptr;
    const u32 unlocked = stock<u32(u32, const void*, u32, u32)>(0x8c03e9c8)(mutex, nullptr, 0, 0);
    return unlocked && in_arena(result, bytes) ? result : nullptr;
}
void release(void*, unsigned char* p) { if (in_arena(p, 1)) stock<void(void*)>(0x8d4d2910)(p); }
bool synchronize(void*, u32 address, u32 bytes) { return sdk_module_sync_code(address, bytes) != 0; }

void* open(const char* path, const char* mode) { return stock<void*(const char*, const char*)>(0x8fed0348)(path, mode); }
u32 read(void* file, void* out, u32 bytes) { return stock<u32(void*, u32, u32, void*)>(0x8fed05bc)(out, 1, bytes, file); }
bool failed(void* file) { return (*static_cast<const volatile u16*>(file) & 0x200u) != 0; }
void close(void* file) { stock<int(void*)>(0x8fed00ae)(file); }

// The stock VFS folder calls, as stock's preset scan (8d4eb928) uses them on
// /presets. An entry holds its type at +0xa (4 = folder) and its name at +0xb.
void* dir_open(const char* path) { return stock<void*(const char*)>(0x8c0be54a)(path); }
const unsigned char* dir_next(void* dir) { return stock<const unsigned char*(void*)>(0x8c0be6a4)(dir); }
void dir_close(void* dir) { stock<void(void*)>(0x8c0be7b2)(dir); }
const u32 dirent_type = 0xa, dirent_name = 0xb, dirent_folder = 4;

// A module name: letters, digits, _ and -, at most name_max, then ".opx".
bool module_name(const char* file, char* name) {
    u32 n = 0;
    for (; n < name_max && file[n] && file[n] != '.'; ++n) {
        const char c = file[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
        name[n] = c;
    }
    name[n] = 0;  // n <= name_max: inside the buffer
    return n && file[n] == '.' && file[n + 1] == 'o' && file[n + 2] == 'p' && file[n + 3] == 'x' && !file[n + 4];
}

bool before(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) < static_cast<unsigned char>(*b);
}

// The whole file into a heap buffer; null when unreadable or too large.
unsigned char* read_file(const char* path, u32& bytes) {
    bytes = 0;
    void* file = open(path, "rb");
    if (!file) return nullptr;
    unsigned char* data = allocate(nullptr, file_limit);
    if (data) {
        for (u32 n; bytes < file_limit && (n = read(file, data + bytes, file_limit - bytes > 4096 ? 4096 : file_limit - bytes)); )
            bytes += n;
        unsigned char extra;
        if (failed(file) || (bytes == file_limit && read(file, &extra, 1))) { release(nullptr, data); data = nullptr; bytes = 0; }
    }
    close(file);
    return data;
}

// The report is written by a task of its own, after startup: writing and
// flushing from inside startup blocked the boot (probe 002). fclose's FAT sync
// discards the card-flush result (#339's collection_io.hpp), so the role-aware stock
// flush runs explicitly, as #339's helper task does. If either blocks, only
// this task waits. Without "done" the report stops where startup stopped.
bool report_gui;
volatile u32 finished;
u32 started_at, finished_at;
alignas(8) unsigned char report_tcb[112];
alignas(8) u32 report_stack[2048];
u32 ticks() { return stock<u32()>(0x8c03ee20)(); }
void delay(u32 ticks) { stock<void(u32)>(0x8c040188)(ticks); }
void write_report(void*) {
    for (u32 waited = 0; !finished && waited < 60000; waited += 100) delay(100);
    delay(5000);  // startup I/O first
    const char* path = report_gui ? "/fat32/projects/openxy/gui-module.oxc" : "/fat32/projects/openxy/audio-module.oxc";
    put(finished ? "],\"done\":true" : "],\"done\":false");
    put(",\"probe_ticks\":"); put_dec(finished ? finished_at - started_at : 0);
    put(",\"report_ticks\":"); put_dec(ticks() - started_at);
    put("}\n");
    if (void* file = open(path, "wb")) {
        stock<u32(const void*, u32, u32, void*)>(0x8fed0780)(report, 1, used, file);
        close(file);
        stock<i32()>(0x8c0c0f00)();
    }
    for (;;) delay(60000);
}
void start_report(bool gui) {
    report_gui = gui;
    stock<void*(void (*)(void*), const char*, u32, void*, u32, u32*, void*)>(0x8c03f744)(
        write_report, "openxy-modules", 2048, nullptr, 1, report_stack, report_tcb);
}

char names[modules_max][name_max + 1];
u32 count;
bool too_many;

void scan() {
    void* dir = dir_open(folder);
    if (!dir) return;
    char name[name_max + 1];
    while (const unsigned char* entry = dir_next(dir)) {
        if (entry[dirent_type] == dirent_folder) continue;
        if (!module_name(reinterpret_cast<const char*>(entry + dirent_name), name)) continue;
        if (count == modules_max) { too_many = true; continue; }
        // Insertion in name order: both roles load the same set in the same order.
        u32 at = count++;
        for (; at && before(name, names[at - 1]); --at)
            for (u32 i = 0; i <= name_max; ++i) names[at][i] = names[at - 1][i];
        for (u32 i = 0; i <= name_max; ++i) names[at][i] = name[i];
    }
    dir_close(dir);
}

void load(const char* name, bool gui) {
    static const ModuleApi api_gui = {module_api_version, 1, note}, api_audio = {module_api_version, 0, note};
    char path[sizeof folder + 1 + name_max + 5];
    u32 n = 0;
    for (const char* p = folder; *p; ++p) path[n++] = *p;
    path[n++] = '/';
    for (const char* p = name; *p; ++p) path[n++] = *p;
    for (const char* p = ".opx"; *p; ++p) path[n++] = *p;
    path[n] = 0;

    put("{\"name\":\""); put(name); put("\",\"status\":\"");
    u32 bytes;
    unsigned char* data = read_file(path, bytes);
    modules::Module module = {};
    const modules::Status status = !data ? modules::io_error :
        modules::stage(data, bytes, modules::os_1_2_1, modules::Memory{nullptr, allocate, release, synchronize}, module);
    if (data) release(nullptr, data);
    if (status != modules::loaded) { put("m-"); put_dec(status); put("\"}"); return; }
    put("loaded\",\"base\":\""); put_hex(module.base); put("\",\"bytes\":"); put_dec(module.bytes);
    put(",\"notes\":["); notes = 0;
    const int result = reinterpret_cast<int (*)(const ModuleApi*)>(static_cast<Address>(module.init))(gui ? &api_gui : &api_audio);
    put("],\"init\":"); put_dec(static_cast<u32>(result));
    if (result) put(",\"status_after_init\":\"m-108\"");
    // A module whose init refused stays in memory but is never called again.
    put("}");
}
}

extern "C" void sdk_module_probe() __asm__("sdk_module_probe");
void sdk_module_probe() {
    if (attempted) return;
    attempted = true;
    const bool gui = stock<u32()>(0x8d5c3eac)() != 0;
    used = 0;
    started_at = ticks();
    put("{\"format\":\"openxy-module-probe/2\",\"role\":\"");
    put(gui ? "gui" : "audio");
    put("\",\"modules\":[");
    start_report(gui);
    scan();
    for (u32 i = 0; i < count; ++i) {
        if (i) put(",");
        load(names[i], gui);
    }
    if (too_many) put(count ? ",{\"status\":\"m-109\"}" : "{\"status\":\"m-109\"}");
    finished_at = ticks();
    finished = 1;
}
