#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace screen { namespace rgba_detail {
using Address = __UINTPTR_TYPE__;
// A record is addressed afresh for each update: no cached heap pointers.
// XY explicit paint parsing appends one fill/stroke record per authored leaf.
inline bool set_color(Address begin, Address end, Address capacity,
                      u32 stride, u32 id, u32 color) {
    if ((stride != 4 && stride != 20) || !begin || (begin & 3) ||
        end < begin || capacity < end || (end - begin) % stride ||
        (capacity - begin) % stride || capacity - begin > 65535u * stride ||
        !id || id > (end - begin) / stride) return false;
    u32* word = reinterpret_cast<u32*>(begin + (id - 1) * stride);
    if (*word == color) return false;
    *word = color;
    return true;
}
// Refuse unexpected aliases among bound leaves. Validated explicit leaf paints
// have distinct parser-appended IDs; inheritance and palette selection are not
// permitted by the RGBA resource contract.
template <class Node> inline bool unique(Node* const* nodes, u32 count, u32 index, bool fill) {
    if (index >= count || !nodes[index]) return false;
    const u32 id = fill ? nodes[index]->fill : nodes[index]->stroke;
    if (!id) return false;
    for (u32 other = 0; other < count; ++other)
        if (other != index && nodes[other] &&
            (fill ? nodes[other]->fill : nodes[other]->stroke) == id) return false;
    return true;
}
}}}
