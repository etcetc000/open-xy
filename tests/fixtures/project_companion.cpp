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
opxy::u32 loaded = 99, order;  // order: 1 for each reset, then 2 for the load
opxy::u8 loaded_bytes[16];
int ready() { return 1; }
void reset(opxy::u32 copy) { order = order * 10 + (copy == 0 ? 1 : 9); }
opxy::u32 save(opxy::u8* data) {
    for (opxy::u32 i = 0; i < 5; ++i) data[i] = saved_bytes[i];
    return 5;
}
void load(const opxy::u8* data, opxy::u32 bytes) {
    order = order * 10 + 2;
    loaded = bytes;
    for (opxy::u32 i = 0; i < bytes && i < 16; ++i) loaded_bytes[i] = data[i];
}
}

extern "C" {
const opxy::platform::ControlMachine sdk_control_machines[] = {
    {nullptr, ready, reset, nullptr, "morph", 16, save, load, true, 1u << 11, nullptr, {}},
    {nullptr, ready, nullptr, nullptr, "plain", 0, nullptr, nullptr, false, 0, nullptr, {}},
    {nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, false, 0, nullptr, {}}};
const opxy::u32 sdk_control_machine_count = 2;
bool sdk_control_prepared[3];
}

// Aux tracks (aux_tracks.cpp): a machine may hold patterns of track 11, with
// the stock role's eight controls kept aside for each; when HOLD is set, the
// fixture holds patterns 0 and 10, whose keys sort apart ("0", "10").
namespace {
bool holding;
opxy::i32 held_controls[2][8] = {{1, 2, 3, -4, 0x7fffffff, 6, 7, 8}, {9, 0, 0, 0, 0, 0, 0, -1}};
opxy::u32 restored_count;
opxy::u32 restored[4][2];
opxy::i32 restored_controls[4][8];
}
opxy::platform::AuxService sdk_aux;
namespace {
const opxy::i32* aux_kept(opxy::u32 track, opxy::u32 pattern) {
    if (!holding || track != 11) return nullptr;
    return pattern == 0 ? held_controls[0] : pattern == 10 ? held_controls[1] : nullptr;
}
void aux_restore(opxy::u32 track, opxy::u32 pattern, const opxy::i32* controls) {
    if (restored_count == 4) return;
    restored[restored_count][0] = track;
    restored[restored_count][1] = pattern;
    for (opxy::u32 i = 0; i < 8; ++i) restored_controls[restored_count][i] = controls[i];
    ++restored_count;
}
void aux_forget() { restored_count = 0; }
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
    holding = std::getenv("HOLD") != nullptr;
    // The aux service, as its startup fills it in (unless NOAUX: a build without it).
    if (!std::getenv("NOAUX")) sdk_aux = opxy::platform::AuxService{nullptr, nullptr, aux_kept, aux_restore, aux_forget};
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
    // The next GUI tick: the keeping machine is reset, then gets its data, once.
    sdk_control_kept_apply();
    sdk_control_kept_apply();
    if (loaded != 99 && (order != 12 || !sdk_control_prepared[0])) return 3;
    for (opxy::u32 r = 0; r < restored_count; ++r) {
        std::printf("aux %u %u", restored[r][0], restored[r][1]);
        for (opxy::u32 i = 0; i < 8; ++i) std::printf(" %d", restored_controls[r][i]);
        std::printf("\n");
    }
    if (loaded == 99) std::printf("none\n");
    else {
        for (opxy::u32 i = 0; i < loaded; ++i) std::printf("%02x", loaded_bytes[i]);
        std::printf("/%u\n", loaded);
    }
    return 0;
}
