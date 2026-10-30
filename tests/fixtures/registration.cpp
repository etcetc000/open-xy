// Host contract checks for checked registration with stock-table doubles.
#include <registration.cpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using namespace opxy::platform;
using namespace opxy::platform::registry;
using opxy::u32;

#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

static const char* address(Address value) { return reinterpret_cast<const char*>(value); }

// Stock doubles: a record table, a name map and the page slots.
static u32 records[63 * 12], effect_records[19 * 12], map_state[2] = {0, 1};
static const char* names[63];
static u32 missing = 33, resolves, adds;
static bool registrar_works = true, ctor_fills = true;
static unsigned char initialized = 1;
static char* volatile slots[128];
static unsigned slot_reads;

static u32 lookup(const StockString* key, u32 limit) {
    ++resolves;
    require(key->zero == 0 && key->capacity == 15 && key->length < 16 && key->text[key->length] == 0);
    for (u32 id = 0; id < limit; ++id)
        if (names[id] && std::strcmp(names[id], key->text) == 0) return id;
    return missing;
}
static u32 stock_resolve(const StockString* key) { return lookup(key, 63); }
static void write_record(u32* record, const Record& r, u32 id, u32 page) {
    ++adds;
    if (!registrar_works) return;
    record[0] = value(r.factory); record[1] = value(r.parameter_name); record[2] = r.flag; record[3] = page;
    std::memset(record + 4, 0, 32);
    std::memcpy(record + 4, r.name, std::strlen(r.name));
    names[id] = r.name;
}
static void add_engine(const Record& r, u32 id, u32 page) { write_record(records + 12 * id, r, id, page); }
static char* volatile* get_slots() { ++slot_reads; return slots; }

static const Registry engines = {reinterpret_cast<Address>(records), 63, 33, reinterpret_cast<Address>(map_state),
                                 reinterpret_cast<Address>(&stock_resolve), add_engine};
static const Pages pages = {128, reinterpret_cast<Address>(&get_slots), reinterpret_cast<Address>(slots),
                            reinterpret_cast<Address>(&initialized)};

// A screen object whose constructor fills the identity words and the slot.
alignas(4) static char state[64];
static char* construct(char* object, u32 page) {
    if (!ctor_fills) return object;
    u32* words = reinterpret_cast<u32*>(object);
    words[0] = 0x3000; words[1] = 0x4000; words[3] = page; words[4] = 0x5000;
    slots[page] = object;
    return object;
}
static const Screen screen = {state, sizeof state, address(0x3000), address(0x4000), construct};

static Record record(const char* name, u32 id = 34, u32 page = 82, u32 flag = 0) {
    return Record{address(0x1000), address(0x2000), name, address(id), address(page), flag};
}
static void reset() {
    std::memset(records, 0, sizeof records);
    std::memset(names, 0, sizeof names);
    std::memset(state, 0, sizeof state);
    for (char* volatile& slot : slots) slot = nullptr;
    map_state[1] = 1; missing = 33; registrar_works = ctor_fills = true; initialized = 1;
    resolves = adds = 0;
}

static StockString key(const char* name) {
    StockString s = {};
    std::memcpy(s.text, name, std::strlen(name));
    s.length = static_cast<u32>(std::strlen(name));
    s.capacity = 15;
    return s;
}

int main() {
    const Record probe = record("probe");
    // Known names resolve; unknown ones don't; before the name map exists every
    // name counts as known, without asking stock.
    reset();
    names[5] = "prism";
    const StockString prism = key("prism"), gone = key("gone");
    require(known(engines, &prism) == 1 && known(engines, &gone) == 0 && resolves == 2);
    map_state[1] = 0;
    require(known(engines, &gone) == 1 && resolves == 2);

    // Registration fills an empty record once, then confirms it.
    reset();
    require(ready(engines, pages, probe, nullptr) == 0);
    require(add(engines, pages, probe) == 1 && adds == 1);
    require(records[12 * 34] == 0x1000 && records[12 * 34 + 3] == 82);
    require(ready(engines, pages, probe, nullptr) == 1);
    require(add(engines, pages, probe) == 2 && adds == 1);
    // A stock rerun clears the records but keeps the name map: repopulate.
    std::memset(records, 0, sizeof records);
    require(add(engines, pages, probe) == 1 && adds == 2);

    // Invalid IDs and names never touch stock state.
    reset();
    for (const Record& bad : {record("probe", 63), record("probe", 34, 0), record("probe", 34, 128),
                              record(""), record("sixteen-chars-xx")})
        require(add(engines, pages, bad) == -1);
    require(resolves == 0 && adds == 0);

    // Any other occupant, or any differing field, is never overwritten.
    for (int field = 0; field < 6; ++field) {
        reset();
        require(add(engines, pages, probe) == 1);
        Record other = probe;
        unsigned char* bytes = reinterpret_cast<unsigned char*>(records + 12 * 34);
        if (field == 0) other.factory = address(0x1004);
        if (field == 1) other.parameter_name = address(0x2004);
        if (field == 2) other.flag = 1;
        if (field == 3) other.page = address(83);
        if (field == 4) other.name = "probf";
        if (field == 5) bytes[47] = 1;  // padding after the name must stay zero
        require(add(engines, pages, other) == -2 && adds == 1);
        require(ready(engines, pages, other, nullptr) == 0);
    }

    // The name map must be ready and must not name another engine.
    reset();
    map_state[1] = 0;
    require(add(engines, pages, probe) == -3 && adds == 0);
    reset();
    names[12] = "probe";
    require(add(engines, pages, probe) == -4 && adds == 0);
    // An owned record whose name is unmapped, or a registrar that writes
    // nothing, is inconsistent.
    reset();
    require(add(engines, pages, probe) == 1);
    names[34] = nullptr;
    require(add(engines, pages, probe) == -5);
    reset();
    registrar_works = false;
    require(add(engines, pages, probe) == -5);

    // Effects: the same checks against another table, limit and miss value.
    reset();
    missing = 19;
    static const Registry effects = {reinterpret_cast<Address>(effect_records), 19, 19,
                                     reinterpret_cast<Address>(map_state), reinterpret_cast<Address>(&stock_resolve),
                                     [](const Record& r, u32 id, u32 page) { write_record(effect_records + 12 * id, r, id, page); }};
    require(add(effects, pages, record("fx", 19, 21)) == -1);
    require(add(effects, pages, record("fx", 11, 21)) == 1 && effect_records[12 * 11 + 3] == 21);
    require(add(effects, pages, record("fx", 11, 21)) == 2);

    // A screen is constructed only into an empty slot from a wholly unused object.
    reset();
    require(add_screen(pages, record("probe", 34, 0), screen) == 0 && slot_reads == 0);
    slots[82] = state + 4;
    require(add_screen(pages, probe, screen) == 0);
    slots[82] = nullptr;
    state[60] = 1;
    require(add_screen(pages, probe, screen) == 0);
    state[60] = 0;
    ctor_fills = false;
    require(add_screen(pages, probe, screen) == 0);
    reset();
    require(add_screen(pages, probe, screen) == 1 && slots[82] == state);
    ctor_fills = false;  // Already in place: confirmed, not rebuilt.
    require(add_screen(pages, probe, screen) == 1);
    reinterpret_cast<u32*>(state)[4] = 0;  // no loader
    require(add_screen(pages, probe, screen) == 0);
    reinterpret_cast<u32*>(state)[4] = 0x5000;

    // Readiness needs the record, the initialized slots, this object in the
    // slot, its identity and word 2; it never calls the slot getter.
    require(add(engines, pages, probe) == 1);
    const unsigned reads = slot_reads;
    require(ready(engines, pages, probe, &screen) == 0);  // word 2 still zero
    reinterpret_cast<u32*>(state)[2] = 1;
    require(ready(engines, pages, probe, &screen) == 1);
    initialized = 0;
    require(ready(engines, pages, probe, &screen) == 0);
    initialized = 1;
    reinterpret_cast<u32*>(state)[3] = 83;
    require(ready(engines, pages, probe, &screen) == 0);
    reinterpret_cast<u32*>(state)[3] = 82;
    slots[82] = nullptr;
    require(ready(engines, pages, probe, &screen) == 0 && slot_reads == reads);

    // Names match exactly, inline or on the heap, without reading past them.
    StockString inline_name = {0, "probe", 5, 15};
    require(name_matches(probe, &inline_name) == 1);
    inline_name.length = 4;
    require(name_matches(probe, &inline_name) == 0);
    StockString short_capacity = {0, "probe", 5, 4};
    require(name_matches(probe, &short_capacity) == 0);
    char heap[6] = "probe";
    StockString heap_name = {0, {}, 5, 31};
    const char* pointer = heap;
    std::memcpy(heap_name.text, &pointer, sizeof pointer);
    require(name_matches(probe, &heap_name) == 1);
    heap[4] = 'f';
    require(name_matches(probe, &heap_name) == 0);
    pointer = nullptr;
    std::memcpy(heap_name.text, &pointer, sizeof pointer);
    require(name_matches(probe, &heap_name) == 0);
    std::puts("registration ok");
    return 0;
}
