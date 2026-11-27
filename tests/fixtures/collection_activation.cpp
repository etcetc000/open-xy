#include "collection_activation.hpp"
#include "collection_handoff.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace opxy;
namespace ca=opxy::collection::activation;
namespace {
struct Source { std::vector<unsigned char> data; u32 at, chunk; bool fail; };
struct Heap { unsigned char* allocation; u32 bytes, allocations, releases; bool fail; };
u32 read(void* context,void* out,u32 count) {
    Source& s=*static_cast<Source*>(context);
    if (s.chunk && count>s.chunk) count=s.chunk;
    const u32 left=static_cast<u32>(s.data.size())-s.at;
    if (count>left) count=left;
    if (count) std::memcpy(out,s.data.data()+s.at,count);
    s.at+=count; return count;
}
bool failed(void* context) {
    Source& s=*static_cast<Source*>(context); return s.fail && s.at==s.data.size();
}
bool ended(void* context) {
    Source& s=*static_cast<Source*>(context); return !s.fail && s.at==s.data.size();
}
unsigned char* allocate(void* context,u32 bytes) {
    Heap& h=*static_cast<Heap*>(context); ++h.allocations;
    if (h.fail) return nullptr;
    if (bytes>0x1f8000) std::abort();
    h.bytes=bytes; h.allocation=new unsigned char[bytes+32];
    std::memset(h.allocation,0xa7,bytes+32);
    return h.allocation+16;
}
void release(void* context,unsigned char* memory) {
    Heap& h=*static_cast<Heap*>(context);
    if (memory!=h.allocation+16) std::abort();
    for (u32 i=0;i<16;++i)
        if (h.allocation[i]!=0xa7 || h.allocation[16+h.bytes+i]!=0xa7) std::abort();
    delete[] h.allocation; h.allocation=nullptr; ++h.releases;
}
int policy() {
    ca::Limits limits={};
    unsigned char memory=0;
    ca::Image image={}; image.status=ca::staged; image.bytes=&memory;
    image.version=1;
    ca::Gate gate={false,false,true,17,false};
    ca::Peer peer={}; peer.prepared=true; peer.epoch=17;
    if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 29;
    image.version=2; image.resources_count=1;
    if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 30;
    gate.resources_ready=true;
    if (ca::decide(image,limits,gate,peer)!=ca::prepare_attempt) return 10;
    gate.bypass=true;
    if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 11;
    gate.bypass=false; gate.previous_attempt=true;
    if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 12;
    gate.previous_attempt=false; gate.quiescent=false;
    if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 13;
    gate.quiescent=true; peer.epoch=16;
    if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 14;
    peer.epoch=17; peer.prepared=false;
    if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 15;
    peer.prepared=true;
    for (u32 i=0;i<32;++i) {
        peer.digest[i]=1;
        if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 16;
        peer.digest[i]=0; peer.platform[i]=1;
        if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 17;
        peer.platform[i]=0; peer.abi[i]=1;
        if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 18;
        peer.abi[i]=0;
    }
    image.status=ca::checksum;
    if (ca::decide(image,limits,gate,peer)!=ca::embedded) return 19;
    for (u32 cache=0;cache<2;++cache) for (u32 committed=0;committed<2;++committed)
        if (ca::after_copy(cache,committed)!=(cache && committed ? ca::handoff_ready : ca::reset_required)) return 20;
    ca::Control selected={}; selected.phase=ca::armed; selected.generation=10; selected.slot=1; selected.digest[0]=7;
    ca::Control attempt=selected; attempt.phase=ca::pending;
    if (!ca::rollback_needed(selected,true,true,attempt)) return 21;
    if (!ca::rollback_needed(selected,true,false,attempt)) return 22;
    if (ca::rollback_needed(selected,false,false,attempt)) return 23;
    attempt.phase=ca::confirmed;
    if (ca::rollback_needed(selected,true,true,attempt)) return 24;
    attempt.digest[0]=8;
    if (!ca::rollback_needed(selected,true,true,attempt)) return 25;
    attempt=selected; attempt.phase=ca::pending; attempt.generation=11;
    if (!ca::rollback_needed(selected,true,true,attempt)) return 26;
    attempt.generation=9;
    if (ca::rollback_needed(selected,true,true,attempt)) return 27;
    selected.phase=ca::disabled;
    if (!ca::rollback_needed(selected,false,false,attempt)) return 28;
    return 0;
}
struct Sync { u32 calls,base,bytes;bool ok; };
bool sync(void* context,u32 base,u32 bytes) {
    Sync& s=*static_cast<Sync*>(context);++s.calls;
    if (base!=s.base || bytes!=s.bytes) std::abort();
    return s.ok;
}
void handoff(ca::Image image,const ca::Limits& limits,const char* mode) {
    std::vector<unsigned char> memory(limits.bytes+64,0xa7);
    Sync s={0,limits.base,limits.bytes,std::strcmp(mode,"handoff-cache-failure")!=0};
    ca::HandoffMemory destination={memory.data()+32,&s,sync};
    bool commit=std::strcmp(mode,"handoff-no-permit")!=0;
    ca::HandoffResult expected=s.ok ? ca::synchronized : ca::overwritten;
    if (!commit) expected=ca::untouched;
    if (!std::strcmp(mode,"handoff-corrupt")) { image.bytes[image.initialized-1]^=1;expected=ca::untouched; }
    if (!std::strcmp(mode,"handoff-metadata")) { image.text_bytes-=2;expected=ca::untouched; }
    if (!std::strcmp(mode,"handoff-overlap")) { destination.bytes=image.bytes;expected=ca::untouched; }
    opxy::collection::Sha256 hash;
    if (ca::install(image,limits,commit,destination,hash)!=expected ||
        s.calls!=(expected==ca::untouched ? 0u : 1u)) std::abort();
    for (u32 i=0;i<memory.size();++i) {
        unsigned char byte=0xa7;
        if (expected!=ca::untouched && 32<=i && i<32+limits.bytes)
            byte=i<32+image.initialized ? image.bytes[i-32] : 0;
        if (memory[i]!=byte) std::abort();
    }
}
}
int main(int argc,char** argv) {
    if (argc==2 && !std::strcmp(argv[1],"policy")) return policy();
    if (argc!=4) return 2;
    std::ifstream file(argv[1],std::ios::binary);
    Source source={std::vector<unsigned char>(std::istreambuf_iterator<char>(file),{}),0,
                   static_cast<u32>(std::atoi(argv[2])),!std::strcmp(argv[3],"io-error")};
    Heap heap={}; heap.fail=!std::strcmp(argv[3],"oom");
    ca::Limits limits={}; limits.base=0x8d2d4380; limits.bytes=0x1f8000; limits.entries=3;
    for (u32 i=0;i<32;++i) { limits.platform[i]=i; limits.abi[i]=31-i; }
    if (!std::strcmp(argv[3],"control")) {
        ca::Control control={}; opxy::collection::Sha256 hash={};
        const bool ok=ca::read_control(source.data.data(),static_cast<u32>(source.data.size()),limits,control,hash);
        if (ok) {
            unsigned char encoded[160];
            if (!ca::encode_control(control,limits,encoded,hash) || std::memcmp(encoded,source.data.data(),160)) return 5;
        }
        std::printf("%u %u %u %u\n",ok,control.phase,control.generation,control.slot);
        return 0;
    }
    ca::Workspace workspace={};
    ca::Image image=ca::stage({&source,read,failed,ended},limits,{&heap,allocate,release},workspace);
    const bool owned=image.bytes!=nullptr;
    if (image.status==ca::staged) {
        if (!owned || std::memcmp(image.bytes,source.data.data()+ca::header_bytes,image.initialized)) return 3;
        if (!std::strncmp(argv[3],"handoff-",8)) handoff(image,limits,argv[3]);
        release(&heap,image.bytes);
    } else if (owned) return 4;
    std::printf("%u %u %u %u\n",image.status,heap.allocations,heap.releases,source.at);
    return 0;
}
