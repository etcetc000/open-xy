// XY 1.2.1 path vector ownership: authenticated constructor/grow/destructor.
// Own parsed destination allocations; never substitute or share begin/capacity.
#pragma once
#include "screen.hpp"
namespace opxy { namespace screen { namespace glyph_detail {
const u32 capacity = 16, parts = 2;
inline bool vector_valid(const Vector& v, u32 minimum, u32 maximum) {
    return v.begin && !(v.begin & 3) && v.end >= v.begin && v.capacity >= v.end &&
        (v.end-v.begin)%32 == 0 && (v.capacity-v.begin)%32 == 0 &&
        v.capacity-v.begin <= 128*32 && v.end-v.begin >= minimum*32 && v.end-v.begin <= maximum*32;
}
inline bool overlap(const Vector& a, const Vector& b) {
    return a.begin < b.capacity && b.begin < a.capacity;
}
inline bool coordinate(i32 value) { return value >= -8192*1024 && value <= 8192*1024; }
inline bool copy_source(Command* out, const Command* in, u32 count) {
    bool closed = true;
    for (u32 i=0; i<count; ++i) {
        const Command& s=in[i]; Command d={};
        if ((closed && s.type != 1) || (s.type != 1 && s.type != 2 && s.type != 4 && s.type != 5)) return false;
        d.type=s.type;
        if (s.type != 5) {
            if (!coordinate(s.x) || !coordinate(s.y)) return false;
            d.x=s.x; d.y=s.y;
        }
        if (s.type == 4) {
            if (!coordinate(s.control_x) || !coordinate(s.control_y) || !coordinate(s.end_x) || !coordinate(s.end_y)) return false;
            d.control_x=s.control_x; d.control_y=s.control_y; d.end_x=s.end_x; d.end_y=s.end_y;
        }
        out[i]=d; closed=s.type == 5;
    }
    return true;
}
template <u32 N,u32 D> struct State {
    Command definitions[D][2][16];
    u32 counts[D][2];
    Vector buffers[N][2];
    u32 selected[N];
    u32 ready;
};
struct Access {
    static PathNode* source(Base* base,const char* id) { return path_node(find(base,id)); }
    static void changed(PathNode* node) { invalidate(node); }
};
template<class F,u32 N=F::glyph_count,class A=Access> struct Binder {
    template<class Machine,class Object> static void init(Object* o) {
        State<N,F::glyph_definition_count>& state=o->glyph_state;
        state.ready=0;
        // Validate every destination before retaining any allocation pointer.
        for (u32 i=0;i<N;++i) for (u32 part=0;part<2;++part) {
            PathNode* node=o->nodes[F::glyph_start+2*i+part];
            if (!node || path_node(node) != node || static_cast<__INT16_TYPE__>(node->fill)>0 || !vector_valid(node->commands,16,16) || node->commands.capacity-node->commands.begin<512) return;
            const Command* c=reinterpret_cast<const Command*>(node->commands.begin);
            for (u32 k=0;k<16;++k) if (c[k].type != (k ? 2 : 1)) return;
            for (u32 j=0;j<F::node_count;++j) {
                PathNode* other=o->nodes[j];
                if (j != F::glyph_start+2*i+part && other && overlap(node->commands,other->commands)) return;
            }
            state.buffers[i][part]=node->commands;
        }
        for (u32 d=0;d<F::glyph_definition_count;++d) for (u32 part=0;part<2;++part) {
            PathNode* source=A::source(&o->base,Machine::glyph_ids[2*d+part]);
            if (!source || !vector_valid(source->commands,1,16)) return;
            for (u32 j=0;j<F::node_count;++j) if (o->nodes[j] && overlap(source->commands,o->nodes[j]->commands)) return;
            const u32 count=(source->commands.end-source->commands.begin)/32;
            if (!part && count<2) return;
            if (!copy_source(state.definitions[d][part],reinterpret_cast<const Command*>(source->commands.begin),count)) return;
            state.counts[d][part]=count == 1 ? 0 : count;
        }
        for (u32 i=0;i<N;++i) state.selected[i]=0xffffffffu;
        state.ready=1;
    }
    template<class Object> static bool apply(Object* o,const F& frame) {
        State<N,F::glyph_definition_count>& state=o->glyph_state;
        if (!state.ready) return false;
        // Whole-atlas preflight: a replaced allocation must never be written.
        for (u32 i=0;i<N;++i) for (u32 p=0;p<2;++p) {
            PathNode* node=o->nodes[F::glyph_start+2*i+p];
            const Vector& saved=state.buffers[i][p];
            if (!node || path_node(node) != node || node->commands.begin != saved.begin || node->commands.end != saved.end ||
                node->commands.capacity != saved.capacity || frame.glyphs[i]>=F::glyph_definition_count) return false;
        }
        for (u32 i=0;i<N;++i) {
            const u32 d=frame.glyphs[i];
            if (state.selected[i] == d) continue;
            for (u32 p=0;p<2;++p) {
                PathNode* node=o->nodes[F::glyph_start+2*i+p];
                Vector& saved=state.buffers[i][p];
                Command* out=reinterpret_cast<Command*>(saved.begin);
                const u32 count=state.counts[d][p];
                for (u32 k=0;k<count;++k) out[k]=state.definitions[d][p][k];
                saved.end=saved.begin+32*count;
                node->commands.end=saved.end;
                A::changed(node);
            }
            state.selected[i]=d;
        }
        return true;
    }
};
template<class F,class A> struct Binder<F,0,A> {
    template<class Machine,class Object> static void init(Object*) {}
    template<class Object> static bool apply(Object*,const F&) { return true; }
};
}}}
