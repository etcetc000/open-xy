#include "../../sdk/platform/opxy-1.2.1/arranger_piano.hpp"
#include <cstdio>
#include <cstdlib>
using namespace opxy; using namespace opxy::platform;
static void require(bool ok) { if(!ok) { std::fputs("piano requirement failed\n",stderr); std::abort(); } }
static i32 get(const u8* p,u32 o) { u32 v=p[o]|(u32(p[o+1])<<8); return v==65535?-1:i32(v); }
static void put(u8* p,u32 o,i32 v) { p[o]=u8(v); p[o+1]=u8(u32(v)>>8); }
static i32 insert(void* ptr,const PianoNote* note) {
    u8* p=static_cast<u8*>(ptr); i32 free=get(p,0x780); if(free<0)return -1;
    i32 next=get(p,0x782),prev=-1;
    while(next!=-1) { PianoNote n; const u8* in=p+next*16+4; u8* out=reinterpret_cast<u8*>(&n); for(u32 j=0;j<11;++j)out[j]=in[j]; if(n.position>=note->position)break; prev=next; next=get(p,next*16+2); }
    put(p,0x780,get(p,free*16+2)); put(p,free*16,prev); put(p,free*16+2,next);
    put(p,prev==-1?0x782:prev*16+2,free); put(p,next==-1?0x784:next*16,free); put(p,0x786,get(p,0x786)+1);
    const u8* src=reinterpret_cast<const u8*>(note);for(u32 j=0;j<11;++j)p[free*16+4+j]=src[j];return free;
}
static u8 source[0x53f4];
int main() {
    source[0]=16;u8* c=source+0x457c;for(u32 i=0;i<120;++i)put(c,i*16+2,i==119?-1:i+1);
    put(c,0x782,-1);put(c,0x784,-1); PianoEditor editor(insert);editor.bind(source,1,1);require(editor.valid());
    i32 a=-1,b=-1,d=-1;PianoNote n={480,240,60,100,0,0}; require(editor.add(n,a));n.position=0;require(editor.add(n,b));n.position=960;require(editor.add(n,d));require(editor.count()==3);
    require(editor.hit(500,60)==a);require(editor.hit(750,60)==-1);
    require(editor.note(a,n));n.position=1200;require(editor.update(a,n,1));require(editor.valid());require(editor.hit(1200,60)==a);
    n.position=1440;require(editor.update(a,n,1));require(editor.undo());require(editor.hit(480,60)==a);require(editor.redo());require(editor.hit(1440,60)==a);
    require(editor.erase(b));require(editor.erase(a));require(editor.erase(d));require(editor.valid() && editor.count()==0);require(editor.undo());require(editor.count()==1);
    require(editor.note(d,n));require(!editor.update(d,n));require(editor.canRedo());require(editor.redo());
    editor.bind(source,1,2);require(!editor.canUndo());
    for(u32 i=0;i<120;++i){n.position=i*40;n.duration=20;require(editor.add(n,a));}require(editor.count()==120);require(!editor.add(n,b));require(editor.valid());
    require(editor.erase(60));require(editor.add(n,b));require(b==60);require(editor.valid());
    c[b*16+14]|=2;require(!editor.erase(b));require(!editor.undo());c[b*16+14]&=u8(~2u);
    const u8 saved=c[0x782];c[0x782]=120;require(!editor.valid());require(!editor.erase(b));c[0x782]=saved;require(editor.valid());
    editor.bind(source,2,2);require(!editor.canUndo());
    source[8]=16;require(!editor.valid());require(!editor.erase(0));source[8]=3;
    source[0]=0;require(!editor.valid());source[0]=16;require(editor.valid());
    const u32 early=u32(get(c,0x782));
    put(c,early*16+4,-120);put(c,early*16+6,-1);
    require(editor.note(early,n));require(n.position==-120);n.pitch=61;
    require(editor.update(early,n));require(editor.note(early,n));require(n.position==-120 && n.pitch==61);
    require(editor.undo());require(editor.note(early,n));require(n.position==-120 && n.pitch==60);
    return 0;
}
