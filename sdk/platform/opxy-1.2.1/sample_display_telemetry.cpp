#include "sample_display_telemetry.hpp"
#include "sample_display_wire.hpp"
#include "stock.hpp"
#include "barrier.hpp"
namespace {
using namespace opxy;
using namespace opxy::platform;
namespace wire=sample_display_wire;
// All of these are processor-local. Only encoded value packets cross processors.
volatile u32 request_sequence,request_cookie,request_track,request_engine;
volatile u32 mailbox_ready,clear_id,clear_cookie,clear_sequence;
SampleDisplaySnapshot working{},mailbox{},sending{};
u32 working_cookie,working_engine,cursor,mailbox_cookie;
u32 send_cookie,send_step=70,last_send,request_received;
wire::Receiver receiver;
u32 gui_track=8,gui_engine,gui_cookie,gui_received,last_request,gui_seen;
bool gui_request;
u32 now() {return stock<u32(u32,u32)>(0x8d5be290)(0,0);}
bool master() {return stock<u32()>(0x8d5c3eac)()!=0;}
void send(u32 kind,u32 track,u32 index,u32 cookie,u32 generation,u32 a,u32 b) {
    u8 p[24];wire::packet(p,kind,track,index,cookie,generation,a,b);
    stock<void(u32,u32,const void*,u32,u32,u32,u32)>(0x11a00ec0)(0x841460b4,0xa4,p,24,0,255,255);
}
bool requested(u32 engine,u32 track,u32& cookie) {
    const u32 sequence=request_sequence;barrier();
    cookie=request_cookie;const bool match=cookie&&request_engine==engine&&request_track==track;
    barrier();return !(sequence&1)&&request_sequence==sequence&&match;
}
bool cleared(u32 cookie,u32 voice) {
    const u32 sequence=clear_sequence;barrier();
    const bool match=clear_cookie==cookie&&clear_id==voice;
    barrier();return !(sequence&1)&&clear_sequence==sequence&&match;
}
u32 packed(i16 lo,i16 hi) {return u32(static_cast<u16>(lo))|(u32(static_cast<u16>(hi))<<16);}
}
void sdk_sample_visual_render(opxy::u32 engine,opxy::u32 track,opxy::u32 voice,
                             const opxy::SampleView* sample,const opxy::SampleVisual* visual) {
    u32 cookie;
    if(!voice||!sample||!visual||!sample->data||!sample->frames||
       (sample->channels!=1&&sample->channels!=2)||!requested(engine,track,cookie))return;
    if(working_cookie==cookie&&working.generation&&static_cast<i32>(voice-working.generation)<0)return;
    if(working_cookie!=cookie||working_engine!=engine||working.generation!=voice) {
        working=SampleDisplaySnapshot{};working.track=track;working.generation=voice;working.frames=sample->frames;
        working.flags=sample_waveform_approximate|sample_voice_live;working_cookie=cookie;working_engine=engine;cursor=0;
    }
    // 512 uniformly distributed source frames, at most64 PCM reads per block.
    // No sample pointer is retained by this service, even within the same CPU.
    for(u32 reads=0;cursor<512&&reads+sample->channels<=64;reads+=sample->channels,++cursor) {
        const u32 frame=(sample->frames/512)*cursor+((sample->frames%512)*cursor)/512;
        const u32 bin=cursor/4;
        for(u32 channel=0;channel<sample->channels;++channel) {
            const i16 value=sample_at(*sample,frame,channel);
            if(!(cursor%4)&&!channel)working.minimum[bin]=working.maximum[bin]=value;
            else {if(value<working.minimum[bin])working.minimum[bin]=value;if(value>working.maximum[bin])working.maximum[bin]=value;}
        }
    }
    if(cursor==512)working.flags|=sample_waveform_ready;
    working.marker_count=0;
    for(u32 i=0;i<visual->count&&i<4;++i) {
        const SampleMarker m=visual->markers[i];
        if(m.frame<sample->frames&&m.phase_q16<=65535&&m.flags<=65535)working.markers[working.marker_count++]=m;
    }
    barrier();
    if(!mailbox_ready) {mailbox=working;mailbox_cookie=cookie;barrier();mailbox_ready=1;}
}
void sdk_sample_visual_clear(opxy::u32 engine,opxy::u32 track,opxy::u32 voice) {
    u32 cookie;
    if(requested(engine,track,cookie)&&cookie==working_cookie&&voice==working.generation) {
        ++clear_sequence;barrier();clear_cookie=cookie;clear_id=voice;barrier();++clear_sequence;
    }
}
bool sdk_sample_visual_message(const void* raw,opxy::u32 size) {
    if(!wire::service(raw,size))return false;
    const u8* p=static_cast<const u8*>(raw);
    if(!wire::valid(p,size))return true; // Never forward malformed private service traffic to stock.
    if(master()) {
        if(p[4]!=wire::Subscribe&&receiver.receive(p,size))gui_received=now();
    } else if(p[4]==wire::Subscribe&&!p[6]&&!wire::read(p+12)&&!wire::read(p+20)) {
        ++request_sequence;barrier();request_track=p[5];request_engine=wire::read(p+16);request_cookie=wire::read(p+8);
        request_received=now();barrier();++request_sequence;
    }
    return true;
}
void sdk_sample_visual_poll() {
    const u32 time=now();
    if(master()) {
        if(gui_track<8&&time-gui_seen<500&&(gui_request||time-last_request>=1000)) {
            gui_request=false;last_request=time;send(wire::Subscribe,gui_track,0,gui_cookie,0,gui_engine,0);
        }
        return;
    }
    if(request_cookie&&time-request_received>1500) {
        ++request_sequence;barrier();request_cookie=0;barrier();++request_sequence;
        send_step=70;barrier();mailbox_ready=0;
    }
    // One24-byte packet per millisecond maximum, outside the render callback.
    if(time-last_send<1)return;
    last_send=time;
    if(send_step>=70) {
        barrier();
        if(mailbox_ready) {
            sending=mailbox;send_cookie=mailbox_cookie;barrier();mailbox_ready=0;
        } else if(!cleared(send_cookie,sending.generation)||!(sending.flags&sample_voice_live)) return;
        if(cleared(send_cookie,sending.generation)) {sending.marker_count=0;sending.flags&=~sample_voice_live;}
        send_step=0;
    }
    if(send_cookie!=request_cookie) {send_step=70;return;}
    if(!send_step)send(wire::Begin,sending.track,0,send_cookie,sending.generation,sending.frames,sending.flags);
    else if(send_step<=64) {
        const u32 i=(send_step-1)*2;
        send(wire::Wave,sending.track,send_step-1,send_cookie,sending.generation,
             packed(sending.minimum[i],sending.maximum[i]),packed(sending.minimum[i+1],sending.maximum[i+1]));
    } else if(send_step<65+sending.marker_count) {
        const u32 i=send_step-65;const SampleMarker& m=sending.markers[i];
        send(wire::Marker,sending.track,i,send_cookie,sending.generation,m.frame,m.phase_q16|(m.flags<<16));
    } else {send(wire::Commit,sending.track,sending.marker_count,send_cookie,sending.generation,0,0);send_step=69;}
    ++send_step;
}
namespace opxy {
bool sample_display_snapshot(u32 track,SampleDisplaySnapshot& out) {
    if(!master()||track>=8)return false;
    gui_seen=now();
    const u32 engine=platform::track_engine(track);
    if(track!=gui_track||engine!=gui_engine) {
        gui_track=track;gui_engine=engine;if(!++gui_cookie)++gui_cookie;
        receiver.reset(gui_cookie);gui_request=true;
    }
    if(!receiver.ready||now()-gui_received>500)return false;
    out=receiver.complete;return true;
}
}
