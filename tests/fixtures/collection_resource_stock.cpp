#include "stock.hpp"
#include "collection_resource_stock.hpp"
#include "collection_io.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
using namespace opxy;
namespace cr=opxy::collection::resources;
namespace cs=opxy::collection::resource_stock;
namespace ca=opxy::collection::activation;
extern "C" {
u32 sdk_collection_try_lock(volatile u32*) __asm__("sdk_collection_try_lock");
void sdk_collection_unlock(volatile u32*) __asm__("sdk_collection_unlock");
}
namespace {
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
cs::View* selected;
u32 fallback_count,svg_calls,file_calls,delays,locks,unlocks,ctors,dtors,reader_ctors,reader_dtors,parses,copies,allocations,fail_at;
bool fail_svg,fail_json;
std::string filename;
std::map<unsigned char*,u32> owned;
void* file_svg(const char* path) {
    ++file_calls;filename=path;
    require(!selected || selected->lock==1,"file/fallback parsed outside work lock");
    return reinterpret_cast<void*>(0x1234);
}
void* parse_svg(char* bytes) {
    ++svg_calls;
    require(selected && selected->lock==1 && selected->working==reinterpret_cast<unsigned char*>(bytes),"SVG parsed outside work lock");
    require(!std::strcmp(bytes,"<svg/>"),"canonical SVG not recopied");bytes[0]='!';
    return fail_svg ? nullptr : reinterpret_cast<void*>(0x5678);
}
bool file_json(const char* path,void* out) { filename=path;static_cast<u32*>(out)[0]=17;return true; }
void json_ctor(void* value,u32 type) { require(!type,"non-null scratch constructor");++ctors;std::memset(value,0,12); }
void json_dtor(void* value) { require(owned.count(static_cast<unsigned char*>(value))!=0,"invalid Json destruction");++dtors; }
void reader_ctor(void* reader) { require(reinterpret_cast<__UINTPTR_TYPE__>(reader)%4==0,"unaligned Reader");++reader_ctors;std::memset(reader,0x5a,120); }
void reader_dtor(void* reader) { require(static_cast<unsigned char*>(reader)[119]==0x5a,"Reader overwritten");++reader_dtors; }
bool parse_json(void* reader,const char* begin,const char* end,void* value,bool comments) {
    ++parses;require(!comments && end-begin==2 && begin[0]=='{' && begin[1]=='}' && end[0]==0,"wrong bounded parse ABI");
    require(static_cast<unsigned char*>(reader)[0]==0x5a && static_cast<u32*>(value)[0]==0,"parse without constructors");
    const_cast<char*>(begin)[0]='!';static_cast<u32*>(value)[0]=0xcafe;
    return !fail_json;
}
void* assign(void* out,const void* source) { ++copies;std::memcpy(out,source,12);return out; }
unsigned char* allocate(void*,u32 bytes) {
    if (++allocations==fail_at) return nullptr;
    unsigned char* p=new unsigned char[bytes];owned[p]=bytes;return p;
}
void release(void*,unsigned char* p) { require(owned.erase(p)==1,"allocation freed twice");delete[] p; }
void reset() { require(owned.empty(),"retained test allocation");fallback_count=svg_calls=file_calls=delays=locks=unlocks=ctors=dtors=reader_ctors=reader_dtors=parses=copies=allocations=fail_at=0;fail_svg=fail_json=false;filename.clear(); }
}
extern "C" u32 sdk_collection_try_lock(volatile u32* word) { ++locks;if (*word) return 0;*word=1;return 1; }
extern "C" void sdk_collection_unlock(volatile u32* word) { require(*word==1,"unlock without owner");++unlocks;*word=0; }
cs::View* sdk_collection_selected_resources() { return selected; }
void sdk_collection_resource_fallback(u32 kind) { require(kind==1 || kind==2,"unknown fallback kind");++fallback_count; }
void opxy::collection::io::delay(u32 count) {
    require(count==1,"wrong work lock delay");++delays;
    if (delays==100) selected->lock=0; // Other task finishes before fallback starts.
}
opxy::platform::Address opxy::platform::host_stock_address(u32 address) {
    using opxy::platform::Address;
    switch(address) {
    case 0x8d5f2400:return reinterpret_cast<Address>(file_svg);
    case 0x8d5f2160:return reinterpret_cast<Address>(parse_svg);
    case 0x8c042490:return reinterpret_cast<Address>(file_json);
    case 0x8c0555c0:return reinterpret_cast<Address>(json_ctor);
    case 0x8c053fc0:return reinterpret_cast<Address>(json_dtor);
    case 0x8c050de8:return reinterpret_cast<Address>(reader_ctor);
    case 0x8c042420:return reinterpret_cast<Address>(reader_dtor);
    case 0x8c052230:return reinterpret_cast<Address>(parse_json);
    case 0x8c054098:return reinterpret_cast<Address>(assign);
    default:throw std::runtime_error("unexpected stock function");
    }
}
int main() {
    unsigned char svg[]="<svg/>",json[]="{}",working[64];cr::Entry entries[2]={};
    entries[0].data=svg;entries[0].bytes=6;entries[0].kind=1;
    entries[1].data=json;entries[1].bytes=2;entries[1].kind=2;
    for (u32 i=0;i<32;++i) entries[0].digest[i]=static_cast<unsigned char>(i);
    std::string path="content/display/openxy-";const char* digits="0123456789abcdef";
    for (u32 i=0;i<32;++i) { path+=digits[i>>4];path+=digits[i&15]; } path+=".svg";
    cr::Cache cache={};cache.entries=entries;cache.count=2;cache.verified=true;
    cs::View view={&cache,working,sizeof(working),nullptr,0};selected=&view;
    reset();
    require(sdk_collection_asset_load("content/display/macroosc.svg")==reinterpret_cast<void*>(0x1234) && filename=="content/display/macroosc.svg" && !fallback_count,"baseline SVG redirected");
    reset();
    for (u32 i=0;i<2;++i) require(sdk_collection_asset_load(path.c_str())==reinterpret_cast<void*>(0x5678),"external SVG failed");
    require(svg_calls==2 && locks==2 && unlocks==2 && !file_calls && !std::strcmp(reinterpret_cast<char*>(svg),"<svg/>"),"canonical SVG mutated or lock leaked");
    reset();fail_svg=true;
    require(sdk_collection_asset_load(path.c_str())==reinterpret_cast<void*>(0x1234) && filename=="content/display/_openxy_loader_1.svg" && fallback_count==1 && !view.lock,"NULL SVG escaped");
    reset();view.lock=1;
    require(sdk_collection_asset_load(path.c_str())==reinterpret_cast<void*>(0x1234) && delays==100 && !svg_calls,"busy work buffer used");view.lock=0;
    reset();std::string absent=path;const u32 hash_at=23;absent[hash_at]=absent[hash_at]=='0' ? '1' : '0';
    require(sdk_collection_asset_load(absent.c_str())==reinterpret_cast<void*>(0x1234) && fallback_count==1 && !svg_calls,"missing cached SVG delegated to absent file");
    reset();selected=nullptr;
    require(sdk_collection_asset_load(path.c_str())==reinterpret_cast<void*>(0x1234) && filename=="content/display/_openxy_loader_1.svg","inactive logical asset opened external path");selected=&view;
    const ca::Allocator allocator={nullptr,allocate,release};
    reset();require(!cs::prepare_catalog(cache,allocator,false) && !allocations && !parses,"catalog parsed before pending marker");
    for (const char* text : {"[[[[[[[[[]]]]]]]]]","{\"x\":\"unterminated}","}"}) {
        reset();entries[1].data=reinterpret_cast<unsigned char*>(const_cast<char*>(text));entries[1].bytes=std::strlen(text);
        require(!cs::prepare_catalog(cache,allocator,true) && !allocations && !parses,"unsafe recursive catalog reached native parser");
    }
    entries[1].data=json;entries[1].bytes=2;
    for (u32 failure=0;failure<4;++failure) {
        reset();fail_at=failure<3 ? failure : 0;fail_json=failure==3;
        void* value=cs::prepare_catalog(cache,allocator,true);
        require(value!=nullptr ? failure==0 : failure!=0,"catalog admission disagreed");
        require(reader_ctors==reader_dtors && json[0]=='{',"Reader leaked or canonical JSON mutated");
        if (value) {
            view.catalog=value;u32 output[3]={99,98,97};
            require(sdk_collection_catalog_load("content/op1_factory.json",output) && output[0]==0xcafe && copies==1 && filename.empty(),"prepared catalog was not copied transactionally");
            require(sdk_collection_catalog_load("content/other.json",output) && output[0]==17 && copies==1,"unowned JSON path redirected");
            cs::destroy_catalog(value,allocator);view.catalog=nullptr;
        }
        require(owned.empty(),"catalog allocation leaked");
        require(dtors==(failure==0 || failure==3 ? 1u : 0u),"incorrect scratch destruction");
    }
    reset();u32 output[3]={};
    require(sdk_collection_catalog_load("content/op1_factory.json",output) && output[0]==17 && fallback_count==1 && !copies,"missing prepared catalog did not preserve baseline");
    return 0;
}
