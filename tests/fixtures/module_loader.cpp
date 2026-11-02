#include "module_loader.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace opxy;
namespace m = opxy::modules;
namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
struct Context { u32 allocated, released, syncs, sync_address, sync_bytes; bool fail_allocate, fail_sync; };
unsigned char* allocate(void* context, u32 bytes) {
    Context& c = *static_cast<Context*>(context);
    if (c.fail_allocate) return nullptr;
    ++c.allocated;
    // Deliberately misaligned, so the loader has to align the base itself.
    unsigned char* block = static_cast<unsigned char*>(std::malloc(bytes + 5));
    std::memset(block, 0xa5, bytes + 5);
    return block + 5;
}
void release(void* context, unsigned char* block) { ++static_cast<Context*>(context)->released; std::free(block - 5); }
bool synchronize(void* context, u32 address, u32 bytes) {
    Context& c = *static_cast<Context*>(context);
    ++c.syncs; c.sync_address = address; c.sync_bytes = bytes;
    return !c.fail_sync;
}
void put32(std::vector<unsigned char>& v, u32 at, u32 x) { for (u32 i = 0; i < 4; ++i) v[at + i] = static_cast<unsigned char>(x >> (8 * i)); }
u32 get32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24; }
u32 get16(const unsigned char* p) { return p[0] | p[1] << 8; }

struct Spec {
    u32 image_bytes = 16, bss_bytes = 8, init_offset = 12, os = m::os_1_2_1, format = m::format, size = m::trailer_bytes;
    std::vector<m::Reloc> relocs = {{m::abs32 << 28 | 0, 20}, {m::hi16 << 28 | 4, 8}, {m::lo16 << 28 | 6, 8}};
    bool corrupt_crc = false, corrupt_magic = false;
};

// Image: word 0 relocated in full, word 1 as a hi/lo pair, words 2-3 plain.
std::vector<unsigned char> build(const Spec& s) {
    const u32 image_offset = 0, reloc_offset = s.image_bytes;
    std::vector<unsigned char> f(s.image_bytes + 8 * s.relocs.size() + m::trailer_bytes, 0);
    for (u32 i = 8; i < s.image_bytes; ++i) f[i] = static_cast<unsigned char>(i);
    for (u32 i = 0; i < s.relocs.size(); ++i) { put32(f, reloc_offset + 8 * i, s.relocs[i].where); put32(f, reloc_offset + 8 * i + 4, s.relocs[i].target); }
    const u32 crc = m::crc32(m::crc32(0, f.data(), s.image_bytes), f.data() + reloc_offset, 8 * u32(s.relocs.size()));
    const u32 t = u32(f.size()) - m::trailer_bytes;
    put32(f, t + 4, image_offset); put32(f, t + 8, s.image_bytes); put32(f, t + 12, s.bss_bytes);
    put32(f, t + 16, reloc_offset); put32(f, t + 20, u32(s.relocs.size())); put32(f, t + 24, s.init_offset);
    put32(f, t + 28, s.corrupt_crc ? crc ^ 1 : crc); put32(f, t + 32, s.os);
    f[t + 36] = static_cast<unsigned char>(s.format); f[t + 38] = static_cast<unsigned char>(s.size);
    std::memcpy(&f[t + 40], m::magic, 8);
    if (s.corrupt_magic) f[t + 40] ^= 1;
    return f;
}

m::Status run(const Spec& s, Context& c, m::Module& out) {
    const std::vector<unsigned char> f = build(s);
    return m::stage(f.data(), u32(f.size()), m::os_1_2_1, m::Memory{&c, allocate, release, synchronize}, out);
}

void expect(const Spec& s, m::Status status, const char* message) {
    Context c = {}; m::Module out;
    require(run(s, c, out) == status, message);
    require(c.allocated == c.released, "memory leaked on failure");
    require(!out.block && !out.start && !out.init, "failed module returned");
}
}

int stage_file(const char* path) {
    // A real module from tooling/build_module.py: must stage, nothing more.
    std::FILE* file = std::fopen(path, "rb");
    require(file != nullptr, "cannot open module file");
    std::vector<unsigned char> data;
    for (int c; (c = std::fgetc(file)) != EOF;) data.push_back(static_cast<unsigned char>(c));
    std::fclose(file);
    Context c = {}; m::Module out;
    require(m::stage(data.data(), u32(data.size()), m::os_1_2_1, m::Memory{&c, allocate, release, synchronize}, out) == m::loaded,
            "built module refused");
    require(c.syncs == 1 && out.init >= out.base && out.init < out.base + out.bytes, "built module staged wrongly");
    release(&c, out.block);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1) return stage_file(argv[1]);
    {
        Spec s; Context c = {}; m::Module out;
        require(run(s, c, out) == m::loaded, "valid module refused");
        require(!(out.base & 31) && out.bytes == 32, "base or extent wrong");
        require(c.syncs == 1 && c.sync_address == out.base && c.sync_bytes == 32, "caches not synchronized over the module");
        const unsigned char* p = out.start;
        require(static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(p)) == out.base, "base differs from start");
        require(get32(p) == out.base + 20, "abs32 wrong");
        require(get16(p + 4) == (out.base + 8) >> 16 && get16(p + 6) == ((out.base + 8) & 0xffff), "hi16/lo16 wrong");
        for (u32 i = 8; i < 16; ++i) require(p[i] == i, "image not copied");
        for (u32 i = 16; i < 32; ++i) require(p[i] == 0, "bss or padding not zeroed");
        require(out.init == out.base + 12, "init address wrong");
        release(&c, out.block);
    }
    { Spec s; s.relocs.clear(); s.bss_bytes = 0; Context c = {}; m::Module out;
      require(run(s, c, out) == m::loaded, "module without relocs refused"); release(&c, out.block); }

    { Spec s; s.corrupt_magic = true; expect(s, m::bad_trailer, "bad magic admitted"); }
    { Spec s; s.format = 1; expect(s, m::bad_trailer, "unknown format admitted"); }
    { Spec s; s.size = 40; expect(s, m::bad_trailer, "wrong trailer size admitted"); }
    { Spec s; s.os = 0x00010200; expect(s, m::wrong_os, "other OS admitted"); }
    { Spec s; s.init_offset = 16; expect(s, m::bad_layout, "init outside image admitted"); }
    { Spec s; s.init_offset = 13; expect(s, m::bad_layout, "odd init admitted"); }
    { Spec s; s.image_bytes = 14; expect(s, m::bad_layout, "unaligned image admitted"); }
    { Spec s; s.bss_bytes = 0x01000004u; expect(s, m::bad_layout, "huge bss admitted"); }
    { Spec s; s.corrupt_crc = true; expect(s, m::bad_crc, "bad CRC admitted"); }
    { Spec s; s.relocs[0].target = 25; expect(s, m::bad_reloc, "reloc target past module admitted"); }
    { Spec s; s.relocs[0].where = m::abs32 << 28 | 14; expect(s, m::bad_reloc, "abs32 past image admitted"); }
    { Spec s; s.relocs[1].where = 4u << 28 | 4; expect(s, m::bad_reloc, "unknown reloc type admitted"); }
    { Spec s; Context c = {}; c.fail_allocate = true; m::Module out;
      require(run(s, c, out) == m::no_space && !out.block, "allocation failure not reported"); }
    { Spec s; Context c = {}; c.fail_sync = true; m::Module out;
      require(run(s, c, out) == m::io_error && c.allocated == c.released, "cache failure admitted"); }
    {
        Context c = {}; m::Module out; const unsigned char small[8] = {};
        require(m::stage(small, sizeof small, m::os_1_2_1, m::Memory{&c, allocate, release, synchronize}, out) == m::short_file,
                "short file admitted");
        require(m::stage(nullptr, 0, m::os_1_2_1, m::Memory{&c, allocate, release, synchronize}, out) == m::short_file,
                "missing data admitted");
    }
    {
        // Truncation anywhere leaves no valid trailer at the end.
        Spec s; const std::vector<unsigned char> f = build(s);
        for (u32 cut = 1; cut < f.size(); ++cut) {
            Context c = {}; m::Module out;
            require(m::stage(f.data(), u32(f.size()) - cut, m::os_1_2_1, m::Memory{&c, allocate, release, synchronize}, out) != m::loaded,
                    "truncated module admitted");
            require(c.allocated == c.released, "memory leaked on truncation");
        }
    }
    require(m::crc32(0, reinterpret_cast<const unsigned char*>("123456789"), 9) == 0xcbf43926u, "CRC-32 check value");
    return 0;
}
