// Platform-neutral file-bank loading, shared by the native service and host
// tests. Serialized, outside every audio callback: one file at a time, one
// unpublished allocation, publication only after the complete payload has been
// read, its stream hash and an independent re-read of the destination both
// match, and the file has closed. Published data is never replaced or released.
#pragma once
#include <opxy/file_bank.hpp>

namespace opxy { namespace file_banks {

// Streaming SHA-256; used only while loading, never by a bank reader.
struct Sha256 {
    u32 state[8], used, bytes;
    u8 block[64];
    static u32 rotate(u32 x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void transform() {
        static const u32 constants[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        u32 w[64];
        for (unsigned i = 0; i < 16; ++i)
            w[i] = (u32(block[i * 4]) << 24) | (u32(block[i * 4 + 1]) << 16) | (u32(block[i * 4 + 2]) << 8) |
                   block[i * 4 + 3];
        for (unsigned i = 16; i < 64; ++i) {
            const u32 x = w[i - 15], y = w[i - 2];
            w[i] = w[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + w[i - 7] +
                   (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
        }
        u32 a = state[0], b = state[1], c = state[2], d = state[3];
        u32 e = state[4], f = state[5], g = state[6], h = state[7];
        for (unsigned i = 0; i < 64; ++i) {
            const u32 t = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + constants[i] + w[i];
            const u32 s = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t; d = c; c = b; b = a; a = t + s;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    void begin() {
        const u32 initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        for (unsigned i = 0; i < 8; ++i) state[i] = initial[i];
        used = bytes = 0;
    }
    void add(const u8* data, u32 count) {
        bytes += count;
        for (u32 i = 0; i < count; ++i) {
            block[used++] = data[i];
            if (used == 64) { transform(); used = 0; }
        }
    }
    void finish(u8* digest) {
        const u32 bits = bytes * 8;  // payloads are below 1 MiB, so the bit count fits
        block[used++] = 0x80;
        if (used > 56) { while (used < 64) block[used++] = 0; transform(); used = 0; }
        while (used < 60) block[used++] = 0;
        for (unsigned i = 0; i < 4; ++i) block[60 + i] = u8(bits >> (24 - i * 8));
        transform();
        for (unsigned i = 0; i < 32; ++i) digest[i] = u8(state[i / 4] >> (24 - (i % 4) * 8));
    }
};

// One shared file. Its container (tooling/file_banks.py) is a 128-byte header
// (magic, version 1, owner "shared", name "bank", sizes, payload SHA-256)
// followed by the payload, so the verified device-transfer tool accepts it.
struct Spec { const char* path; u32 bytes; u8 sha256[32]; };
enum Status { empty, reading_header, reading_payload, finalizing, prepared, published,
              invalid, open_failed, read_failed, size_mismatch, identity_mismatch, hash_mismatch,
              no_memory, bad_pointer, over_budget, close_failed, live_mismatch };

const u32 kHeaderBytes = 128, kAllocationLimit = 1024u * 1024u, kAllowance = 64, kChunk = 4096;

// Backend: open(path) -> file or null; read(file, data, bytes) -> bytes read,
// 0 at a clean end, -1 on error; close(file) -> success; allocate(bytes) ->
// 32-byte aligned block or null without a fatal error; valid(block, bytes);
// release(block); publish_barrier(). Zero-initialize the loader.
template <class Backend> struct Loader {
    Status status;
    Spec spec;
    void* file;
    u8* data;
    u32 cursor, charge;
    u8 header[kHeaderBytes];
    Sha256 hash;

    bool running() const { return status == reading_header || status == reading_payload || status == finalizing; }
    static u32 le32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
    static bool field(const u8* at, const char* expected) {
        u32 i = 0;
        for (; expected[i]; ++i)
            if (at[i] != u8(expected[i])) return false;
        for (; i < 32; ++i)
            if (at[i]) return false;
        return true;
    }
    Status fail(Backend& io, u32& charged, Status reason) {
        if (file) { io.close(file); file = nullptr; }
        if (data) { io.release(data); data = nullptr; charged -= charge; charge = 0; }
        status = reason;
        return status;
    }
    bool begin(Backend& io, const Spec& expected, u32& charged, u32 budget) {
        if (status != empty) return false;
        if (!expected.path || !*expected.path || !expected.bytes || expected.bytes % 4 ||
            expected.bytes > kAllocationLimit - kAllowance) { status = invalid; return false; }
        if (charged > budget || expected.bytes + kAllowance > budget - charged) { status = over_budget; return false; }
        spec = expected;
        file = io.open(spec.path);
        if (!file) { status = open_failed; return false; }
        cursor = 0;
        status = reading_header;
        return true;
    }
    Status step(Backend& io, u32& charged, u32 budget) {
        if (status == reading_header) {
            const u32 wanted = kHeaderBytes - cursor;
            const i32 n = io.read(file, header + cursor, wanted);
            if (n < 0 || u32(n) > wanted) return fail(io, charged, read_failed);
            if (!n) return fail(io, charged, size_mismatch);
            cursor += u32(n);
            if (cursor != kHeaderBytes) return status;
            const u8 magic[8] = {'O', 'P', 'X', 'Y', 'H', 'B', '0', '1'};
            for (u32 i = 0; i < 8; ++i)
                if (header[i] != magic[i]) return fail(io, charged, invalid);
            if (le32(header + 8) != 1 || le32(header + 12) != kHeaderBytes || le32(header + 88) != 1 ||
                le32(header + 92))
                return fail(io, charged, invalid);
            if (!field(header + 16, "shared") || !field(header + 48, "bank")) return fail(io, charged, identity_mismatch);
            if (le32(header + 80) != spec.bytes || le32(header + 84) != spec.bytes / 4)
                return fail(io, charged, size_mismatch);
            for (u32 i = 0; i < 32; ++i)
                if (header[96 + i] != spec.sha256[i]) return fail(io, charged, hash_mismatch);
            if (charged > budget || spec.bytes + kAllowance > budget - charged) return fail(io, charged, over_budget);
            void* block = io.allocate(spec.bytes);
            if (!block) return fail(io, charged, no_memory);
            // An unexpected pointer is never read, written or freed.
            if (reinterpret_cast<__UINTPTR_TYPE__>(block) % 32 || !io.valid(block, spec.bytes))
                return fail(io, charged, bad_pointer);
            data = static_cast<u8*>(block);
            charge = spec.bytes + kAllowance;
            charged += charge;
            cursor = 0;
            hash.begin();
            status = reading_payload;
            return status;
        }
        if (status == reading_payload) {
            const u32 remaining = spec.bytes - cursor, wanted = remaining < kChunk ? remaining : kChunk;
            const i32 n = io.read(file, data + cursor, wanted);
            if (n < 0 || u32(n) > wanted) return fail(io, charged, read_failed);
            if (!n) return fail(io, charged, size_mismatch);
            hash.add(data + cursor, u32(n));
            cursor += u32(n);
            if (cursor == spec.bytes) status = finalizing;
            return status;
        }
        if (status != finalizing) return status;
        u8 extra;
        const i32 tail = io.read(file, &extra, 1);
        if (tail < 0) return fail(io, charged, read_failed);
        if (tail) return fail(io, charged, size_mismatch);
        void* closed = file;
        file = nullptr;
        if (!io.close(closed)) return fail(io, charged, close_failed);
        u8 digest[32];
        hash.finish(digest);
        for (u32 i = 0; i < 32; ++i)
            if (digest[i] != spec.sha256[i]) return fail(io, charged, hash_mismatch);
        // Read every destination byte again, independently of the stream hash.
        Sha256 live;
        live.begin();
        const volatile u8* memory = data;
        for (u32 i = 0; i < spec.bytes; ++i) {
            const u8 value = memory[i];
            live.add(&value, 1);
        }
        live.finish(digest);
        for (u32 i = 0; i < 32; ++i)
            if (digest[i] != spec.sha256[i]) return fail(io, charged, live_mismatch);
        status = prepared;
        return status;
    }
    // Load the whole file; the result is prepared or a terminal failure.
    Status load(Backend& io, const Spec& expected, u32& charged, u32 budget) {
        if (!begin(io, expected, charged, budget)) return status;
        while (running()) step(io, charged, budget);
        return status;
    }
    // Point a view at prepared data; the data is never released afterwards.
    bool publish(Backend& io, FileBank& view) {
        if ((status != prepared && status != published) || !data || view.active || view.word_count != spec.bytes / 4)
            return false;
        io.publish_barrier();
        view.active = reinterpret_cast<const u32*>(data);
        io.publish_barrier();
        status = published;
        return true;
    }
};

}}  // namespace opxy::file_banks
