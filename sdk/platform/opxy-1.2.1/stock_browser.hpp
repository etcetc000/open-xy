// Internal GUI-only flat selector for the authenticated stock OS 1.2.1.
#pragma once
#include "stock.hpp"

namespace opxy { namespace platform { namespace stock_browser {
struct Callbacks {
    void* context;
    void (*confirm)(void*, u32 index);
    void (*cancel)(void*); // Optional; called on close without confirmation.
    // Optional: every highlight stock reports, as its mode and the row's or item's id.
    void (*highlight)(void*, u32 mode, i32 id);
};
namespace detail {
struct String { u32 allocator; char text[16]; u32 length, capacity; };
template<class T> struct Vector { u32 allocator; T* begin; T* end; T* capacity; };
struct Item { String name; i32 id; u32 flags; };
struct Row {
    String name; i32 id; u32 flags;
    Vector<Item> items; Vector<Row> children; Row* parent;
};
struct Catalog { Vector<Row> rows; u32 filesystem; };
struct Session;
struct Delegate;
struct Methods {
    void (*destroy)(Delegate*);
    u32 (*unused04)(Delegate*);
    u32 (*key)(Delegate*, const u32*);
    u32 (*shift_key)(Delegate*, const u32*);
    void (*highlight)(Delegate*, u32, i32);
    u32 (*page_key)(Delegate*);
    u32 (*unused18)(Delegate*);
    u32 (*click)(Delegate*, u32);
    void (*tick)(Delegate*);
    void (*leave)(Delegate*);
    void (*audition)(Delegate*, i32);
    u32 (*unused2c)(Delegate*);
    void (*options)(Vector<String>*, Delegate*);
    u32 (*option)(Delegate*);
    void (*set_option)(Delegate*, u32);
    u32 (*busy)(Delegate*);
};
struct Delegate { const Methods* methods; Session* session; };
struct Session {
    Catalog catalog;
    Delegate delegate;
    Callbacks callbacks;
    char title[16];
    u32 count, candidate;
    bool accepted, closing;
    bool columns;  // rows with items: the second column's controls are on
};
static_assert(sizeof(String) == 28, "Stock inline string ABI");
static_assert(sizeof(Address) != 4 || sizeof(Item) == 36, "Stock browser item ABI");
static_assert(sizeof(Address) != 4 || sizeof(Row) == 72, "Stock browser row ABI");
static_assert(sizeof(Address) != 4 || sizeof(Catalog) == 20, "Stock browser catalog ABI");
static_assert(sizeof(Methods) == 16 * sizeof(void(*)()), "Stock browser delegate ABI");
bool open(Session&, Row*, u32 count, const char* title,
          const char* const* labels, u32 initial, Callbacks, i32 track = -1);
// Rows the caller filled, each possibly holding items (a second column): the
// same modal as open. name() fills a stock string from 1..15 printable characters.
bool name(String&, const char*);
// With columns, the browser's second column (the rows' items) gets its controls,
// as stock's preset browser has them: knobs 2-4 scroll and press it.
bool open_rows(Session&, Row*, u32 count, const char* title, u32 initial, Callbacks, i32 track = -1,
               bool columns = false);
}
// Declare as a zero-initialized static aggregate. Storage and callback context
// must outlive the modal. Titles and labels are copied, printable ASCII 1..15.
// Returns false for invalid input or an occupied stock browser. No stock patch
// or public machine/WASM API is installed by this adapter.
template<u32 N> struct StockSelector {
    static_assert(N > 0 && N <= 0x7fffffffu / 72, "Nonempty bounded stock catalog");
    detail::Row rows[N];
    detail::Session session;
    bool open(const char* title, const char* const (&labels)[N], u32 initial, Callbacks callbacks) {
        return detail::open(session, rows, N, title, labels, initial, callbacks);
    }
    // The same with the first `count` rows, 1..N, and the heading's track
    // number shown under the title as stock's own selectors show it (-1: none).
    bool open(const char* title, const char* const* labels, u32 count, u32 initial, Callbacks callbacks,
              i32 track = -1) {
        return count <= N && detail::open(session, rows, count, title, labels, initial, callbacks, track);
    }
};
}}}
