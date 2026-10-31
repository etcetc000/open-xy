// Stock SVG boundary double for executing the real player renderer on the host.
#pragma once
#include <opxy/types.hpp>
#include <cstring>
namespace opxy { namespace screen { namespace {
struct Leaf { i32 matrix[9]; char text[64]; };
struct PathNode { Leaf leaf; };
struct Base { const void* svg; Leaf* titles[4]; Leaf* values[4]; };
inline PathNode* path_node(void* p) { return static_cast<PathNode*>(p); }
inline Leaf* text_node(void* p) { return static_cast<Leaf*>(p); }
template<class T> void clear(T*) {}
inline void bind_controls(Base*) {}
inline void* find(Base*, const char*) { return nullptr; }
inline u32 selected_track() { return 0; }
template<unsigned N> void set_words(Leaf* node, const u32 (&at)[N], const i32 (&values)[N]) {
    for (unsigned i = 0; i < N; ++i) node->matrix[at[i]] = values[i];
}
inline void set_text(Leaf* node, const char* text) { std::strncpy(node->text, text, 63); }
inline void draw(Base*) {}
}}}
