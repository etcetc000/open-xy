// Checked stock registration. Each machine's generated registration unit
// describes its record (and own screen) with the types below and exports thin C
// entry points; registration.cpp holds the checks, registration_stock.cpp.in
// binds them to the stock tables. Startup and menu paths only, never audio.
#pragma once
#include "stock.hpp"

namespace opxy { namespace platform {

// Every field is a link-time address. Engine, effect and page IDs are absolute
// symbols whose values the linker (GNU ld or the browser kit) supplies, so they
// are never compile-time constants.
struct Record {
    const char* factory;         // record word 0
    const char* parameter_name;  // record word 1
    const char* name;            // native name, 1-15 characters, at record bytes 16-47
    const char* id;              // absolute symbol: engine or effect ID
    const char* page;            // absolute symbol: screen page ID, record word 3
    u32 flag;                    // record word 2: 1 for engines that receive samples, else 0
};

// A synth's own screen page: a stock screen object in machine BSS.
struct Screen {
    char* state;                 // the object, `bytes` long
    u32 bytes;
    const char* vtable;          // object word 0
    const char* asset;           // object word 1: SVG asset name
    char* (*construct)(char* state, u32 page);  // returns state and fills the page slot
};

namespace registry {
// A stock string: inline characters while capacity < 16, else a heap pointer.
struct StockString {
    u32 zero;
    char text[16];  // or, from the first byte, the heap pointer
    u32 length;
    u32 capacity;
};
static_assert(sizeof(StockString) == 28, "Stock string layout");



// One stock record table of 48-byte records and the name map beside it.
// Addresses are integers so the stock tables are constant data, not constructors.
struct Registry {
    Address records;
    u32 limit;                                   // IDs below this have a record
    u32 missing;                                 // resolver result for an unknown name
    Address map;                                 // word 1 stays null until initialized
    Address resolve;                             // u32 (const StockString*)
    void (*add)(const Record&, u32 id, u32 page);  // stock registrar; returns no result
};

// Screen page slots. The stock getter initializes them; the table and its
// initialized byte are read without side effects.
struct Pages {
    u32 limit;
    Address slots;                               // char** (): the getter
    Address table;
    Address initialized;                         // byte
};

// 1 added or repopulated, 2 already owned; negative rejects: -1 invalid IDs or name,
// -2 occupied record, -3 name map not initialized, -4 name mapped elsewhere,
// -5 inconsistent state or failed postcondition.
int add(const Registry&, const Pages&, const Record&);
// 1 when the page slot holds this screen, constructed here only if the slot and
// the whole object were empty; otherwise 0.
int add_screen(const Pages&, const Record&, const Screen&);
// 1 only while the record (and, given a screen, its loaded page) is ours.
// Read-only: calls no stock getter, allocator, registrar or resolver.
int ready(const Registry&, const Pages&, const Record&, const Screen*);
// 1 when a stock string holds exactly the record's name.
int name_matches(const Record&, const StockString*);
// 1 when the name resolves to a registered entry, or before the name map exists
// (so nothing is refused while stock is still starting).
int known(const Registry&, const StockString*);
}

}}

extern "C" {
int sdk_register_engine(const opxy::platform::Record*) __asm__("sdk_register_engine");
int sdk_register_effect(const opxy::platform::Record*) __asm__("sdk_register_effect");
int sdk_register_screen(const opxy::platform::Record*, const opxy::platform::Screen*) __asm__("sdk_register_screen");
int sdk_engine_ready(const opxy::platform::Record*, const opxy::platform::Screen*) __asm__("sdk_engine_ready");
int sdk_effect_ready(const opxy::platform::Record*) __asm__("sdk_effect_ready");
int sdk_name_matches(const opxy::platform::Record*, const void* string) __asm__("sdk_name_matches");
int sdk_stock_engine_known(const void* name) __asm__("sdk_stock_engine_known");
}
