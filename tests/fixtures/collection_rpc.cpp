#include "stock.hpp"
#include "collection_gate.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace opxy;
using namespace opxy::collection::activation;
extern "C" {
u32 sdk_collection_rpc_dispatch(void*) __asm__("sdk_collection_rpc_dispatch");
void sdk_collection_rpc_entry() __asm__("sdk_collection_rpc_entry");
void sdk_collection_rpc_probe() __asm__("sdk_collection_rpc_probe");
u32 sdk_voice_ticks() __asm__("sdk_voice_ticks");
u32 sdk_collection_rpc_stock(void*) __asm__("sdk_collection_rpc_stock");
}
namespace {
struct Park {};
bool gui,inside_helper,inside_handler;
std::string mode,report;
u32 creates,writes,delays,routed,forwarded,client_step;
StartupGate peer={}; GateMailbox peer_mailbox={}; Identity identity={};
void (*deferred)(void*);
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
u32 role() { return gui; }
u32 operation(u32 index) {
    if (!index) return claim;
    if (index<=8) return platform_word+index-1;
    if (index<=16) return abi_word+index-9;
    if (index<=24) return digest_word+index-17;
    const u32 tail[]={generation,slot,freeze,copy,publish,query,cancel};
    require(index<32,"too many client commands"); return tail[index-25];
}
void packet(void* data,u32 kind,u32 argument,u32 nonce,u16 marker) {
    std::memset(data,0,36); unsigned char* bytes=static_cast<unsigned char*>(data);
    bytes[8]=static_cast<unsigned char>(kind);
    *reinterpret_cast<u32*>(bytes+12)=argument;
    *reinterpret_cast<u32*>(bytes+28)=nonce;
    *reinterpret_cast<u16*>(bytes+32)=marker;
}
void constructor(void* data,u32 kind,u32 argument,u32 nonce,u16 marker) {
    require(inside_helper,"RPC constructed on startup thread"); packet(data,kind,argument,nonce,marker);
}
u32 route(u32 router,void*) {
    require(router==0x12345678,"wrong reply router"); ++routed; return 0xc001;
}
u32 send(void* data) {
    require(inside_helper,"RPC waited on startup thread");
    const unsigned char* outgoing=static_cast<const unsigned char*>(data);
    require(outgoing[8]==45,"stock factory rejects unsupported request kind");
    const u32 wire[3]={u32(outgoing[8]) | u32(*reinterpret_cast<const u16*>(outgoing+32))<<16,
                       *reinterpret_cast<const u32*>(outgoing+12),*reinterpret_cast<const u32*>(outgoing+28)};
    alignas(8) unsigned char incoming[96]={};
    packet(incoming,wire[0]&255,wire[1],wire[2],u16(wire[0]>>16));
    const u16 marker=u16(wire[0]>>16);
    const u32 ticket=(marker>>7)&127,op=marker&127;
    const Reply result=peer_mailbox.exchange(ticket,wire[2],op);
    *reinterpret_cast<u32*>(incoming+20)=result.status;
    *reinterpret_cast<u32*>(incoming+24)=result.value;
    // Cooperative model of the remote startup task. Separate tests exercise
    // the actual generated handler while the local audio entry loop is held.
    u32 taken=0,epoch=0,command=0;
    if (mode!="busy" && peer_mailbox.take(taken,epoch,command)) {
        const Reply response=(command==copy || command==publish) ? Reply{refused,peer.phase()} : peer.request(epoch,command,0);
        peer_mailbox.complete(taken,response);
    }
    static_cast<u32*>(data)[5]=*reinterpret_cast<u32*>(incoming+20);
    static_cast<u32*>(data)[6]=*reinterpret_cast<u32*>(incoming+24);
    if (mode=="bad-reply" && op==platform_word && result.status==accepted) static_cast<u32*>(data)[6]^=1;
    return static_cast<u32*>(data)[5];
}
void delay(u32 ticks) {
    require(!inside_handler,"handler blocked");
    if (inside_helper) {
        if (ticks==60000) throw Park{};
        require(ticks==10,"wrong helper poll delay"); return;
    }
    require(ticks==10,"wrong bounded coordinator wait"); ++delays;
    if (!gui && mode=="normal" && client_step<32) {
        const u32 op=operation(client_step),ticket=client_step+1;
        alignas(8) unsigned char incoming[96]={};
        packet(incoming,45,0xcf585950,0x10203040,u16(0x8000|ticket<<7|op));
        inside_handler=true; sdk_collection_rpc_dispatch(incoming); inside_handler=false;
        const u32 status=*reinterpret_cast<u32*>(incoming+20);
        if (status!=busy) {
            require(status==((op==copy || op==publish) ? refused : accepted),"audio command failed");
            ++client_step;
        }
    }
}
void run_helper(void (*fn)(void*)) {
    inside_helper=true;
    try { fn(nullptr); throw std::runtime_error("task returned"); }
    catch (const Park&) { inside_helper=false; }
}
void* create(void (*fn)(void*),const char*,u32 words,void* param,u32 priority,u32* stack,void* tcb) {
    ++creates;
    require(words==2048 && !param && priority==1 && reinterpret_cast<__UINTPTR_TYPE__>(stack)%8==0 &&
            reinterpret_cast<__UINTPTR_TYPE__>(tcb)%8==0,"wrong static task ABI");
    deferred=fn;
    if (mode=="create-failed") return nullptr;
    if (mode!="timeout" && mode!="late") run_helper(fn);
    return reinterpret_cast<void*>(1);
}
void* open(const char* path,const char* flags) {
    require(!inside_handler && !inside_helper,"RPC handler/helper performed file I/O");
    require(!std::strcmp(path,gui ? "/fat32/projects/openxy/gui-rpc-status.oxc" : "/fat32/projects/openxy/audio-rpc-status.oxc") &&
            !std::strcmp(flags,"wb"),"wrong report path"); return &report;
}
u32 write(const void* bytes,u32 size,u32 count,void* file) {
    require(file==&report && size==1,"wrong report write");
    ++writes; report.append(static_cast<const char*>(bytes),count); return count;
}
int close(void* file) { require(file==&report,"wrong close"); return 0; }
}
u32 sdk_voice_ticks() { return 0x12345678; }
u32 sdk_collection_rpc_stock(void*) { ++forwarded; return 0xd00d; }
u32 opxy::platform::word(u32 address) {
    require(address==0x84259220,"unexpected stock global"); return 0x12345678;
}
opxy::platform::Address opxy::platform::host_stock_address(u32 address) {
    using opxy::platform::Address;
    switch(address) {
    case 0x8d5c3eac: return reinterpret_cast<Address>(role);
    case 0x8c0c3c3c: return reinterpret_cast<Address>(constructor);
    case 0x8c0c3a88: return reinterpret_cast<Address>(send);
    case 0x8c0c0c80: return reinterpret_cast<Address>(route);
    case 0x8c040188: return reinterpret_cast<Address>(delay);
    case 0x8c03f744: return reinterpret_cast<Address>(create);
    case 0x8fed0348: return reinterpret_cast<Address>(open);
    case 0x8fed0780: return reinterpret_cast<Address>(write);
    case 0x8fed00ae: return reinterpret_cast<Address>(close);
    default: throw std::runtime_error("unexpected stock call");
    }
}
int main(int argc,char** argv) {
    if (argc!=4) return 2;
    mode=argv[1];gui=std::atoi(argv[2])!=0;
    if (mode=="dispatch") {
        alignas(8) unsigned char data[36]={};
        packet(data,45,0xcf585950,0x10203040,0x8081);
        const int change=std::atoi(argv[3]);
        if (change==28) std::memset(data+28,0,4);
        else if (change>=0) data[change]^=(change>=32 ? 0x80 : 1);
        const u32 result=sdk_collection_rpc_dispatch(data);
        std::printf("%u %u %u %u %u\n",result,*reinterpret_cast<u32*>(data+20),
                    *reinterpret_cast<u32*>(data+24),routed,forwarded); return 0;
    }
    require(std::strlen(argv[3])==192,"missing identity");
    unsigned char* fields[]={identity.platform,identity.abi,identity.digest};
    for (u32 i=0;i<96;++i) {
        char byte[3]={argv[3][i*2],argv[3][i*2+1],0};
        fields[i/32][i%32]=static_cast<unsigned char>(std::strtoul(byte,nullptr,16));
    }
    identity.generation=1; peer.enter(identity,true,0,10000);
    sdk_collection_rpc_entry();
    if (mode=="late") run_helper(deferred);
    sdk_collection_rpc_entry(); sdk_collection_rpc_probe(); sdk_collection_rpc_probe();
    std::printf("{\"report\":%s,\"creates\":%u,\"writes\":%u,\"delays\":%u,\"forwarded\":%u}\n",
                report.c_str(),creates,writes,delays,forwarded); return 0;
}
