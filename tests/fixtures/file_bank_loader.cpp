// Host check of file_bank_loader.hpp: an in-memory backend, every failure path,
// and a shared bank published to two views. Prints "ok" when every case holds.
#include "file_bank_loader.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace opxy;
namespace fb = opxy::file_banks;

static int failures;
#define CHECK(x) do { if (!(x)) { std::printf("failed: %s (line %d)\n", #x, __LINE__); ++failures; } } while (0)

struct Memory {
    std::vector<u8> file;
    bool exists = true, fail_read_at_payload = false, fail_close = false, null_alloc = false, bad_pointer = false;
    u32 position = 0, opened = 0, closed = 0, allocated = 0, released = 0;
    std::vector<void*> blocks;
};

struct Backend {
    Memory* m;
    void* open(const char*) {
        if (!m->exists) return nullptr;
        ++m->opened;
        m->position = 0;
        return m;
    }
    i32 read(void*, void* data, u32 bytes) {
        if (m->fail_read_at_payload && m->position >= fb::kHeaderBytes) return -1;
        const u32 left = u32(m->file.size()) - m->position, n = bytes < left ? bytes : left;
        std::memcpy(data, m->file.data() + m->position, n);
        m->position += n;
        return i32(n);
    }
    bool close(void*) { ++m->closed; return !m->fail_close; }
    void* allocate(u32 bytes) {
        if (m->null_alloc) return nullptr;
        void* p = std::aligned_alloc(32, (bytes + 31) / 32 * 32);
        ++m->allocated;
        m->blocks.push_back(p);
        return p;
    }
    bool valid(const void*, u32) { return !m->bad_pointer; }
    void release(void* p) { ++m->released; std::free(p); }
    void publish_barrier() {}
};

static std::vector<u8> payload(u32 words) {
    std::vector<u8> bytes(words * 4);
    for (u32 i = 0; i < bytes.size(); ++i) bytes[i] = u8(i * 7 + 3);
    return bytes;
}

static void put32(std::vector<u8>& v, u32 at, u32 x) { for (u32 i = 0; i < 4; ++i) v[at + i] = u8(x >> (8 * i)); }

static std::vector<u8> container(const std::vector<u8>& data, u8* digest) {
    fb::Sha256 h;
    h.begin();
    h.add(data.data(), u32(data.size()));
    h.finish(digest);
    std::vector<u8> out(fb::kHeaderBytes, 0);
    std::memcpy(out.data(), "OPXYHB01", 8);
    put32(out, 8, 1); put32(out, 12, fb::kHeaderBytes);
    std::memcpy(out.data() + 16, "shared", 6);
    std::memcpy(out.data() + 48, "bank", 4);
    put32(out, 80, u32(data.size())); put32(out, 84, u32(data.size()) / 4); put32(out, 88, 1);
    std::memcpy(out.data() + 96, digest, 32);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

struct Case {
    Memory memory;
    fb::Spec spec;
    fb::Loader<Backend> loader{};
    u32 charged = 0;
    Case() {
        const std::vector<u8> words = payload(1024 + 3);  // more than one 4096-byte chunk
        memory.file = container(words, spec.sha256);
        spec.path = "/yaffs2/content/banks/shared/x-bank.hb";
        spec.bytes = u32(words.size());
    }
    fb::Status run(u32 budget = 1u << 20) {
        Backend io{&memory};
        return loader.load(io, spec, charged, budget);
    }
    // Nothing may stay allocated or charged after a failure.
    void clean(fb::Status expected) {
        CHECK(loader.status == expected);
        CHECK(memory.allocated == memory.released);
        CHECK(charged == 0);
        CHECK(memory.opened == memory.closed);
    }
};

int main() {
    {  // A good file loads and publishes to two views that share it.
        Case c;
        CHECK(c.run() == fb::prepared);
        CHECK(c.charged == c.spec.bytes + fb::kAllowance);
        FileBank a = {nullptr, c.spec.bytes / 4}, b = {nullptr, c.spec.bytes / 4}, wrong = {nullptr, 1};
        Backend io{&c.memory};
        CHECK(c.loader.publish(io, a) && c.loader.publish(io, b));
        CHECK(a.active && a.active == b.active && !std::memcmp(a.active, c.memory.file.data() + 128, c.spec.bytes));
        CHECK(!c.loader.publish(io, wrong) && !wrong.active);
        CHECK(!c.loader.publish(io, a));  // already active: never replaced
        CHECK(c.memory.released == 0 && c.memory.closed == 1);
        std::free(c.memory.blocks[0]);
    }
    { Case c; c.memory.exists = false; c.run(); c.clean(fb::open_failed); }
    { Case c; c.memory.file.resize(c.memory.file.size() - 1); c.run(); c.clean(fb::size_mismatch); }
    { Case c; c.memory.file.push_back(0); c.run(); c.clean(fb::size_mismatch); }
    { Case c; c.memory.file.resize(100); c.run(); c.clean(fb::size_mismatch); }
    { Case c; c.memory.file[0] = 'X'; c.run(); c.clean(fb::invalid); }
    { Case c; c.memory.file[16] = 'x'; c.run(); c.clean(fb::identity_mismatch); }
    { Case c; c.memory.file[100] ^= 1; c.run(); c.clean(fb::hash_mismatch); }
    { Case c; c.memory.file[200] ^= 1; c.run(); c.clean(fb::hash_mismatch); }  // payload no longer matches
    { Case c; c.memory.fail_read_at_payload = true; c.run(); c.clean(fb::read_failed); }
    { Case c; c.memory.fail_close = true; c.run(); c.clean(fb::close_failed); }
    { Case c; c.memory.null_alloc = true; c.run(); c.clean(fb::no_memory); }
    { Case c; c.memory.bad_pointer = true; c.run(); CHECK(c.loader.status == fb::bad_pointer && c.charged == 0);
      for (void* p : c.memory.blocks) std::free(p); }  // never released through the backend
    { Case c; c.run(c.spec.bytes); c.clean(fb::over_budget); }
    { Case c; c.spec.bytes += 4; c.run(); c.clean(fb::size_mismatch); }
    {  // A failed bank never touches a view.
        Case c;
        c.memory.file[300] ^= 1;
        c.run();
        FileBank view = {nullptr, c.spec.bytes / 4};
        Backend io{&c.memory};
        CHECK(!c.loader.publish(io, view) && !view.active);
    }
    {  // Two banks share one budget; the second does not fit.
        Case first, second;
        first.run();
        second.charged = first.charged;
        second.run(first.charged + second.spec.bytes);
        CHECK(second.loader.status == fb::over_budget && second.charged == first.charged);
        std::free(first.memory.blocks[0]);
    }
    {  // SHA-256 of "abc".
        fb::Sha256 h;
        u8 d[32];
        h.begin();
        h.add(reinterpret_cast<const u8*>("abc"), 3);
        h.finish(d);
        CHECK(d[0] == 0xba && d[1] == 0x78 && d[31] == 0xad);
    }
    if (!failures) std::printf("ok\n");
    return failures ? 1 : 0;
}
