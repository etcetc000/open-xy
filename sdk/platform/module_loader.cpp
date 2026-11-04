#include "module_loader.hpp"
namespace opxy { namespace modules {
namespace {
u32 get32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24; }
u16 get16(const unsigned char* p) { return u16(p[0] | p[1] << 8); }
void put16(unsigned char* p, u32 v) { p[0] = static_cast<unsigned char>(v); p[1] = static_cast<unsigned char>(v >> 8); }
void put32(unsigned char* p, u32 v) { put16(p, v); put16(p + 2, v >> 16); }

struct Trailer {
    u32 manifest_bytes, image_offset, image_bytes, bss_bytes, reloc_offset, reloc_count, init_offset, crc, os;
    u16 format, size;
};

Trailer parse(const unsigned char* p) {
    Trailer t;
    t.manifest_bytes = get32(p); t.image_offset = get32(p + 4); t.image_bytes = get32(p + 8); t.bss_bytes = get32(p + 12);
    t.reloc_offset = get32(p + 16); t.reloc_count = get32(p + 20); t.init_offset = get32(p + 24);
    t.crc = get32(p + 28); t.os = get32(p + 32); t.format = get16(p + 36); t.size = get16(p + 38);
    return t;
}
}

u32 crc32(u32 crc, const unsigned char* data, u32 bytes) {
    crc = ~crc;
    while (bytes--) {
        crc ^= *data++;
        for (int k = 0; k < 8; ++k) crc = crc >> 1 ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

u32 array_end(const unsigned char* text, u32 bytes, const char* key) {
    u32 key_bytes = 0;
    while (key && key[key_bytes]) ++key_bytes;
    if (!text || !key_bytes) return 0;
    for (u32 at = 0; at + key_bytes < bytes; ++at) {
        u32 k = 0;
        while (k < key_bytes && text[at + k] == static_cast<unsigned char>(key[k])) ++k;
        if (k < key_bytes) continue;
        u32 i = at + key_bytes;
        while (i < bytes && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t' || text[i] == ':')) ++i;
        if (i >= bytes || text[i] != '[') return 0;
        u32 depth = 0;
        bool quoted = false, escaped = false;
        for (; i < bytes; ++i) {
            const unsigned char c = text[i];
            if (quoted) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
            } else if (c == '"') quoted = true;
            else if (c == '[') ++depth;
            else if (c == ']' && --depth == 0) return i;
        }
        return 0;
    }
    return 0;
}

namespace {
// The exported address for the 0-terminated name at names + at, or 0: also
// when the name runs past the names.
u32 resolve(const unsigned char* names, u32 names_bytes, u32 at, const Exports& exports) {
    u32 end = at;
    while (end < names_bytes && names[end]) ++end;
    if (end >= names_bytes || end == at) return 0;
    for (u32 e = 0; e < exports.count; ++e) {
        const char* name = exports.table[e].name;
        u32 i = 0;
        while (at + i < end && name[i] && static_cast<unsigned char>(name[i]) == names[at + i]) ++i;
        if (at + i == end && !name[i]) return exports.table[e].address;
    }
    return 0;
}

bool terminated(const unsigned char* names, u32 names_bytes, u32 at) {
    for (u32 i = at; i < names_bytes; ++i) if (!names[i]) return i > at;
    return false;
}
}

Status stage(const unsigned char* data, u32 bytes, u32 os, const Memory& memory, const Exports& exports, Module& out) {
    out = Module{nullptr, nullptr, 0, 0, 0};
    if (!data || bytes < trailer_bytes) return short_file;
    const unsigned char* tail = data + bytes - trailer_bytes;
    for (u32 i = 0; i < 8; ++i) if (tail[40 + i] != magic[i]) return bad_trailer;
    const Trailer t = parse(tail);
    if (t.format != format || t.size != trailer_bytes) return bad_trailer;
    if (t.os != os) return wrong_os;
    const u32 end = bytes - trailer_bytes;
    if ((t.image_offset | t.image_bytes) & 3 || !t.image_bytes ||
        t.image_offset > end || t.image_bytes > end - t.image_offset || t.manifest_bytes > t.image_offset ||
        t.reloc_offset > end || t.reloc_count > (end - t.reloc_offset) / sizeof(Reloc) ||
        t.init_offset >= t.image_bytes || (t.init_offset & 1) ||
        t.bss_bytes > 0x01000000u || t.image_bytes > 0x01000000u) return bad_layout;
    const unsigned char* image = data + t.image_offset;
    const unsigned char* relocs = data + t.reloc_offset;
    const u32 reloc_bytes = t.reloc_count * u32(sizeof(Reloc));
    const unsigned char* names = relocs + reloc_bytes;
    const u32 names_bytes = end - (t.reloc_offset + reloc_bytes);
    if (crc32(crc32(crc32(0, image, t.image_bytes), relocs, reloc_bytes), names, names_bytes) != t.crc) return bad_crc;

    const u32 need = t.image_bytes + t.bss_bytes;
    const u32 rounded = (need + line_bytes - 1) & ~(line_bytes - 1);
    unsigned char* block = memory.allocate ? memory.allocate(memory.context, rounded + line_bytes - 1) : nullptr;
    if (!block) return no_space;
    typedef __UINTPTR_TYPE__ Address;
    const Address aligned = (reinterpret_cast<Address>(block) + line_bytes - 1) & ~Address(line_bytes - 1);
    unsigned char* target = reinterpret_cast<unsigned char*>(aligned);
    const u32 base = static_cast<u32>(aligned);

    for (u32 i = 0; i < t.image_bytes; ++i) target[i] = image[i];
    for (u32 i = t.image_bytes; i < rounded; ++i) target[i] = 0;
    for (u32 i = 0; i < t.reloc_count; ++i) {
        const u32 where = get32(relocs + i * 8), value = get32(relocs + i * 8 + 4);
        const u32 offset = where & 0x0fffffffu, type = where >> 28;
        const u32 width = type == abs32 || type == import ? 4 : 2;
        if (offset > t.image_bytes - width ||
            (type == import ? (offset & 3) || !terminated(names, names_bytes, value) : value > need) ||
            (type != abs32 && type != hi16 && type != lo16 && type != import)) {
            memory.release(memory.context, block);
            return bad_reloc;
        }
        if (type == import) {
            const u32 address = resolve(names, names_bytes, value, exports);
            if (!address) {
                memory.release(memory.context, block);
                return missing_import;
            }
            put32(target + offset, address);
            continue;
        }
        const u32 address = base + value;
        if (type == abs32) put32(target + offset, address);
        else put16(target + offset, type == hi16 ? address >> 16 : address & 0xffffu);
    }
    if (!memory.synchronize || !memory.synchronize(memory.context, base, rounded)) {
        memory.release(memory.context, block);
        return io_error;
    }
    out = Module{block, target, base, rounded, base + t.init_offset};
    return loaded;
}
}}
