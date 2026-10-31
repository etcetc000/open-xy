#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform { namespace project_sections {
const u32 byte_limit = 64 * 1024;
const u32 section_limit = 32;
const u32 name_limit = 64;
struct Section { char name[name_limit + 1]; u32 offset, size; };
struct Document {
    u8 bytes[byte_limit];
    Section sections[section_limit];
    u32 size, count;
    void clear() { size = count = 0; }
    i32 find(const char* name) const {
        for (u32 i = 0; i < count; ++i) {
            u32 n = 0;
            while (name[n] && name[n] == sections[i].name[n]) ++n;
            if (!name[n] && !sections[i].name[n]) return static_cast<i32>(i);
        }
        return -1;
    }
    bool record(const char* name, u32 length, u32 offset, u32 size) {
        if (!length || length > name_limit || offset > byte_limit || size > byte_limit - offset) return false;
        char key[name_limit + 1];
        for (u32 i = 0; i < length; ++i) key[i] = name[i];
        key[length] = 0;
        const i32 existing = find(key);
        if (existing < 0 && count == section_limit) return false;
        Section& section = sections[existing < 0 ? count++ : static_cast<u32>(existing)];
        for (u32 i = 0; i <= length; ++i) section.name[i] = key[i];
        section.offset = offset; section.size = size;
        return true;
    }
    // Compact surviving ranges in source order; duplicate keys may have moved.
    void compact() {
        for (u32 i = 0; i < count; ++i)
            for (u32 j = i + 1; j < count; ++j)
                if (sections[j].offset < sections[i].offset) {
                    const Section old = sections[i]; sections[i] = sections[j]; sections[j] = old;
                }
        u32 used = 0;
        for (u32 i = 0; i < count; ++i) {
            Section& section = sections[i];
            for (u32 j = 0; j < section.size; ++j) bytes[used + j] = bytes[section.offset + j];
            section.offset = used; used += section.size;
        }
        size = used;
    }
    void erase(u32 index) {
        if (index >= count) return;
        for (u32 i = index + 1; i < count; ++i) sections[i - 1] = sections[i];
        --count; compact();
    }
};
}}}
