#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
// Never replace a stock or machine-owned page. A lost constructed slot is not
// repaired here: doing so would require a separately proven loader lifecycle.
template<class Object> u32 arrangement_free_page(Object* const* slots, u32 limit) {
    if (!slots || limit <= 82 || limit > 256) return 0;
    for (u32 page = limit - 1; page >= 82; --page) if (!slots[page]) return page;
    return 0;
}
}}
