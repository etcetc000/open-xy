// Host boundary double for the real project companion writer: stock's newlib file
// calls are host stdio, and stock's save writes known bytes or fails.
// project_companion <path> <save result>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <project_companion.cpp>

using opxy::platform::Address;

namespace {
const char* saving;
unsigned save_result;
// Match the verified native FILE prefix while using host stdio behind it.
struct HostFile { opxy::u16 flags; std::FILE* stream; bool writing; };
const char* fault() { const char* s = std::getenv("OPXY_COMPANION_FAULT"); return s ? s : ""; }
void* open_file(const char* path, const char* mode) {
    std::FILE* stream = std::fopen(path, mode);
    return stream ? new HostFile{0, stream, mode[0] == 'w'} : nullptr;
}
opxy::u32 read_file(void* data, opxy::u32 size, opxy::u32 count, void* file) {
    HostFile* f = static_cast<HostFile*>(file);
    if (std::strcmp(fault(), "read") == 0) { f->flags |= 0x200; return 0; }
    const auto n = static_cast<opxy::u32>(std::fread(data, size, count, f->stream));
    if (std::feof(f->stream)) f->flags |= 0x100;
    if (std::ferror(f->stream)) f->flags |= 0x200;
    return n;
}
opxy::u32 write_file(const void* data, opxy::u32 size, opxy::u32 count, void* file) {
    HostFile* f = static_cast<HostFile*>(file);
    if (std::strcmp(fault(), "write") == 0 && count) --count;
    return static_cast<opxy::u32>(std::fwrite(data, size, count, f->stream));
}
int close_file(void* file) {
    HostFile* f = static_cast<HostFile*>(file);
    const bool fail = f->writing && std::strcmp(fault(), "close") == 0;
    const int result = std::fclose(f->stream);
    delete f;
    return fail ? -1 : result;
}
opxy::u32 rename_file(const char* from, const char* to) {
    std::remove(to); // Match stock replacement, not the host's overwrite guarantee.
    return std::strcmp(fault(), "rename") == 0 ? 0 : std::rename(from, to) == 0;
}
opxy::u32 stock_save(void*, const char* path) {
    if (!save_result) return 0;
    std::FILE* file = std::fopen(path, "wb");
    for (unsigned i = 0; i < 5000; ++i) std::fputc(static_cast<int>(i * 7 % 256), file);
    std::fclose(file);
    return save_result;
}
const char* number(unsigned n) { return reinterpret_cast<const char*>(static_cast<Address>(n)); }
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
    case 0x8c040c78: return reinterpret_cast<Address>(&rename_file);
    case 0x8fed00ae: return reinterpret_cast<Address>(&close_file);
    case 0x8d599074: return reinterpret_cast<Address>(&stock_save);
    }
    std::abort();
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    saving = argv[1];
    save_result = static_cast<unsigned>(std::atoi(argv[2]));
    return sdk_project_save(nullptr, saving) == save_result ? 0 : 1;
}
