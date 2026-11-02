// Host boundary double for the real project companion reader: stock's newlib file
// calls are host stdio, and stock's deserialize fills a project buffer of stock's
// layout with the patterns the command line gives, or fails.
// project_load <path> <deserialize result> [<track>:<number>,<number>...]...
// prints each given track's pattern numbers after the load, as signed integers.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <project_companion.cpp>

using opxy::platform::Address;

namespace {
const opxy::u32 kProjectBytes = 0x54076c;
const opxy::u32 kTrack = 0x53f44, kPattern = 0x53f4;
unsigned char* project;
int tracks;
char** specs;
unsigned deserialize_result;

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
opxy::u32* field(unsigned track, unsigned pattern) {
    return reinterpret_cast<opxy::u32*>(project + 0xd7c + track * kTrack + pattern * kPattern + 0x18);
}
opxy::u32* count(unsigned track) { return reinterpret_cast<opxy::u32*>(project + 0xd7c + track * kTrack + 0x53f40); }
opxy::u32 stock_deserialize(void*, const char*) {
    for (int i = 0; i < tracks; ++i) {
        char* spec = specs[i];
        const unsigned track = static_cast<unsigned>(std::strtoul(spec, &spec, 10));
        unsigned patterns = 0;
        while (*spec == ':' || *spec == ',') *field(track, patterns++) = static_cast<opxy::u32>(std::strtol(spec + 1, &spec, 10));
        *count(track) = patterns;
    }
    return deserialize_result;
}
const char* number(unsigned n) { return reinterpret_cast<const char*>(static_cast<Address>(n)); }
}

// This firmware: karplus renumbered, pingpong kept, fm2op where the saving firmware had
// timestretch, ringmod at an effect number equal to karplus's old engine number.
const opxy::platform::CompanionMachine sdk_companion_machines[] = {
    {"fm2op", number(35), number(60), false},
    {"karplus", number(41), number(90), false},
    {"macroosc", number(36), number(61), false},
    {"pingpong", number(40), number(21), true},
    {"ringmod", number(36), number(21), true},
    {"skipped", number(0), number(0), false},  // a kit machine left out of the selection
    {nullptr, nullptr, nullptr, false}
};

Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case 0x8fed0348: return reinterpret_cast<Address>(&open_file);
    case 0x8fed05bc: return reinterpret_cast<Address>(&read_file);
    case 0x8fed0780: return reinterpret_cast<Address>(&write_file);
    case 0x8c040c78: return reinterpret_cast<Address>(&rename_file);
    case 0x8fed00ae: return reinterpret_cast<Address>(&close_file);
    case 0x8d599b10: return reinterpret_cast<Address>(&stock_deserialize);
    }
    std::abort();
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    deserialize_result = static_cast<unsigned>(std::atoi(argv[2]));
    tracks = argc - 3;
    specs = argv + 3;
    project = static_cast<unsigned char*>(std::calloc(kProjectBytes, 1));
    if (!project) return 2;
    if (sdk_project_load(project, argv[1]) != deserialize_result) return 1;
    for (int i = 0; i < tracks; ++i) {
        const unsigned track = static_cast<unsigned>(std::strtoul(specs[i], nullptr, 10));
        std::printf("%u:", track);
        for (unsigned k = 0; k < *count(track); ++k)
            std::printf(k ? ",%d" : "%d", static_cast<int>(*field(track, k)));
        std::printf("\n");
    }
    std::free(project);
    return 0;
}
