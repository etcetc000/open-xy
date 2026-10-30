// Host boundary double for the real project companion writer: stock's newlib file
// calls are host stdio, stock's save writes known bytes or fails, and a track
// control machine keeps a few bytes.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <project_companion.cpp>
#include <track_control_project.cpp>
extern "C" void sdk_control_forget() {}

extern "C" opxy::u32 sdk_project_kept(void* file) { return sdk_control_kept_write(file); }
extern "C" void sdk_project_loaded(const char* path) { sdk_control_kept_load(path); }

using opxy::platform::Address;

namespace {
const char* saving;
unsigned save_result;
void* open_file(const char* path, const char* mode) { return std::fopen(path, mode); }
opxy::u32 read_file(void* data, opxy::u32 size, opxy::u32 count, void* file) {
    return static_cast<opxy::u32>(std::fread(data, size, count, static_cast<std::FILE*>(file)));
}
opxy::u32 write_file(const void* data, opxy::u32 size, opxy::u32 count, void* file) {
    return static_cast<opxy::u32>(std::fwrite(data, size, count, static_cast<std::FILE*>(file)));
}
int close_file(void* file) { return std::fclose(static_cast<std::FILE*>(file)); }
opxy::u32 stock_save(void*, const char* path) {
    if (!save_result) return 0;
    std::FILE* file = std::fopen(path, "wb");
    for (unsigned i = 0; i < 5000; ++i) std::fputc(static_cast<int>(i * 7 % 256), file);
    std::fclose(file);
    return save_result;
}
const char* number(unsigned n) { return reinterpret_cast<const char*>(static_cast<Address>(n)); }
}

namespace {
// Track control: one machine keeps five bytes, one keeps nothing.
const opxy::u8 saved_bytes[5] = {'s', 't', 'a', 't', 0xff};
opxy::u32 loaded = 99;
opxy::u8 loaded_bytes[16];
int ready() { return 1; }
opxy::u32 save(opxy::u8* data) {
    for (opxy::u32 i = 0; i < 5; ++i) data[i] = saved_bytes[i];
    return 5;
}
void load(const opxy::u8* data, opxy::u32 bytes) {
    loaded = bytes;
    for (opxy::u32 i = 0; i < bytes && i < 16; ++i) loaded_bytes[i] = data[i];
}
}

extern "C" {
const opxy::platform::ControlMachine sdk_control_machines[] = {
    {nullptr, ready, nullptr, nullptr, "morph", 16, save, load, true},
    {nullptr, ready, nullptr, nullptr, "plain", 0, nullptr, nullptr, false},
    {nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, false}};
const opxy::u32 sdk_control_machine_count = 2;
}

namespace {
opxy::u32 stock_read(void*, const char*) { return save_result; }
// Stock's two project clears, and its loading flag, which makes them do nothing.
opxy::u32 loading, clears;
opxy::u32 stock_clear(opxy::u32, opxy::u32, opxy::u32) { if (!loading) ++clears; return 0; }
}

const opxy::platform::CompanionMachine sdk_companion_machines[] = {
    {"karplus", number(36), number(84), false},
    {"pingpong", number(40), number(21), true},
    {"skipped", number(0), number(0), false},  // a kit machine left out of the selection
    {"timestretch", number(35), number(7), false},
    {nullptr, nullptr, nullptr, false}
};

Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case 0x8fed0348: return reinterpret_cast<Address>(&open_file);
    case 0x8fed05bc: return reinterpret_cast<Address>(&read_file);
    case 0x8fed0780: return reinterpret_cast<Address>(&write_file);
    case 0x8fed00ae: return reinterpret_cast<Address>(&close_file);
    case 0x8d599074: return reinterpret_cast<Address>(&stock_save);
    case 0x8d599b10: return reinterpret_cast<Address>(&stock_read);
    case 0x8d588114: return reinterpret_cast<Address>(&stock_clear);
    case 0x8d588988: return reinterpret_cast<Address>(&stock_clear);
    case 0x840a0528: return reinterpret_cast<Address>(&loading);
    }
    std::abort();
}

// project_companion <path> <stock result> [read | clear | random | busy]: saves, or
// loads the project, starts a new one (clear or random), or tries to while a project
// is loading (busy), and prints what the keeping machine got back as hex, or "none".
int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 2;
    saving = argv[1];
    save_result = static_cast<unsigned>(std::atoi(argv[2]));
    if (argc == 3) return sdk_project_save(nullptr, saving) == save_result ? 0 : 1;
    const std::string mode = argv[3];
    loading = mode == "busy";
    if (mode == "read") {
        // The companion's load hook (project_load.cpp tests it) gives track control
        // the path after a load that succeeds, and nothing after one that fails.
        if (save_result) sdk_control_kept_load(saving);
    } else if (mode == "clear" || mode == "busy") {
        sdk_project_clear(0, 0, 0);
        if (clears != (loading ? 0u : 1u)) return 1;
    } else if (mode == "random") {
        sdk_project_clear_random(0, 0, 0);
        if (clears != 1) return 1;
    } else {
        return 2;
    }
    if (loaded == 99) std::printf("none\n");
    else {
        for (opxy::u32 i = 0; i < loaded; ++i) std::printf("%02x", loaded_bytes[i]);
        std::printf("/%u\n", loaded);
    }
    return 0;
}
