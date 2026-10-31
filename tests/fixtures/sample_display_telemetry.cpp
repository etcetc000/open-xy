#include <cstdio>
#include <cstdlib>
#include <array>
#include <vector>
#include <cstring>
#include "stock.hpp"
#include <opxy/sample.hpp>
#include "sample_display_preview.hpp"
void host_barrier() {}
void* fake_address(opxy::u32);
namespace opxy { namespace platform {
template<class F> F* fake_stock(u32 address){return reinterpret_cast<F*>(fake_address(address));}
u32 fake_track_engine(u32){return 36;}
u32 fake_word(u32);
}}
unsigned reads, invalidate_at_read;
namespace opxy { i16 counted_sample(const SampleView& s,u32 f,u32 c){++reads;if(invalidate_at_read==reads)sdk_sample_preview_request(888,202);return sample_at(s,f,c);} }
#define sample_at counted_sample
#define stock fake_stock
#define track_engine fake_track_engine
#define word fake_word
#include <sample_display_preview.cpp>
#include <sample_display_telemetry.cpp>
#undef sample_at
#undef stock
#undef track_engine
#undef word
u32 role,clock_ms;
std::vector<std::array<u8,24>> packets;
void require(bool b,const char* s){if(!b){std::fprintf(stderr,"%s\n",s);std::exit(1);}}
u32 get_role(){return role;}
u32 get_time(u32,u32){return clock_ms;}
void transmit(u32,u32 opcode,const void* p,u32 size,u32,u32,u32){
 require(opcode==0xa4&&size==24,"native bridge contract");
 std::array<u8,24> v;for(u32 i=0;i<24;++i)v[i]=static_cast<const u8*>(p)[i];packets.push_back(v);
}
u32 project_id=11, project_epoch=1, load_busy, browser, ring_read, ring_write;
u32 acquires,releases,locks;
void* queued;
alignas(8) u8 object[64], second_object[64];
u8 source[0x39d0];
u32 object_key=101;
bool cached=true, invalidate_on_acquire;
u32 hash_path(const char* path){return !std::strcmp(path,"first")?101:202;}
u32 epoch(){return project_epoch;}
const u8* source_pattern(u32){return source;}
void lock_queue(u32,i32){++locks;}
void unlock_queue(u32,u32,u32,u32){}
void enqueue(u32,void** item){require(!queued,"only one preview job queued");queued=*item;}
const void* acquire(u32,u32 key){++acquires;if(invalidate_on_acquire)sdk_sample_preview_request(777,202);return cached&&key==object_key?object:nullptr;}
void release(u32,u32){++releases;}
void run_job(){
 require(queued,"worker job exists");void* item=queued;queued=nullptr;
 const auto* table=*static_cast<const platform::Address**>(item);
 require(reinterpret_cast<u32(*)(void*)>(table[4])(item)==0&&
         reinterpret_cast<u32(*)(void*)>(table[5])(item)==0,"worker-only noncancelled job ABI");
 reinterpret_cast<void(*)(void*)>(table[2])(item);
 require(preview::busy,"job owned until destructor");
 reinterpret_cast<void(*)(void*)>(table[1])(item);
}
namespace opxy { namespace platform {
u32 fake_word(u32 a){
 if(a==0x840a0524)return project_id;if(a==0x840a0528)return load_busy;
 if(a==kScreen)return browser;if(a==0x84048894)return 99;
 if(a==0x08037d70)return 1;if(a==0x08037d6c)return 2;
 if(a==0x08037f78)return ring_read;if(a==0x08037f7c)return ring_write;
 std::exit(3);
}
}}
int ready(){return 1;} u32 page(){return 255;}
const platform::SampleMachine* sdk_sample_machine(u32 engine){
 static const platform::SampleMachine m={reinterpret_cast<const char*>(36),2,ready,nullptr,nullptr,
 nullptr,nullptr,0,nullptr,nullptr,nullptr,nullptr,page};
 return engine==36?&m:nullptr;
}
void* fake_address(u32 a){
 if(a==0x8d584224)return reinterpret_cast<void*>(&epoch);
 if(a==0x8d587708)return reinterpret_cast<void*>(&source_pattern);
 if(a==0x8d4e813a)return reinterpret_cast<void*>(&hash_path);
 if(a==0x8c03e6f4)return reinterpret_cast<void*>(&lock_queue);
 if(a==0x8c03e9c8)return reinterpret_cast<void*>(&unlock_queue);
 if(a==0x8d4dd02c)return reinterpret_cast<void*>(&enqueue);
 if(a==0x8d509d70)return reinterpret_cast<void*>(&acquire);
 if(a==0x8d509df8)return reinterpret_cast<void*>(&release);
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
 std::strcpy(reinterpret_cast<char*>(source+0x396c),"first");
 *reinterpret_cast<u32*>(object)=1;*reinterpret_cast<const i16**>(object+8)=pcm;
 *reinterpret_cast<u32*>(object+0x30)=1024;*reinterpret_cast<u32*>(object+0x38)=44100;object[0x3c]=1;
 role=1;SampleDisplaySnapshot out{};
 require(!sample_display_snapshot(3,out),"new selected track cannot expose previous track data");
 // No voice render after this subscription: worker primes the already cached sample.
 packets.clear();sdk_sample_visual_poll();require(packets.size()==1,"GUI queues only subscription, no PCM locks");
 const auto subscribe=packets[0];require(wire::read(subscribe.data()+20)==101,"subscription binds assigned source key");
 role=0;sdk_sample_visual_message(subscribe.data(),24);sdk_sample_visual_poll();
 const u32 before_reads=reads;run_job();require(reads-before_reads<=1024,"worker analysis bounded");
 packets.clear();for(u32 i=0;i<72;++i){++clock_ms;sdk_sample_visual_poll();}
 role=1;for(const auto& packet:packets)sdk_sample_visual_message(packet.data(),24);
 require(sample_display_snapshot(3,out)&&(out.flags&sample_waveform_ready)&&!out.marker_count&&
         !(out.flags&sample_voice_live),"cold assigned waveform before any note");
 clock_ms+=600;require(sample_display_snapshot(3,out)&&!out.marker_count,"idle waveform remains after stale marker timeout");
 // An old or different sample voice cannot contribute markers under the new binding.
 role=0;SampleView wrong=s;wrong.data=pcm+2;
 sdk_sample_visual_render(36,3,100,&wrong,&v);
 require(working_cookie!=gui_cookie,"different PCM voice rejected");
 sdk_sample_visual_render(36,3,101,&s,&v);
 packets.clear();for(u32 i=0;i<72;++i){++clock_ms;sdk_sample_visual_poll();}
 role=1;for(const auto& packet:packets)sdk_sample_visual_message(packet.data(),24);
 require(sample_display_snapshot(3,out)&&(out.flags&sample_voice_live)&&out.marker_count==1,"matched real grains follow preview");
 clock_ms+=600;require(sample_display_snapshot(3,out)&&!(out.flags&sample_voice_live)&&!out.marker_count,
                     "stale live grains cleared without losing waveform");
 // Source changes hide old data immediately. A job queued for that source may
 // finish after another project was restored, but cannot publish into its cookie.
 std::strcpy(reinterpret_cast<char*>(source+0x396c),"second");
 require(!sample_display_snapshot(3,out),"reassignment invalidates waveform before worker");
 packets.clear();sdk_sample_visual_poll();const auto second=packets[0];
 role=0;sdk_sample_visual_message(second.data(),24);sdk_sample_visual_poll();require(queued,"replacement queues worker");
 role=1;++project_epoch;require(!sample_display_snapshot(3,out),"project epoch invalidates same source");
 packets.clear();sdk_sample_visual_poll();const auto changed=packets[0];
 role=0;sdk_sample_visual_message(changed.data(),24);run_job();
 require(!sdk_sample_preview_snapshot(wire::read(changed.data()+8),out),"delayed old job cannot publish new project");
 require(releases==1,"retained old source released exactly once by worker");
 // Queue full rejects safely, and an empty cache retries without allocation or I/O.
 ring_read=1;ring_write=0;clock_ms+=250;sdk_sample_visual_poll();require(!queued&&!preview::busy,"full queue safe");
 ring_read=ring_write=0;cached=false;clock_ms+=250;sdk_sample_visual_poll();run_job();
 require(!sdk_sample_preview_snapshot(wire::read(changed.data()+8),out),"cache miss stays pending");
 cached=true;object_key=202;clock_ms+=250;sdk_sample_visual_poll();run_job();
 require(sdk_sample_preview_snapshot(wire::read(changed.data()+8),out),"cached sample ready on bounded retry");
 // Subscription expiry releases on worker, never from audio render or GUI draw.
 clock_ms+=1600;sdk_sample_visual_poll();require(queued,"expiry schedules release");run_job();
 require(releases==2&&!preview::retained,"expiry drains retained source");
 // Cancellation before load never acquires, and a request superseded during
 // acquire or bounded PCM copy releases its reference without publishing.
 object_key=101;u32 prior_acquires=acquires,prior_releases=releases;
 sdk_sample_preview_request(501,101);clock_ms+=250;sdk_sample_preview_poll(clock_ms);
 sdk_sample_preview_request(0,0);run_job();require(acquires==prior_acquires,"queued cancel performs no acquire");
 sdk_sample_preview_request(502,101);clock_ms+=250;sdk_sample_preview_poll(clock_ms);
 invalidate_on_acquire=true;run_job();invalidate_on_acquire=false;
 require(releases==prior_releases+1&&!sdk_sample_preview_snapshot(502,out),"changed request during acquire releases reference");
 sdk_sample_preview_request(503,101);clock_ms+=250;sdk_sample_preview_poll(clock_ms);
 invalidate_at_read=reads+10;run_job();invalidate_at_read=0;
 require(releases==prior_releases+2&&!sdk_sample_preview_snapshot(503,out),"changed request during copy cannot publish");
 role=1;browser=kSampleBrowserScreen;require(!sample_display_snapshot(3,out),"browser audition invalidates assigned snapshot");
 browser=0;load_busy=1;require(!sample_display_snapshot(3,out),"project load cannot expose cached waveform");
 load_busy=0;std::memset(source+0x396c,'x',96);
 require(!sample_display_snapshot(3,out)&&!gui_key,"unterminated path rejected boundedly");
 source[0x396c]=0;require(!sample_display_snapshot(3,out)&&!gui_key,"empty assignment is unavailable");

}
