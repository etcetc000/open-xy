// Exercise the shared native header adapter against safe host text leaves.
#include <opxy/draw.hpp>
#include <cstring>
namespace opxy { namespace screen { namespace {
struct Leaf { bool text; char caption[24]; };
struct Base { Leaf* values[4]; Leaf* titles[4]; };
Leaf leaves[8];
const char* const ids[8] = {"v0", "v1", "v2", "v3", "t0", "t1", "t2", "t3"};
void* find(Base*, const char* id) {
    for (u32 i = 0; i < 8; ++i) if (!std::strcmp(ids[i], id)) return &leaves[i];
    return nullptr;
}
Leaf* text_node(void* raw) { return raw && static_cast<Leaf*>(raw)->text ? static_cast<Leaf*>(raw) : nullptr; }
void set_text(Leaf* leaf, const char* caption) { draw::copy(leaf->caption, sizeof(leaf->caption), caption); }
}}}
#include "screen_header.hpp"
using namespace opxy;
using namespace opxy::screen;
int main() {
    for (auto& leaf : leaves) leaf.text = true;
    Base base = {};
    bind_authored_header(&base, ids);
    struct Parameter { const char* label; } parameters[5] = {{"gain"}, {"mix"}, {"tone"}, {"rate"}, {"mode"}};
    draw::Input input = {};
    for (u32 i = 0; i < 4; ++i) input.shown[i] = i;
    input.parameters[0] = 0x40000000;
    refresh_authored_header(&base, parameters, 5, input);
    if (std::strcmp(leaves[0].caption, "50") || std::strcmp(leaves[4].caption, "gain")) return 1;
    draw::Frame<0, 0, 0, 0, 1> frame = {};
    draw::header(frame, 0, "-6db");
    apply_header(&base, frame);
    if (std::strcmp(leaves[0].caption, "-6db") || std::strcmp(leaves[1].caption, "0")) return 2;
    // An empty override restores the default; alternate labels follow shown[].
    draw::header(frame, 0, "");
    input.shown[0] = 4;
    input.parameters[0] = 0x7fffffff;
    refresh_authored_header(&base, parameters, 5, input);
    apply_header(&base, frame);
    if (std::strcmp(leaves[0].caption, "100") || std::strcmp(leaves[4].caption, "mode")) return 3;
    // Invalid frames cannot write headers; undeclared slots clear their text.
    draw::header(frame, 0, "bad");
    frame.header[1][draw::header_capacity - 1] = 'x';
    for (u32 i = 0; i < draw::header_capacity; ++i) frame.header[1][i] = 'x';
    apply_header(&base, frame);
    if (std::strcmp(leaves[0].caption, "100")) return 4;
    input.shown[0] = 7;
    refresh_authored_header(&base, parameters, 5, input);
    if (leaves[0].caption[0] || leaves[4].caption[0]) return 5;
    // Missing/non-text bindings are harmless, including entirely headerless art.
    const char* absent[8] = {"none", "none", "none", "none", "none", "none", "none", "none"};
    bind_authored_header(&base, absent);
    refresh_authored_header(&base, parameters, 5, input);
    frame = {};
    draw::header(frame, 0, "safe");
    apply_header(&base, frame);
    leaves[0].text = false;
    bind_authored_header(&base, ids);
    if (base.values[0]) return 6;
    apply_header(&base, frame);
    return 0;
}
