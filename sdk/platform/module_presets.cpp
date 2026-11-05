#include "module_presets.hpp"
namespace opxy { namespace modules {
namespace {
const char root[] = "content/presets";

// The rest of text after prefix, or null.
const char* after(const char* text, const char* prefix) {
    if (!text || !prefix) return nullptr;
    while (*prefix) if (*text++ != *prefix++) return nullptr;
    return text;
}
bool equal(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
// The module with presets whose ID text starts with, followed by end; the rest in rest.
bool owner(const PresetModules& all, const char* text, char end, u32& module, const char*& rest) {
    for (u32 i = 0; i < all.count; ++i) {
        if (!all.modules[i].count) continue;
        const char* p = after(text, all.modules[i].id);
        if (p && *p == end) { module = i; rest = p; return true; }
    }
    return false;
}
void write(unsigned char* entry, u32 type, const char* name, const char* suffix) {
    for (u32 i = 0; i < entry_bytes; ++i) entry[i] = 0;
    entry[entry_type] = static_cast<unsigned char>(type);
    u32 n = entry_name;
    for (const char* c = name; *c && n + 1 < entry_bytes; ++c) entry[n++] = static_cast<unsigned char>(*c);
    for (const char* c = suffix; *c && n + 1 < entry_bytes; ++c) entry[n++] = static_cast<unsigned char>(*c);
}
}

bool preset_file(const PresetModules& all, const char* path, u32& module, u32& preset) {
    if (const char* p = after(path, "/yaffs2/")) path = p;
    const char* p = after(path, root);
    const char* rest;
    if (!p || *p != '/' || !owner(all, p + 1, '/', module, rest)) return false;
    const PresetModule& m = all.modules[module];
    for (preset = 0; preset < m.count; ++preset) {
        const char* q = after(rest + 1, m.names[preset]);
        if (q && equal(q, ".json")) return true;
    }
    return false;
}

u32 preset_folder(const PresetModules& all, const char* path, u32& module) {
    const char* p = after(path, root);
    const char* rest;
    if (!p) return folder_stock;
    if (!*p) return folder_root;
    return *p == '/' && owner(all, p + 1, 0, module, rest) ? folder_module : folder_stock;
}

bool replaced(const PresetModules& all, const unsigned char* entry) {
    u32 module;
    const char* rest;
    return entry && entry[entry_type] == entry_folder &&
        owner(all, reinterpret_cast<const char*>(entry + entry_name), 0, module, rest);
}

bool next_entry(const PresetModules& all, PresetFolder& folder, unsigned char* entry) {
    if (folder.kind == folder_root) {
        while (folder.next < all.count) {
            const PresetModule& m = all.modules[folder.next++];
            if (m.count) { write(entry, entry_folder, m.id, ""); return true; }
        }
        return false;
    }
    if (folder.kind != folder_module || folder.module >= all.count) return false;
    const PresetModule& m = all.modules[folder.module];
    if (folder.next >= m.count) return false;
    write(entry, entry_file, m.names[folder.next++], ".json");
    return true;
}
}}
