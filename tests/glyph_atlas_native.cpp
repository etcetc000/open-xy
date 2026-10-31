#include "glyph_atlas.hpp"
using namespace opxy;
using namespace opxy::screen;
using F=draw::GlyphFrame<draw::AffineFrame<0,0,4,0,0>,2,2,0>;
struct Object {Base base;PathNode* nodes[4];glyph_detail::State<2,2> glyph_state;};
struct Machine {static const char* const glyph_ids[4];};
const char* const Machine::glyph_ids[4]={"0","1","2","3"};
static Object object;
static PathNode destinations[4],sources[4];
static Command storage[4][18], original[4][16];
static u32 changes;
struct Access {
    static PathNode* source(Base*,const char* id){return path_node(&sources[id[0]-'0']);}
    static void changed(PathNode*){++changes;}
};
using B=glyph_detail::Binder<F,2,Access>;
static u32 address(void* p){return reinterpret_cast<u32>(p);}
static void reset(){
    object={};changes=0;
    for(u32 i=0;i<4;++i){
        destinations[i]={};sources[i]={};
        destinations[i].leaf.vtable=sources[i].leaf.vtable=kPathLeaf;
        for(u32 j=0;j<18;++j){storage[i][j]={};storage[i][j].x=12345;storage[i][j].type=99;}
        for(u32 j=0;j<16;++j){storage[i][j+1].x=j;storage[i][j+1].type=j?2:1;original[i][j]={};}
        destinations[i].commands={address(storage[i]+1),address(storage[i]+17),address(storage[i]+17)};
        object.nodes[i]=&destinations[i];
        const u32 count=i==0?3:i==1?2:i==2?4:1;
        for(u32 j=0;j<count;++j){original[i][j].type=j?2:1;original[i][j].x=100+i*10+j;original[i][j].y=-300-j;
            for(u32 k=0;k<7;++k)original[i][j].stock25[k]=0xa5;}
        if(i==2){original[i][2].type=4;original[i][2].control_x=77;original[i][2].control_y=-78;
            original[i][2].end_x=79;original[i][2].end_y=-80;original[i][3].type=5;}
        sources[i].commands={address(original[i]),address(original[i]+count),address(original[i]+16)};
    }
}
extern "C" unsigned run(){
    static_assert(sizeof(F)==4+4*32+36+32+8,"Glyph frame ABI");
    static_assert(__is_standard_layout(F),"Frame must have standard layout");
    F frame={};for(u32 i=0;i<4;++i)draw::affine(frame,i,{1024,0,0,1024,0,0});
    frame.glyphs[0]=0;frame.glyphs[1]=1;
    if(!draw::valid(frame))return 1;
    frame.glyphs[1]=2;if(draw::valid(frame))return 2;frame.glyphs[1]=1;
    reset();B::init<Machine>(&object);if(!object.glyph_state.ready)return 3;
    if(!B::apply(&object,frame)||changes!=4)return 4;
    if(destinations[0].commands.end-destinations[0].commands.begin!=96||
       destinations[1].commands.end-destinations[1].commands.begin!=64||
       destinations[2].commands.end-destinations[2].commands.begin!=128||
       destinations[3].commands.end!=destinations[3].commands.begin)return 5;
    for(u32 i=0;i<4;++i)if(storage[i][0].type!=99||storage[i][17].type!=99||
       destinations[i].commands.begin!=address(storage[i]+1)||destinations[i].commands.capacity!=address(storage[i]+17))return 6;
    if(storage[2][3].control_x!=77||storage[2][3].end_y!=-80||storage[2][3].stock25[0])return 7;
    original[0][0].x=9999;storage[0][1].stock25[0]=12;
    if(!B::apply(&object,frame)||changes!=4||storage[0][1].stock25[0]!=12)return 8;
    frame.glyphs[0]=1;frame.glyphs[1]=0;
    if(!B::apply(&object,frame)||changes!=8||storage[2][1].x!=100||storage[2][1].stock25[0])return 9;
    // Reject the entire update when any allocation or selector changes unexpectedly.
    const u32 saved=destinations[3].commands.end;destinations[3].commands.end+=32;
    frame.glyphs[0]=0;if(B::apply(&object,frame)||changes!=8)return 10;
    destinations[3].commands.end=saved;frame.glyphs[1]=2;
    if(B::apply(&object,frame)||changes!=8)return 11;
    reset();destinations[1].commands=destinations[0].commands;B::init<Machine>(&object);
    if(object.glyph_state.ready)return 12;
    reset();sources[0].commands=destinations[0].commands;B::init<Machine>(&object);
    if(object.glyph_state.ready)return 13;
    reset();sources[0].commands.end+=17*32;B::init<Machine>(&object);if(object.glyph_state.ready)return 14;
    reset();original[0][1].type=3;B::init<Machine>(&object);if(object.glyph_state.ready)return 15;
    reset();original[0][1].x=8192*1024+1;B::init<Machine>(&object);if(object.glyph_state.ready)return 16;
    reset();destinations[0].commands.capacity-=32;B::init<Machine>(&object);if(object.glyph_state.ready)return 17;
    reset();destinations[0].commands.begin+=1;B::init<Machine>(&object);if(object.glyph_state.ready)return 18;
    return 0;
}
