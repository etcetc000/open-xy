#include "sample_display_wire.hpp"
#include <cstdio>
#include <cstdlib>
using namespace opxy;
using namespace opxy::platform::sample_display_wire;
void require(bool b,const char* s){if(!b){std::fprintf(stderr,"%s\n",s);std::exit(1);}}
int main(){
 Receiver r;r.reset(9);u8 p[24];
 packet(p,Begin,2,0,8,1,1000,7);r.receive(p,24);require(!r.assembling,"reject stale subscription");
 packet(p,Begin,2,0,9,1,1000,7);r.receive(p,24);
 packet(p,Commit,2,0,9,1,0,0);require(!r.receive(p,24),"partial waveform never published");
 for(u32 i=0;i<64;++i){packet(p,Wave,2,i,9,1,0x000afff6,0x0014ffec);r.receive(p,24);}
 packet(p,Marker,2,0,9,1,1000,0);r.receive(p,24);
 packet(p,Commit,2,1,9,1,0,0);require(!r.receive(p,24),"out of bounds grain rejected");
 packet(p,Marker,2,0,9,1,999,123);r.receive(p,24);
 packet(p,Commit,2,1,9,1,0,0);require(r.receive(p,24),"complete actual waveform and marker published");
 require(r.complete.minimum[127]==-20&&r.complete.maximum[127]==20&&r.complete.markers[0].frame==999,"signed waveform and real position preserved");
 packet(p,Begin,2,0,9,2,500,7);r.receive(p,24);
 packet(p,Wave,2,0,9,1,0,0);r.receive(p,24);require(!r.wave_low,"old voice packet rejected after new generation");
 packet(p,Begin,2,0,9,1,1000,7);r.receive(p,24);require(r.generation==2,"late old Begin cannot restore retired voice");
 p[3]=99;require(service(p,24)&&!valid(p,24),"private malformed packet claimed but rejected");
 r.reset(10);require(!r.ready&&!r.assembling,"selection reset hides stale display");
}
