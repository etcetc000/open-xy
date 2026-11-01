#include "collection_probe.hpp"

namespace opxy { namespace collection {
namespace {
const u32 constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
u32 rotate(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }
u32 big(const unsigned char* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
u32 little(const unsigned char* p) { return u32(p[3]) << 24 | u32(p[2]) << 16 | u32(p[1]) << 8 | p[0]; }
bool equal(const unsigned char* a, const unsigned char* b, u32 n) {
    unsigned char difference = 0;
    while (n--) difference |= *a++ ^ *b++;
    return !difference;
}
bool exact(Reader r, unsigned char* p, u32 bytes) {
    while (bytes) {
        const u32 n = r.read(r.context, p, bytes);
        if (!n || n > bytes) return false;
        p += n; bytes -= n;
    }
    return true;
}
}

void Sha256::begin() {
    const u32 initial[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                            0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (u32 i = 0; i < 8; ++i) state[i] = initial[i];
    bytes = used = 0;
}
void Sha256::compress() {
    u32 w[64];
    for (u32 i = 0; i < 16; ++i) w[i] = big(block + i * 4);
    for (u32 i = 16; i < 64; ++i) {
        const u32 x = w[i - 15], y = w[i - 2];
        w[i] = w[i - 16] + (rotate(x,7) ^ rotate(x,18) ^ (x >> 3)) + w[i - 7] +
               (rotate(y,17) ^ rotate(y,19) ^ (y >> 10));
    }
    u32 a=state[0], b=state[1], c=state[2], d=state[3];
    u32 e=state[4], f=state[5], g=state[6], h=state[7];
    for (u32 i = 0; i < 64; ++i) {
        const u32 t = h + (rotate(e,6) ^ rotate(e,11) ^ rotate(e,25)) +
                      ((e & f) ^ (~e & g)) + constants[i] + w[i];
        const u32 s = (rotate(a,2) ^ rotate(a,13) ^ rotate(a,22)) + ((a & b) ^ (a & c) ^ (b & c));
        h=g; g=f; f=e; e=d+t; d=c; c=b; b=a; a=t+s;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}
void Sha256::add(const unsigned char* data, u32 count) {
    bytes += count;
    while (count--) {
        block[used++] = *data++;
        if (used == 64) { compress(); used = 0; }
    }
}
void Sha256::finish(unsigned char* output) {
    const u32 high = bytes >> 29, low = bytes << 3;
    block[used++] = 0x80;
    if (used > 56) { while (used < 64) block[used++] = 0; compress(); used = 0; }
    while (used < 56) block[used++] = 0;
    for (u32 i = 0; i < 4; ++i) {
        block[56+i] = high >> (24-i*8); block[60+i] = low >> (24-i*8);
    }
    compress();
    for (u32 i = 0; i < 32; ++i) output[i] = state[i/4] >> (24-(i%4)*8);
}

Inspection inspect(Reader reader, const Limits& limits, Workspace& workspace) {
    Inspection out = {};
    unsigned char* header = workspace.header;
    out.status = truncated;
    if (!reader.read || !reader.failed || !reader.ended) { out.status = io_error; return out; }
    if (!exact(reader, header, header_bytes)) {
        if (reader.failed(reader.context)) out.status = io_error;
        return out;
    }
    const unsigned char magic[8] = {'O','X','Y','P','R','O','B','1'};
    out.status = malformed;
    if (!equal(header, magic, 8) || little(header+8) != 1 || little(header+12) != header_bytes ||
        little(header+36) != 1) return out;
    for (u32 i = 104; i < header_bytes; ++i) if (header[i]) return out;
    out.status = incompatible;
    if (!equal(header+40, limits.platform, 32)) return out;
    out.initialized = little(header+20); out.bss_offset = little(header+24);
    out.bss_bytes = little(header+28); out.text_bytes = little(header+32);
    out.status = outside_pool;
    // Subtractions keep malicious sizes from wrapping before the comparison.
    if (little(header+16) != limits.base || limits.base > 0xffffffffu - limits.bytes ||
        !out.text_bytes || out.text_bytes > out.initialized || out.initialized > limits.bytes ||
        out.bss_offset < out.initialized || out.bss_offset > limits.bytes ||
        out.bss_bytes > limits.bytes - out.bss_offset || (out.bss_offset & 3) || (out.bss_bytes & 3))
        return out;
    for (u32 i = 0; i < 32; ++i) { out.digest[i] = header[72+i]; header[72+i] = 0; }
    Sha256& hash = workspace.hash; hash.begin(); hash.add(header, header_bytes);
    unsigned char* chunk = workspace.chunk;
    u32 remaining = out.initialized;
    out.status = truncated;
    while (remaining) {
        const u32 n = remaining < sizeof workspace.chunk ? remaining : sizeof workspace.chunk;
        if (!exact(reader, chunk, n)) {
            if (reader.failed(reader.context)) out.status = io_error;
            return out;
        }
        hash.add(chunk, n); remaining -= n;
    }
    out.status = trailing;
    if (reader.read(reader.context, chunk, 1)) return out;
    if (reader.failed(reader.context) || !reader.ended(reader.context)) { out.status = io_error; return out; }
    unsigned char digest[32]; hash.finish(digest);
    out.status = equal(digest, out.digest, 32) ? valid : checksum;
    return out;
}
}}
