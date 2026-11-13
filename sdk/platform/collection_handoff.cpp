#include "collection_handoff.hpp"
namespace opxy { namespace collection { namespace activation {
namespace {
void store(unsigned char* p,u32 value) {
    for (u32 i=0;i<4;++i) p[i]=static_cast<unsigned char>(value>>(i*8));
}
bool digest(const Image& image,const Limits& limits,const unsigned char* data,Sha256& hash) {
    unsigned char header[header_bytes]={};
    const unsigned char magic[8]={'O','X','Y','L','O','A','D','1'};
    for (u32 i=0;i<8;++i) header[i]=magic[i];
    const u32 fields[]={2,header_bytes,limits.base,image.initialized,image.bss_offset,
                        image.bss_bytes,image.text_bytes,image.entries,1,0};
    for (u32 i=0;i<10;++i) store(header+8+i*4,fields[i]);
    for (u32 i=0;i<32;++i) { header[48+i]=limits.platform[i];header[80+i]=limits.abi[i]; }
    store(header+144,image.resources_offset);store(header+148,image.resources_count);
    store(header+152,resource_bytes);
    hash.begin();hash.add(header,sizeof(header));hash.add(data,image.initialized);
    unsigned char actual[32];hash.finish(actual);unsigned char difference=0;
    for (u32 i=0;i<32;++i) difference|=actual[i]^image.digest[i];
    return !difference;
}
}
HandoffResult install(const Image& image,const Limits& limits,bool commit,HandoffMemory memory,Sha256& hash) {
    if (!commit || !memory.bytes || !memory.synchronize || image.status!=staged || !image.bytes ||
        image.version!=2 || !limits.bytes || ((limits.base|limits.bytes)&31) ||
        limits.bytes>0xffffffffu-limits.base || image.entries!=limits.entries ||
        !image.initialized || image.initialized>image.bss_offset || image.bss_offset>limits.bytes ||
        image.bss_bytes>limits.bytes-image.bss_offset || ((image.bss_offset|image.bss_bytes)&3) ||
        !image.resources_count || image.resources_count>maximum_resources ||
        (image.resources_offset&3) || image.text_bytes>image.resources_offset ||
        image.resources_offset>image.initialized ||
        image.resources_count>(image.initialized-image.resources_offset)/resource_bytes) return untouched;
    typedef __UINTPTR_TYPE__ Address;
    const Address source=reinterpret_cast<Address>(image.bytes),target=reinterpret_cast<Address>(memory.bytes);
    if (source>~Address(0)-image.initialized || target>~Address(0)-limits.bytes ||
        !(source+image.initialized<=target || target+limits.bytes<=source) ||
        !digest(image,limits,image.bytes,hash)) return untouched;
    for (u32 i=0;i<limits.bytes;++i) memory.bytes[i]=0;
    for (u32 i=0;i<image.initialized;++i) memory.bytes[i]=image.bytes[i];
    if (!digest(image,limits,memory.bytes,hash)) return overwritten;
    return memory.synchronize(memory.context,limits.base,limits.bytes) ? synchronized : overwritten;
}
}}}
