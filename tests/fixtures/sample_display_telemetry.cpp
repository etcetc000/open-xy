#include <cstdio>
#include <cstdlib>
#include <array>
#include <vector>
#include "stock.hpp"
#include <opxy/sample.hpp>
void host_barrier() {}
void* fake_address(opxy::u32);
namespace opxy { namespace platform {
template<class F> F* fake_stock(u32 address){return reinterpret_cast<F*>(fake_address(address));}
u32 fake_track_engine(u32){return 36;}
}}
unsigned reads;
namespace opxy { i16 counted_sample(const SampleView& s,u32 f,u32 c){++reads;return sample_at(s,f,c);} }
#define sample_at counted_sample
#define stock fake_stock
#define track_engine fake_track_engine
#include <sample_display_telemetry.cpp>
#undef sample_at
#undef stock
#undef track_engine
u32 role,clock_ms;
std::vector<std::array<u8,24>> packets;
void require(bool b,const char* s){if(!b){std::fprintf(stderr,"%s\n",s);std::exit(1);}}
u32 get_role(){return role;}
u32 get_time(u32,u32){return clock_ms;}
void transmit(u32,u32 opcode,const void* p,u32 size,u32,u32,u32){
 require(opcode==0xa4&&size==24,"native bridge contract");
 std::array<u8,24> v;for(u32 i=0;i<24;++i)v[i]=static_cast<const u8*>(p)[i];packets.push_back(v);
}
void* fake_address(u32 a){
 if(a==0x8d5c3eac)return reinterpret_cast<void*>(&get_role);
 if(a==0x8d5be290)return reinterpret_cast<void*>(&get_time);
 if(a==0x11a00ec0)return reinterpret_cast<void*>(&transmit);
 std::exit(2);
}
int main(){
 u8 request[24];wire::packet(request,wire::Subscribe,2,0,9,0,36,0);
 require(sdk_sample_visual_message(request,24),"subscription accepted");
 i16 pcm[2048];for(u32 i=0;i<2048;++i)pcm[i]=static_cast<i16>(i-1000);
 SampleView s{pcm,1024,44100,2};SampleVisual v{};v.count=1;v.markers[0]={100,1234,1};
 for(u32 i=0;i<16;++i){reads=0;sdk_sample_visual_render(36,2,10,&s,&v);require(reads<=64,"bounded PCM reads");}
 require(cursor==512&&(working.flags&sample_waveform_ready),"progressive waveform completes");
 sdk_sample_visual_render(36,2,9,&s,&v);require(working.generation==10,"older polyphonic render cannot steal latest voice");
 wire::Receiver gui;gui.reset(9);
 // First pending snapshot was made before analysis completed. Consume it,
 // then publish the finished waveform and clear during its transmission.
 for(u32 i=0;i<72;++i){++clock_ms;sdk_sample_visual_poll();}
 sdk_sample_visual_render(36,2,10,&s,&v);
 for(u32 i=0;i<5;++i){++clock_ms;sdk_sample_visual_poll();}
 sdk_sample_visual_clear(36,2,10);
 for(u32 i=0;i<150;++i){++clock_ms;sdk_sample_visual_poll();}
 for(const auto& packet:packets)gui.receive(packet.data(),24);
 require(gui.ready&&(gui.complete.flags&sample_waveform_ready),"completed waveform transferred by values");
 require(!(gui.complete.flags&sample_voice_live)&&!gui.complete.marker_count,"destroy during transmission eventually publishes cleared state without another render");
 require(gui.complete.minimum[0]==-1000&&gui.complete.maximum[127]>1000,"sampled source waveform preserved");
 // A second engine has its own serial counter and may also start at10.
 packets.clear();wire::packet(request,wire::Subscribe,2,0,10,0,37,0);
 sdk_sample_visual_message(request,24);
 sdk_sample_visual_render(37,2,10,&s,&v);
 for(u32 i=0;i<72;++i){++clock_ms;sdk_sample_visual_poll();}
 gui.reset(10);for(const auto& packet:packets)gui.receive(packet.data(),24);
 require(gui.ready&&(gui.complete.flags&sample_voice_live)&&gui.complete.marker_count==1,
         "retired engine voice serial cannot clear another subscription's equal serial");
 // An interrupted clear publication is ignored rather than mixed with old fields.
 ++clear_sequence;clear_cookie=10;clear_id=10;
 require(!cleared(10,10),"partial clear publication must not be consumed");
 ++clear_sequence;require(cleared(10,10),"complete clear publication consumed");
 role=1;SampleDisplaySnapshot out{};
 require(!sample_display_snapshot(3,out),"new selected track cannot expose previous track data");
}
