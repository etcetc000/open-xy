// Shared header binding and Frame.header application. Include after screen.hpp.
#pragma once
#include <opxy/page_header.hpp>
namespace opxy { namespace screen { namespace {
// Standalone artwork can opt into any of the four value/title positions.
// Missing leaves stay null; the stock synth host keeps its existing bindings.
inline void bind_authored_header(Base* base, const char* const* ids) {
    for (u32 i = 0; i < page_controls; ++i) {
        base->values[i] = text_node(find(base, ids[i]));
        base->titles[i] = text_node(find(base, ids[page_controls + i]));
    }
}
// Standalone hosts have no stock value writer. Restore declared labels and
// normalized amounts each frame before applying optional author captions.
template <class Parameter>
inline void refresh_authored_header(Base* base, const Parameter* declared, u32 count, const draw::Input& input) {
    for (u32 i = 0; i < page_controls; ++i) {
        const u32 slot = input.shown[i];
        const bool active = declared && slot < count;
        if (Leaf* title = text_node(base->titles[i]))
            set_text(title, active ? declared[slot].label : "");
        if (Leaf* value = text_node(base->values[i])) {
            char caption[draw::header_capacity] = {};
            if (active) draw::header_amount(caption, sizeof(caption), input.parameters[i]);
            set_text(value, caption);
        }
    }
}
template <class Frame> inline void apply_header(Base* base, const Frame& frame) {
    if (!draw::valid(frame)) return;
    for (u32 i = 0; i < page_controls; ++i)
        if (frame.header[i][0])
            if (Leaf* value = text_node(base->values[i])) set_text(value, frame.header[i]);
}
}}} // namespace opxy::screen::<unnamed>
