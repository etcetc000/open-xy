// Checks behind every machine's stock registration (see registration.hpp).
// Stock state is single-threaded on these paths. Stock calls may allocate, log
// or fail without rolling back, so each result is checked again afterwards.
#include "registration.hpp"

namespace opxy { namespace platform { namespace registry {
namespace {
u32 value(const void* symbol) { return static_cast<u32>(reinterpret_cast<Address>(symbol)); }

const u32 kRecordWords = 12, kNameAt = 16, kNameBytes = 32;

// 1-15 characters, or 0 for a name a stock string cannot hold inline.
u32 name_length(const char* name) {
    for (u32 n = 0; n < 16; ++n)
        if (!name[n]) return n;
    return 0;
}

bool ids_valid(const Registry& registry, const Pages& pages, u32 id, u32 page) {
    return id < registry.limit && page >= 1 && page < pages.limit;
}

const volatile u32* record_at(const Registry& registry, u32 id) {
    return at<const volatile u32>(registry.records) + id * kRecordWords;
}
u32 resolve(const Registry& registry, const StockString* key) {
    return at<u32(const StockString*)>(registry.resolve)(key);
}

// Exact full-record match. Does not dereference the factory or callbacks.
bool owned(const volatile u32* record, const Record& r, u32 page) {
    if (record[0] != value(r.factory) || record[1] != value(r.parameter_name) || record[2] != r.flag ||
        record[3] != page)
        return false;
    const volatile unsigned char* name = reinterpret_cast<const volatile unsigned char*>(record) + kNameAt;
    const u32 length = name_length(r.name);
    for (u32 i = 0; i < kNameBytes; ++i)
        if (name[i] != (i < length ? static_cast<unsigned char>(r.name[i]) : 0)) return false;
    return true;
}

bool empty(const volatile u32* record) {
    for (u32 i = 0; i < kRecordWords; ++i)
        if (record[i]) return false;
    return true;
}

// Object words 0, 1 and 3 are this machine's vtable, asset and page; word 4 is
// the loader stock fills in (artwork may still fail to load later).
bool identity(const Screen& screen, u32 page) {
    const volatile u32* object = reinterpret_cast<const volatile u32*>(screen.state);
    return object[0] == value(screen.vtable) && object[1] == value(screen.asset) && object[3] == page &&
           object[4] != 0;
}
}

int add(const Registry& registry, const Pages& pages, const Record& r) {
    const u32 id = value(r.id), page = value(r.page), length = name_length(r.name);
    if (!ids_valid(registry, pages, id, page) || !length) return -1;
    const volatile u32* record = record_at(registry, id);
    const bool ours = !empty(record);
    if (ours && !owned(record, r, page)) return -2;
    if (!at<const volatile u32>(registry.map)[1]) return -3;
    StockString key = {};
    for (u32 i = 0; i < length; ++i) key.text[i] = r.name[i];
    key.length = length;
    key.capacity = 15;
    const u32 mapped = resolve(registry, &key);
    if (mapped == id) {
        if (ours) return 2;
    } else if (mapped != registry.missing) {
        return -4;
    } else if (ours) {
        return -5;
    }
    registry.add(r, id, page);
    return owned(record, r, page) && resolve(registry, &key) == id ? 1 : -5;
}

int add_screen(const Pages& pages, const Record& r, const Screen& screen) {
    const u32 page = value(r.page);
    if (page < 1 || page >= pages.limit) return 0;
    char* volatile* slot = at<char* volatile*()>(pages.slots)() + page;
    if (*slot != screen.state) {
        // An empty slot does not authorize reusing a live object: the whole
        // object must be unused. A prior construction, another ID or partial
        // state fails.
        if (*slot) return 0;
        const volatile u32* object = reinterpret_cast<const volatile u32*>(screen.state);
        for (u32 i = 0; i < screen.bytes / 4; ++i)
            if (object[i]) return 0;
        if (screen.construct(screen.state, page) != screen.state || *slot != screen.state) return 0;
    }
    return identity(screen, page);
}

int ready(const Registry& registry, const Pages& pages, const Record& r, const Screen* screen) {
    const u32 id = value(r.id), page = value(r.page);
    if (!ids_valid(registry, pages, id, page) || !owned(record_at(registry, id), r, page)) return 0;
    if (!screen) return 1;  // The engine shows a stock page.
    if (!*at<const volatile unsigned char>(pages.initialized) || at<char* volatile>(pages.table)[page] != screen->state)
        return 0;
    const volatile u32* object = reinterpret_cast<const volatile u32*>(screen->state);
    return identity(*screen, page) && object[2] != 0;
}

int known(const Registry& registry, const StockString* name) {
    if (!at<const volatile u32>(registry.map)[1]) return 1;
    return resolve(registry, name) != registry.missing;
}

// Honors inline and heap storage without reading past the length.
int name_matches(const Record& r, const StockString* string) {
    const u32 length = name_length(r.name);
    if (!length || string->length != length || string->capacity < length) return 0;
    const char* text = string->text;
    if (string->capacity >= 16) {
        __builtin_memcpy(&text, string->text, sizeof text);
        if (!text) return 0;
    }
    for (u32 i = 0; i < length; ++i)
        if (text[i] != r.name[i]) return 0;
    return 1;
}
}}}
