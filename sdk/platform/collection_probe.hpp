// File inspection only. No executable-memory writes, registration or callbacks.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace collection {
const u32 header_bytes = 128;
enum Status { valid, truncated, malformed, incompatible, outside_pool, checksum, trailing, missing, io_error };
struct Reader {
    void* context;
    u32 (*read)(void*, void*, u32);
    bool (*failed)(void*);
    bool (*ended)(void*);
};
struct Limits { u32 base, bytes; unsigned char platform[32]; };
struct Inspection {
    Status status;
    u32 initialized, bss_offset, bss_bytes, text_bytes;
    unsigned char digest[32];
};
struct Sha256 {
    u32 state[8], bytes, used;
    unsigned char block[64];
    void begin();
    void add(const unsigned char*, u32);
    void finish(unsigned char*);
private:
    void compress();
};
struct Workspace {
    unsigned char header[header_bytes], chunk[1024];
    Sha256 hash;
};
// The reader may return short chunks. EOF is accepted only when ended is true
// and failed is false. At most the bounded payload and one extra byte are read.
Inspection inspect(Reader, const Limits&, Workspace&);
}}
