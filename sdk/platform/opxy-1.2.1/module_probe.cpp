// Module-file probe, diagnostic only. Once per processor role it loads
// /projects/openxy/test.opx into stock heap memory, calls the module's init
// and writes gui-module.oxc or audio-module.oxc beside it. It answers two
// questions: does each role read the user drive, and does code run from the
// heap. A failed check leaves the module out; startup goes on unchanged.
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
const char* const module_path = "/fat32/projects/openxy/test.opx";

bool attempted;
char report[640];
u32 used;
u32 notes;

void put(const char* s) { while (*s && used < sizeof(report) - 16) report[used++] = *s++; }
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

// One short line from the module; quotes and control characters are dropped.
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

// The whole file into a heap buffer; 0 bytes when missing, unreadable or too large.
unsigned char* read_file(u32& bytes, bool& found) {
    bytes = 0; found = false;
    void* file = open(module_path, "rb");
    if (!file) return nullptr;
    found = true;
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

// The report is written by a task of its own, well after startup: writing
// and flushing from inside startup blocked the boot (probe 002). fclose's FAT
// sync discards the card-flush result (#339's collection_io.hpp), so the role-aware
// stock flush runs explicitly, as #339's helper task does. If either blocks,
// only this task waits. The report is the text so far plus a closing brace,
// so it is valid JSON even if init never returned.
bool report_gui;
volatile u32 finished;  // set once the probe, init included, has returned
u32 started_at, finished_at;
u32 ticks() { return stock<u32()>(0x8c03ee20)(); }
alignas(8) unsigned char report_tcb[112];
alignas(8) u32 report_stack[2048];
void delay(u32 ticks) { stock<void(u32)>(0x8c040188)(ticks); }
void write_report(void*) {
    // Waits for the probe; a report without "done" means init never returned.
    for (u32 waited = 0; !finished && waited < 60000; waited += 100) delay(100);
    delay(5000);  // startup I/O first
    const char* path = report_gui ? "/fat32/projects/openxy/gui-module.oxc" : "/fat32/projects/openxy/audio-module.oxc";
    put(finished ? ",\"done\":true" : ",\"done\":false");
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
}

extern "C" void sdk_module_probe() __asm__("sdk_module_probe");
void sdk_module_probe() {
    if (attempted) return;
    attempted = true;
    const bool gui = stock<u32()>(0x8d5c3eac)() != 0;
    used = 0; notes = 0;
    put("{\"format\":\"openxy-module-probe/1\",\"role\":\"");
    put(gui ? "gui" : "audio");
    put("\",\"status\":\"");
    started_at = ticks();
    start_report(gui);
    u32 bytes; bool found;
    unsigned char* data = read_file(bytes, found);
    modules::Module module = {};
    modules::Status status = !found ? modules::io_error : !data ? modules::no_space :
        modules::stage(data, bytes, modules::os_1_2_1, modules::Memory{nullptr, allocate, release, synchronize}, module);
    if (data) release(nullptr, data);
    if (!found) put("missing\"");
    else if (status != modules::loaded) { put("m-"); put_dec(status); put("\""); }
    else {
        static const ModuleApi api_gui = {module_api_version, 1, note}, api_audio = {module_api_version, 0, note};
        put("staged\",\"base\":\""); put_hex(module.base); put("\",\"bytes\":"); put_dec(module.bytes);
        put(",\"notes\":[");
        const int result = reinterpret_cast<int (*)(const ModuleApi*)>(static_cast<Address>(module.init))(gui ? &api_gui : &api_audio);
        put("],\"init\":"); put_dec(static_cast<u32>(result));
        // A module whose init refused stays in memory but is never called again.
    }
    finished_at = ticks();
    finished = 1;
}
