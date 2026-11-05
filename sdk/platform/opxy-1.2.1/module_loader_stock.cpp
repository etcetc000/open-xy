// Module files. Once per processor role, at startup, every <name>.opx in
// /open-xy/modules is loaded into stock heap memory, in name order, and its
// init is called (module_loader.hpp has the format). A module that fails any
// check, or whose init refuses, is left out and startup goes on.
#include "module_loader.hpp"
#include "module_synths.hpp"
#include "safe_mode.hpp"
#include "stock.hpp"
#include <opxy/module.hpp>
using namespace opxy;
using namespace opxy::platform;
extern "C" u32 sdk_module_sync_code(u32, u32) __asm__("sdk_module_sync_code");
namespace {
// The SDK heap of the 1.2.1 profile: stock's arena ends where the module
// reservation (profile module_base) begins; 8 MiB stay free for stock.
const u32 arena_base = 0x8c0d0380, protected_base = 0x8d2d0380, reserve_bytes = 0x800000;
const u32 file_limit = 0x100000;  // 1 MiB: room for modules with large tables
// The custom firmware's own folder on the user drive, and its modules inside.
const char root[] = "/fat32/open-xy";
const char folder[] = "/fat32/open-xy/modules";
const u32 name_max = 15;  // name without ".opx"

bool attempted;

// A short line from a module's init. Kept in the API for the live log
// monitor to come; nothing reads it yet.
void note(const char*) {}

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
}
unsigned char* sdk_module_allocate(u32 bytes) { return allocate(nullptr, bytes); }
void sdk_module_release(unsigned char* p) { release(nullptr, p); }
namespace {
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

}
unsigned char* sdk_module_read_file(const char* path, u32& bytes) { return read_file(path, bytes); }
namespace {

// The GUI role creates /open-xy and /open-xy/modules, so they show over MTP
// before the first module is copied. Stock's mkdir (8c0bd5bc, as its MTP
// folder creation calls it) fails harmlessly when a folder exists. A task of
// its own does it, after startup: writing and flushing from inside startup
// blocked the boot (probe 002). The FAT sync discards the card-flush result
// (#339's collection_io.hpp), so the role-aware stock flush runs explicitly.
// If either blocks, only this task waits.
volatile u32 finished;
alignas(8) unsigned char folders_tcb[112];
alignas(8) u32 folders_stack[2048];
void delay(u32 ticks) { stock<void(u32)>(0x8c040188)(ticks); }
void make_folders(void*) {
    for (u32 waited = 0; !finished && waited < 60000; waited += 100) delay(100);
    delay(5000);  // startup I/O first
    stock<i32(const char*, u32)>(0x8c0bd5bc)(root, 0x1ff);
    stock<i32(const char*, u32)>(0x8c0bd5bc)(folder, 0x1ff);
    stock<i32()>(0x8c0c0f00)();
    for (;;) delay(60000);
}
void start_folders() {
    stock<void*(void (*)(void*), const char*, u32, void*, u32, u32*, void*)>(0x8c03f744)(
        make_folders, "openxy-modules", 2048, nullptr, 1, folders_stack, folders_tcb);
}

// The module names, in heap memory sized by the folder: no fixed count.
typedef char Name[name_max + 1];
Name* names;
u32 count;

// One pass over the folder; with no list, it only counts the module files.
u32 scan_folder(Name* list, u32 capacity) {
    void* dir = dir_open(folder);
    if (!dir) return 0;
    u32 found = 0;
    Name name;
    while (const unsigned char* entry = dir_next(dir)) {
        if (entry[dirent_type] == dirent_folder) continue;
        if (!module_name(reinterpret_cast<const char*>(entry + dirent_name), name)) continue;
        if (!list) { ++found; continue; }
        if (found == capacity) break;  // the folder grew between the passes
        // Insertion in name order: both roles load the same set in the same order.
        u32 at = found++;
        for (; at && before(name, list[at - 1]); --at)
            for (u32 i = 0; i <= name_max; ++i) list[at][i] = list[at - 1][i];
        for (u32 i = 0; i <= name_max; ++i) list[at][i] = name[i];
    }
    dir_close(dir);
    return found;
}

void scan() {
    const u32 found = scan_folder(nullptr, 0);
    if (!found) return;
    names = reinterpret_cast<Name*>(allocate(nullptr, found * sizeof(Name)));
    if (names) count = scan_folder(names, found);
}

template<class Function> u32 address(Function* f) { return static_cast<u32>(reinterpret_cast<Address>(f)); }

void load(const char* name, bool gui) {
    static const ModuleApi api_gui = {module_api_version, 1, note, sdk_module_synth_register, sdk_module_synth_voice},
                           api_audio = {module_api_version, 0, note, sdk_module_synth_register, sdk_module_synth_voice};
    char path[sizeof folder + 1 + name_max + 5];
    u32 n = 0;
    for (const char* p = folder; *p; ++p) path[n++] = *p;
    path[n++] = '/';
    for (const char* p = name; *p; ++p) path[n++] = *p;
    for (const char* p = ".opx"; *p; ++p) path[n++] = *p;
    path[n] = 0;

    // What a module may import by name (module.hpp, opxy::core). Order is free.
    const modules::Export exports[] = {
        {"note", address(note)},
        {"synth_register", address(sdk_module_synth_register)},
        {"synth_voice", address(sdk_module_synth_voice)},
    };
    u32 bytes;
    unsigned char* data = read_file(path, bytes);
    modules::Module module = {};
    const modules::Status status = !data ? modules::io_error :
        modules::stage(data, bytes, modules::os_1_2_1, modules::Memory{nullptr, allocate, release, synchronize},
                       modules::Exports{exports, sizeof exports / sizeof exports[0]}, module);
    if (data) release(nullptr, data);
    if (status != modules::loaded) return;
    // A module whose init refuses stays in memory but is never called again.
    reinterpret_cast<int (*)(const ModuleApi*)>(static_cast<Address>(module.init))(gui ? &api_gui : &api_audio);
}
}

void sdk_module_load() {
    if (attempted) return;
    attempted = true;
    const bool gui = stock<u32()>(0x8d5c3eac)() != 0;
    if (gui) start_folders();
    // Safe mode loads nothing; the folders still appear, so a module can be removed.
    if (safe_mode()) { finished = 1; return; }
    scan();
    sdk_module_registration(true);
    for (u32 i = 0; i < count; ++i) load(names[i], gui);
    sdk_module_registration(false);
    if (names) release(nullptr, reinterpret_cast<unsigned char*>(names));
    names = nullptr;
    count = 0;
    finished = 1;
}
