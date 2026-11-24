// Stock OS 1.2.1 file access for track control's saved data
// (track_control_project.cpp), in a namespace of its own: the project companion
// (project_companion.cpp) keeps its own copy, and host tests build both in one unit.
#pragma once
#include "stock.hpp"

namespace kept_files { namespace {
using opxy::u32;
using opxy::platform::stock;

typedef void File;
// newlib, as stock's project reader and writer call it (8d590d7c, 8d590e28, 8d591304, 8d590da2).
File* file_open(const char* path, const char* mode) {
    return stock<File*(const char*, const char*)>(0x8fed0348)(path, mode);
}
u32 file_read(void* data, u32 size, u32 count, File* file) {
    return stock<u32(void*, u32, u32, File*)>(0x8fed05bc)(data, size, count, file);
}
u32 file_write(const void* data, u32 size, u32 count, File* file) {
    return stock<u32(const void*, u32, u32, File*)>(0x8fed0780)(data, size, count, file);
}
void file_close(File* file) { stock<int(File*)>(0x8fed00ae)(file); }

const u32 kPathBytes = 256;
unsigned char chunk[4096];
char companion[kPathBytes];
u32 crc_table[256];

// zlib's CRC-32.
u32 crc32(u32 crc, const unsigned char* data, u32 count) {
    if (!crc_table[1]) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int bit = 0; bit < 8; ++bit) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    }
    crc = ~crc;
    while (count--) crc = crc_table[(crc ^ *data++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

// The project's bytes as saved: false when it cannot be read back.
bool measure(const char* path, u32& bytes, u32& crc) {
    File* file = file_open(path, "rb");
    if (!file) return false;
    bytes = crc = 0;
    for (u32 count; (count = file_read(chunk, 1, sizeof chunk, file)) != 0;) {
        crc = crc32(crc, chunk, count);
        bytes += count;
    }
    file_close(file);
    return true;
}

// song.xy -> song.oxy; false for any other name.
bool companion_path(const char* path) {
    u32 length = 0;
    while (path[length]) ++length;
    if (length < 4 || length + 2 >= kPathBytes || path[length - 3] != '.' || path[length - 2] != 'x' ||
        path[length - 1] != 'y')
        return false;
    for (u32 i = 0; i < length - 2; ++i) companion[i] = path[i];
    companion[length - 2] = 'o';
    companion[length - 1] = 'x';
    companion[length] = 'y';
    companion[length + 1] = 0;
    return true;
}
}}
