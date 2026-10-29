#pragma once
#include <opxy/sample_display.hpp>
namespace opxy { namespace platform { namespace sample_display_wire {
const u32 size=24;
enum Kind { Subscribe=1, Begin=2, Wave=3, Marker=4, Commit=5 };
inline u32 read(const u8* p) { return u32(p[0])|(u32(p[1])<<8)|(u32(p[2])<<16)|(u32(p[3])<<24); }
inline void write(u8* p,u32 v) { for(u32 i=0;i<4;++i)p[i]=static_cast<u8>(v>>(8*i)); }
inline void packet(u8* p,u32 kind,u32 track,u32 index,u32 cookie,u32 generation,u32 a,u32 b) {
    p[0]=16;p[1]='O';p[2]='V';p[3]=1;p[4]=static_cast<u8>(kind);p[5]=static_cast<u8>(track);p[6]=static_cast<u8>(index);p[7]=0;
    write(p+8,cookie);write(p+12,generation);write(p+16,a);write(p+20,b);
}
inline bool service(const void* raw,u32 n) { const u8* p=static_cast<const u8*>(raw);return p&&n>=3&&p[0]==16&&p[1]=='O'&&p[2]=='V'; }
inline bool valid(const u8* p,u32 n) {return n==size&&p[3]==1&&p[4]>=Subscribe&&p[4]<=Commit&&p[5]<8&&!p[7]&&read(p+8);}
struct Receiver {
    SampleDisplaySnapshot pending{}, complete{};
    u32 cookie=0, generation=0, wave_low=0,wave_high=0,markers=0;
    bool assembling=false,ready=false;
    void reset(u32 next) {cookie=next;assembling=ready=false;generation=0;}
    bool receive(const u8* p,u32 n) {
        if(!valid(p,n)||read(p+8)!=cookie||p[4]==Subscribe)return false;
        const u32 kind=p[4],gen=read(p+12),index=p[6],a=read(p+16),b=read(p+20);
        if(kind==Begin) {
            if(!gen||index||!a||(b&~7u)||(generation&&static_cast<i32>(gen-generation)<0))return false;
            pending=SampleDisplaySnapshot{};pending.track=p[5];pending.generation=gen;pending.frames=a;pending.flags=b;
            generation=gen;wave_low=wave_high=markers=0;assembling=true;return false;
        }
        if(!assembling||gen!=generation||p[5]!=pending.track)return false;
        if(kind==Wave&&index<64) {
            pending.minimum[index*2]=static_cast<i16>(a);pending.maximum[index*2]=static_cast<i16>(a>>16);
            pending.minimum[index*2+1]=static_cast<i16>(b);pending.maximum[index*2+1]=static_cast<i16>(b>>16);
            if(index<32)wave_low|=1u<<index;else wave_high|=1u<<(index-32);
        } else if(kind==Marker&&index<4&&a<pending.frames) {
            pending.markers[index]=SampleMarker{a,b&65535u,b>>16};markers|=1u<<index;
        } else if(kind==Commit&&index<=4&&!a&&!b&&wave_low==0xffffffffu&&wave_high==0xffffffffu&&markers==((1u<<index)-1)) {
            pending.marker_count=index;complete=pending;ready=true;assembling=false;return true;
        }
        return false;
    }
};
}}}
