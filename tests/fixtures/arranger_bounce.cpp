#include <map>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "stock.hpp"
#include "arranger_bounce.hpp"
#include "arranger_state.hpp"
#include "arranger_transport.hpp"
void host_barrier() {}
void* fake_function(opxy::u32);
unsigned char* fake_memory(opxy::u32);
namespace opxy { namespace platform {
template<class F> F* fake_stock(u32 address) { return reinterpret_cast<F*>(fake_function(address)); }
template<class T> T* fake_at(Address address) { return reinterpret_cast<T*>(fake_memory(static_cast<u32>(address))); }
u32 fake_word(u32 address) { u32 n; std::memcpy(&n, fake_memory(address),4); return n; }
u32 fake_byte(u32 address) { return *fake_memory(address); }
u32 fake_bpm() { return 1200; }
}}
#define stock fake_stock
#define at fake_at
#define word fake_word
#define byte fake_byte
#define project_bpm_x10 fake_bpm
#include <arranger_bounce.cpp>
#undef stock
#undef at
#undef word
#undef byte
#undef project_bpm_x10
using opxy::u32;
namespace {
unsigned char editor_memory[128], path_memory[64];
std::map<u32,u32> globals;
opxy::arranger::Model model;
u32 milliseconds, gen=1, starts, ends, commits, forwarded, saves, queued_frames, spinner_closes;
bool spinner_active;
bool running, destination_ok=true, destination_working, begin_ok=true, start_ok=true;
u32 destination_result=1;
std::vector<wire::Message> sent;
void require(bool yes,const char* label) { if(!yes){std::puts(label);std::exit(1);} }
u32 clock_ms(u32,u32){return milliseconds;}
u32 generation(){return gen;}
u32 get_editor(){return 0x1000;}
void set_state(u32,u32 n){store_word(0x1034,n);}
void saved(u32,u32){++forwarded;}
void close_spinner(){require(spinner_active,"only close an owned native save indicator"); spinner_active=false; ++spinner_closes;}
void native_range(const char*,u32 begin,u32 end,opxy::i32){queued_frames=end-begin;}
void save(u32,u32 frames){
    ++saves; spinner_active=true;
    const char path[]="/fat32/samples/user/bounce.wav";
    std::memcpy(path_memory,path,sizeof(path));
    store_word(0x1018,0x2000);store_word(0x1028,sizeof(path)-1);store_word(0x102c,64);
    sdk_arranger_bounce_save_range(path,0,frames,60);
}
void receive(wire::Kind kind,u32 a=0,u32 b=0,u32 c=0,u32 session=0){
    const wire::Message message={kind,session?session:cookie,{a,b,c}};
    sdk_arranger_bounce_receive(&message);
}
void request_start(){
    const u32 before=sent.size();
    require(sdk_arranger_bounce_request(0,2,false)==0,"request accepted");
    require(sent.size()==before,"no recorder request before exclusive dispatcher acknowledgement");
    sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    require(sent.size()==before+1&&sent.back().kind==wire::Prepare,"prepare follows isolation");
    require(!running,"transport waits remote Ready");
}
void ready_start(){
    receive(wire::Ready,64,1);
    require(!running,"receive only publishes Ready");
    sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    require(running&&state==2,"dispatcher starts after remote ready");
}
void stop_capture(){
    receive(wire::Progress,request.frames+128,400);
    running=false;sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    require(sent.back().kind==wire::Stop,"stop request at clip boundary");
    receive(wire::Stopped,request.frames+192,402);sdk_arranger_bounce_poll();
    require(state==3&&saving,"save follows remote stop acknowledgement");
    require(queued_frames==request.frames+128,"save range removes acknowledged stopped preroll");
}
void write_result(bool ok=true,u32 hash=0){
    const char* path=reinterpret_cast<const char*>(path_memory);
    receive(wire::WriteResult,ok?1:0,queued_frames,hash?hash:path_hash(path));
}
void complete_save(){sdk_arranger_bounce_saved(0x1000,queued_frames);write_result();sdk_arranger_bounce_poll();}
void settle(){
    sdk_arranger_bounce_poll();
    require(active&&release_sent,"remote remains owned until release reply");
    require(spinner_active == saving,"native save indicator remains until writer and destination drain");
    receive(wire::Released);sdk_arranger_bounce_poll();
    sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    require(!active,"release and dispatcher finish settle");
    require(!spinner_active && spinner_closes == saves,"every owned native save indicator closes exactly once on success, failure and cancellation");
}
void cancel_before_ready(){
    sdk_arranger_bounce_cancel();sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    require(sent.back().kind==wire::Stop,"cancel sends remote Stop");
    receive(wire::Ready,64,1);sdk_arranger_bounce_dispatch(1);
    require(!running,"late Ready cannot start cancelled clip");
    receive(wire::Stopped,64,1);sdk_arranger_bounce_poll();settle();
}
}
unsigned char* fake_memory(u32 address){
    require(address!=0x842e2a18&&!(address>=0x8429d5b0&&address<0x8429d5f0)&&
        address!=0x84a50b7c&&address!=0x84a50b80,"master must not inspect slave recorder memory");
    if(address>=0x1000&&address<0x1080)return editor_memory+address-0x1000;
    if(address>=0x2000&&address<0x2040)return path_memory+address-0x2000;
    return reinterpret_cast<unsigned char*>(&globals[address]);
}
void* fake_function(u32 a){
#define F(address,function) if(a==address)return reinterpret_cast<void*>(&function)
    F(0x8d5be290,clock_ms);F(0x8d584224,generation);F(0x8c0a424c,get_editor);
    F(0x8c0a46b4,set_state);F(0x8d4d8874,native_range);F(0x8c0a4bcc,save);F(0x8c0a44f0,saved);F(0x8d54a26c,close_spinner);
#undef F
    std::printf("unexpected master stock call %x\n",a);std::exit(1);
}
void sdk_arranger_bounce_send(u32 kind,u32 session,u32 a,u32 b,u32 c){
    sent.push_back(wire::Message{static_cast<wire::Kind>(kind),session,{a,b,c}});
}
opxy::arranger::Model& sdk_arranger_model(){return model;}
bool sdk_arranger_project_edit_ready(){return true;}
bool sdk_arranger_project_linear(){return true;}
bool sdk_arranger_bounce_destination_ready(u32,u32,bool){return destination_ok;}
bool sdk_arranger_bounce_destination_begin(u32,u32,bool,const char*,u32){++commits;return destination_ok;}
u32 sdk_arranger_bounce_destination_status(){return destination_result;}
bool sdk_arranger_bounce_destination_busy(){return destination_working;}
void sdk_arranger_bounce_destination_cancel(){}
bool sdk_arranger_bounce_begin(u32,u32,u32){++starts;running=false;return begin_ok;}
bool sdk_arranger_bounce_start(){running=start_ok;return start_ok;}
void sdk_arranger_bounce_stop(){running=false;}
void sdk_arranger_bounce_end(){++ends;running=false;}
bool sdk_arranger_bounce_transport_running(){return running;}
int main(){
    store_word(0x840a0524,0x8888);store_word(0x84039384,3);store_word(0x84039394,1234);require(model.insertAt(0,{0,0,0,1920,0,1920}),"clip");
    require(model.setMuted(0,true),"mute source instance");
    require(sdk_arranger_bounce_request(0,2,false)==5 && sdk_arranger_bounce_request(0,3,true)==5,
        "muted sampler and drum requests rejected even with replacement");
    require(model.clip(0)->muted && !active && sent.empty() && !starts && !commits,
        "muted refusal preserves state and never starts capture or destination work");
    require(model.setMuted(0,false),"unmute source instance");
    globals[0x860c6adc+0x305f]=1;
    require(sdk_arranger_bounce_request(0,2,false)==1,"reject native REC armed");globals[0x860c6adc+0x305f]=0;
    request_start();const u32 first_cookie=cookie;
    receive(wire::Ready,999,999,0,cookie+1);require(!ready,"wrong cookie Ready ignored");
    require(sdk_arranger_bounce_request(0,2,false)==1,"exclusive request");
    ready_start();stop_capture();
    sdk_arranger_bounce_saved(0x1000,queued_frames);sdk_arranger_bounce_poll();
    require(active&&state==3&&!commits,"native completion alone cannot prove writer success");
    write_result();sdk_arranger_bounce_poll();require(state==7&&commits==1,"two completions allow assignment");
    store_word(0x84039384,2);store_word(0x84039394,99999);
    settle();require(state==4&&ends==1,"success completes after both releases");
    require(fake_word(0x84039384)==3&&fake_word(0x84039394)==1234,"native mirror restored only after remote release");
    require(model.clip(0)&&model.clip(0)->length==1920,"source preserved");
    request_start();require(cookie!=first_cookie,"new session cookie");
    receive(wire::Stopped,999,99,0,first_cookie);receive(wire::Released,0,0,0,first_cookie);
    require(!stop_ack&&!released,"stale stop/release ignored");cancel_before_ready();require(state==5,"cancel before Ready");
    request_start();milliseconds+=3001;sdk_arranger_bounce_poll();sdk_arranger_bounce_dispatch(1);
    require(stop_sent&&!stop_ack&&active,"arming timeout drains remote start and stop");
    ArrangerBounceDiagnostic diagnostic{};sdk_arranger_bounce_diagnostic(&diagnostic);
    require(diagnostic.timeout_flags&0x80000000u,"timeout diagnostic latched");
    receive(wire::Stopped,0,0);sdk_arranger_bounce_poll();settle();require(state==6,"timeout fails after ack");
    request_start();ready_start();stop_capture();write_result(false);
    sdk_arranger_bounce_poll();require(state==3,"writer result alone cannot bypass native completion");
    sdk_arranger_bounce_saved(0x1000,queued_frames);sdk_arranger_bounce_poll();settle();
    require(state==6&&commits==1,"writer failure never assigns");
    require(sdk_arranger_bounce_failure()==3,"writer failure identified");
    request_start();ready_start();stop_capture();write_result(true,123);
    sdk_arranger_bounce_saved(0x1000,queued_frames);sdk_arranger_bounce_poll();settle();
    require(state==6&&commits==1,"foreign file fingerprint never assigns");
    require(sdk_arranger_bounce_failure()==4,"saved file mismatch identified");
    request_start();ready_start();
    running=false;sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    receive(wire::Stopped,trim_frames+request.frames-1,400);sdk_arranger_bounce_poll();settle();
    require(state==6&&sdk_arranger_bounce_failure()==2,"short capture remains rejected and is identified");
    request_start();ready_start();stop_capture();destination_ok=false;complete_save();settle();
    require(state==6&&sdk_arranger_bounce_failure()==5,"destination rejection identified");
    destination_ok=true;
    request_start();ready_start();stop_capture();++gen;
    complete_save();settle();require(state==5&&commits==2,"project swap prevents assignment");
    request_start();receive(wire::Error,1);receive(wire::Released);
    sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();receive(wire::Stopped);
    sdk_arranger_bounce_poll();settle();require(state==6,"rejected Prepare settles");
    request_start();ready_start();stop_capture();destination_working=true;destination_result=0;
    complete_save();require(state==7,"async destination holds session");
    sdk_arranger_bounce_cancel();sdk_arranger_bounce_poll();require(!release_sent,"pending destination blocks remote release");
    destination_working=false;sdk_arranger_bounce_poll();settle();require(state==5,"destination cancel settles");
    destination_result=1;
    const u32 before=sent.size();require(sdk_arranger_bounce_request(0,2,false)==0,"early cancel request");
    sdk_arranger_bounce_cancel();sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    require(!active&&state==5&&sent.size()==before,"cancel before isolation never contacts remote");
    begin_ok=false;require(sdk_arranger_bounce_request(0,2,false)==0,"failed begin request");
    sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();require(!active&&state==6,"failed exclusive begin settles locally");begin_ok=true;
    request_start();ready_start();stop_capture();sdk_arranger_bounce_cancel();sdk_arranger_bounce_poll();
    require(active&&release_sent,"cancel while writing requests remote drain");
    sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    require(active&&!finish_requested,"master stays exclusive until remote writer drains");
    settle();require(state==5,"cancel waits remote release acknowledgement");
    request_start();ready_start();stop_capture();
    sdk_arranger_bounce_saved(0x1000,queued_frames-1);write_result();sdk_arranger_bounce_poll();settle();
    require(state==6,"mismatched native completion fails instead of hanging");
    request_start();receive(wire::Ready,63,1);sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();
    receive(wire::Stopped,0,0);sdk_arranger_bounce_poll();settle();require(state==6,"invalid Ready rejected");
    request_start();receive(wire::Ready,64,1);milliseconds+=3001;sdk_arranger_bounce_poll();
    sdk_arranger_bounce_dispatch(1);receive(wire::Stopped,64,1);sdk_arranger_bounce_poll();settle();
    require(state==6,"Ready without dispatcher start still times out");
    request_start();ready_start();receive(wire::Progress,128,2);receive(wire::Progress,64,1);
    sdk_arranger_bounce_dispatch(1);sdk_arranger_bounce_poll();receive(wire::Stopped,128,2);
    sdk_arranger_bounce_poll();settle();require(state==6,"regressing Progress rejected");
    request_start();ready_start();stop_capture();receive(wire::Error,1);
    sdk_arranger_bounce_poll();settle();require(state==6,"save Error without native completion releases after remote drain");
    sdk_arranger_bounce_saved(0x1000,12);require(forwarded==1,"ordinary completion forwards");
    diagnostic_polls=65535;count(diagnostic_polls);require(diagnostic_polls==65535,"counter saturation");
    for(u32 tempo=200;tempo<=3000;tempo+=37)for(u32 ticks=0;ticks<260000;ticks+=191){
        const __UINT64_TYPE__ numerator=static_cast<__UINT64_TYPE__>(ticks)*26460000u;
        const u32 divisor=tempo*1920u;const __UINT64_TYPE__ exact=(numerator+divisor-1)/divisor;
        const u32 expected=exact>6000&&exact<=882000-4096?static_cast<u32>(exact):0;
        require(duration_frames(ticks,tempo)==expected,"exact rounded frame duration");
    }
    require(!duration_frames(0xffffffffu,200)&&!duration_frames(7680,0),"duration overflow rejected");
    return 0;
}
