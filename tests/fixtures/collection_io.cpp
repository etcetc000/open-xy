#include "stock.hpp"
#include "collection_io.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using namespace opxy;
namespace ci=opxy::collection::io;
namespace ca=opxy::collection::activation;
namespace {
struct File { u16 flags; u32 at; bool writing; } file;
std::vector<unsigned char> data;
std::string mode,trace;
bool role_gui=true,locked,aborted;
u32 closes,flushes,allocations,releases,last_wait;
u32 requested_bytes=100;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
u32 role() { return role_gui; }
u32 ticks() { return 0xfffffff0; }
void delay(u32 count) { require(count==1,"wrong resource progress delay"); }
u32 lock(u32 handle,u32 wait) {
    require(handle==1 && (wait==0xffffffff || !wait) && !locked,"wrong heap lock");last_wait=wait;
    if (mode=="heap-busy") return 0;
    locked=true;return 1;
}
u32 unlock(u32 handle,const void* a,u32 b,u32 c) {
    require(handle==1 && !a && !b && !c && locked,"wrong heap unlock");locked=false;
    return mode=="heap-unlock-error" ? 0 : 1;
}
unsigned char* allocate(u32 heap,u32 bytes) {
    require(locked && heap==0x8c0d03a0 && bytes==requested_bytes,"unlocked heap allocation");++allocations;
    if (mode=="heap-oom") return nullptr;
    if (mode=="heap-overlap") return reinterpret_cast<unsigned char*>(0x8d2d1000);
    return reinterpret_cast<unsigned char*>(0x8c100000);
}
void release(void* pointer) { require(!locked && reinterpret_cast<__UINTPTR_TYPE__>(pointer)==0x8c100000,"free under wrong lock");++releases; }
void* open(const char* path,const char* flags) {
    require(!std::strcmp(path,"/fat32/projects/openxy/attempt.oxc"),"unexpected write path");
    require(!std::strcmp(flags,"rb") || !std::strcmp(flags,"wb"),"wrong file flags");
    if (mode=="open-failed") return nullptr;
    file={0,0,!std::strcmp(flags,"wb")};trace+=file.writing ? 'W' : 'R';
    if (file.writing) data.clear();
    else {
        if (mode=="corrupt") data.back()^=1;
        if (mode=="trailing") data.push_back(0x17);
        if (mode=="short") data.pop_back();
    }
    if (mode=="abort-open") aborted=true;
    return &file;
}
u32 read(void* out,u32 size,u32 count,void* handle) {
    require(handle==&file && !file.writing && size==1,"bad read ABI");
    u32 n=u32(data.size())-file.at;if (n>count) n=count;if (n>7) n=7;
    if (n) std::memcpy(out,data.data()+file.at,n);
    file.at+=n;
    if (mode=="abort-read") aborted=true;
    if (n<count && file.at==data.size()) file.flags|=mode=="read-error" ? 0x200 : 0x100;
    return n;
}
u32 write(const void* in,u32 size,u32 count,void* handle) {
    require(role_gui && handle==&file && file.writing && size==1 && count==160,"bad marker write");
    if (mode=="write-short") --count;
    data.assign(static_cast<const unsigned char*>(in),static_cast<const unsigned char*>(in)+count);
    if (mode=="write-error") file.flags|=0x200;
    if (mode=="abort-write") aborted=true;
    return count;
}
int close(void* handle) {
    require(handle==&file,"wrong file close");++closes;trace+='C';
    if (mode=="abort-close") aborted=true;
    return mode=="close-error" ? -1 : 0;
}
i32 flush() {
    require(role_gui && trace=="WC","flush not after closed marker");++flushes;trace+='F';
    if (mode=="abort-flush") aborted=true;
    return mode=="flush-error" ? -1 : 0;
}
bool cancelled(void*) { return aborted; }
}
opxy::platform::Address opxy::platform::host_stock_address(u32 address) {
    using opxy::platform::Address;
    switch(address) {
    case 0x8d5c3eac:return reinterpret_cast<Address>(role);
    case 0x8c03ee20:return reinterpret_cast<Address>(ticks);
    case 0x8c040188:return reinterpret_cast<Address>(delay);
    case 0x8c03e6f4:return reinterpret_cast<Address>(lock);
    case 0x8c03e9c8:return reinterpret_cast<Address>(unlock);
    case 0x8c048788:return reinterpret_cast<Address>(allocate);
    case 0x8d4d2910:return reinterpret_cast<Address>(release);
    case 0x8fed0348:return reinterpret_cast<Address>(open);
    case 0x8fed05bc:return reinterpret_cast<Address>(read);
    case 0x8fed0780:return reinterpret_cast<Address>(write);
    case 0x8fed00ae:return reinterpret_cast<Address>(close);
    case 0x8c0c0f00:return reinterpret_cast<Address>(flush);
    default:throw std::runtime_error("unexpected stock call");
    }
}
u32 opxy::platform::word(u32 address) {
    if (mode=="heap-uninitialized") return 0;
    if (address==0x08036aa4) return 1;
    if (address==0x08036aa8) return 0x8c0d03a0;
    if (address==0x8c0d03ac) {
        require(locked,"heap metadata read outside lock");
        if (mode=="heap-expanded") return 0xf00000;
        if (mode=="heap-expanded-low") return 0xa00000;
        return mode=="heap-low" ? 0x800060 : 0x1000000;
    }
    if (address==0x8c0d03b8) { require(locked,"heap metadata read outside lock");return 0x8c0d1000; }
    throw std::runtime_error("unexpected heap word");
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    mode=argv[1];
    require(ci::ticks()==0xfffffff0,"tick ABI wrong");
    if (mode.substr(0,4)=="heap") {
        const bool expanded=mode=="heap-expanded" || mode=="heap-expanded-low";
        const ci::Heap standard={0x8d2d0380,0x200000,0x8c0d0380,0x1200000,0x800000};
        ci::Heap heap=expanded ? ci::Heap{0x8d0d0380,0x400000,0x8c0d0380,0x1000000,0x800000} : standard;
        const auto a=ci::allocator(heap);
        if (expanded) requested_bytes=0x300000;
        if (!expanded) require(!a.allocate(a.context,0x300000) && !allocations,"standard heap admitted an expanded request");
        const u32 available=ci::contiguous_free(heap);
        require(!locked && !allocations,"heap query allocated or retained mutex");
        if (mode=="heap") require(available==0x1000000,"wrong contiguous-free lower bound");
        if (mode=="heap-busy" || mode=="heap-uninitialized" || mode=="heap-unlock-error")
            require(!available,"failed heap query reported capacity");
        require(!a.allocate(a.context,0) && !a.allocate(a.context,0xffffffff),"invalid heap size accepted");
        require(!allocations,"invalid request touched heap");
        unsigned char* p=mode=="heap-try" || mode=="heap-busy" ? ci::try_allocate(heap,requested_bytes) :
            a.allocate(a.context,requested_bytes);
        require(!locked,"heap mutex retained");
        if (mode=="heap" || mode=="heap-try" || mode=="heap-expanded") {
            require(p!=nullptr,"allocation failed");a.release(a.context,p);require(releases==1,"allocation leaked");
            require(last_wait==(mode=="heap-try" ? 0 : 0xffffffff),"wrong bootstrap blocking policy");
        }
        else require(!p && !releases,"unsafe allocation admitted or freed");
        return 0;
    }
    ca::Limits limits={};ca::Control control={};control.phase=ca::pending;control.generation=7;control.slot=1;
    control.digest[0]=1;
    if (mode=="audio") role_gui=false;
    if (mode=="invalid") control.generation=0;
    if (mode=="abort-before") aborted=true;
    const bool good=ci::persist_attempt(control,limits,cancelled);
    if (mode=="normal") {
        require(good && trace=="WCFRC" && closes==2 && flushes==1,"marker not flushed/read back");
        ca::Control parsed={};opxy::collection::Sha256 hash={};
        require(ca::read_control(data.data(),u32(data.size()),limits,parsed,hash) && parsed.generation==7 && parsed.phase==ca::pending,"invalid persisted marker");
    } else {
        require(!good,"marker failure admitted");
        if (mode=="audio" || mode=="invalid") require(trace.empty(),"invalid write touched storage");
        if (mode=="flush-error") require(trace=="WCF" && closes==1,"flush error ignored");
        if (mode=="open-failed") require(!closes,"closed unopened file");
        if (mode=="abort-before") require(trace.empty(),"aborted worker opened file");
        if (mode=="abort-open" || mode=="abort-write" || mode=="abort-close")
            require(trace=="WC","aborted write started another operation");
        if (mode=="abort-flush") require(trace=="WCF","aborted flush started readback");
        if (mode=="abort-read") require(trace=="WCFRC" && file.at==7,"aborted read continued");
    }
    return 0;
}
