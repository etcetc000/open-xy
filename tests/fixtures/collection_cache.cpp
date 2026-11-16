#include "collection_cache.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace opxy;
namespace ca=opxy::collection::activation;
namespace {
void require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
struct Context { u16 words[16],cached[16]; u32 syncs,calls,mode; };
bool sync(void* context,u32 address,u32 bytes) {
    Context& c=*static_cast<Context*>(context); ++c.syncs;
    require(address==0x8d2d1000 && bytes==32,"cache operation outside scratch line");
    if (c.mode==1 && c.syncs==1) return false;
    if (c.mode==2 && c.syncs==2) return false;
    if (c.mode!=3) std::memcpy(c.cached,c.words,32);
    return true;
}
u32 invoke(void* context) {
    Context& c=*static_cast<Context*>(context); ++c.calls;
    require(c.cached[0]==0xe120 && c.cached[2]==0x10,"invalid scratch instruction");
    return c.mode==4 ? 0 : c.cached[1];
}
Context fresh(u32 mode=0) {
    Context c={};c.mode=mode;c.words[0]=0xe120;c.words[1]=0x1357;c.words[2]=0x10;
    std::memcpy(c.cached,c.words,32);return c;
}
}
int main() {
    for (u32 mode=0;mode<5;++mode) {
        Context c=fresh(mode);
        const bool ok=ca::qualify_cache({&c,sync,invoke},c.words,0x8d2d1000,0x8d2d0000,0x8000);
        require(ok==(mode==0),"cache failure admitted");
        require(c.words[1]==0x1357,"scratch not restored");
        require(c.syncs==(mode==4 ? 0u : 2u),"wrong cache operation count");
    }
    for (u32 index=0;index<16;++index) {
        Context c=fresh();c.words[index]^=1;
        require(!ca::qualify_cache({&c,sync,invoke},c.words,0x8d2d1000,0x8d2d0000,0x8000),"changed scratch admitted");
        require(!c.calls && !c.syncs,"changed code invoked");
    }
    const u32 invalid[][3]={{0x8d2d1001,0x8d2d0000,0x8000},{0x8d2cffe0,0x8d2d0000,0x8000},
                           {0x8d2d8000,0x8d2d0000,0x8000},{0x8d2d1000,0x8d2d0000,31},
                           {0xffffffe0,0xffffffe0,64},{0x8d2d1000,0x8d2d0001,0x8000}};
    for (const auto& range:invalid) {
        Context c=fresh();
        require(!ca::qualify_cache({&c,sync,invoke},c.words,range[0],range[1],range[2]),"bad range admitted");
        require(!c.calls && !c.syncs && c.words[1]==0x1357,"bad range touched");
    }
    return 0;
}
